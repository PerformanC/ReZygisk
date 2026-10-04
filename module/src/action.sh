#!/system/bin/sh

# INFO: Runs from the root manager's "Action" button (Magisk 28+, KernelSU, APatch) and
#         prints NextZygisk's status, including every Zygisk module and whether it works.
#         It works without any WebUI, so Magisk users don't need an external app.

MODDIR=${0%/*}
STATE=/data/adb/rezygisk/state.json
MODULES=/data/adb/modules
MODULES_UPDATE=/data/adb/modules_update

prop_value() {
  grep -m1 "^$2=" "$1" 2>/dev/null | cut -d= -f2-
}

echo "NextZygisk $(prop_value "$MODDIR/module.prop" version)"
echo

if [ ! -f "$STATE" ]; then
  echo "! No status reported yet: the monitor is not running."
  echo "  Reboot, and if it persists, collect logs with a debug build."

  exit 0
fi

# INFO: state.json is written by the monitor with one key per line, which keeps it
#         parseable with plain awk (no jq on Android).
eval "$(awk '
  index($0, "\"monitor\"") { section = "monitor" }
  index($0, "\"rezygiskd\"") { section = "daemon" }
  index($0, "\"zygote\"") { section = "zygote" }

  section == "daemon" && index($0, "\"64\": {") { bits = "64" }
  section == "daemon" && index($0, "\"32\": {") { bits = "32" }

  function value(line) {
    sub(/^[^:]*:[ ]*/, "", line); sub(/,[ ]*$/, "", line); gsub(/"/, "", line)
    gsub(/[^A-Za-z0-9 ._:\/()-]/, "", line)
    return line
  }

  section == "monitor" && index($0, "\"state\"") { print "monitor_state=\"" value($0) "\"" }
  section == "monitor" && index($0, "\"reason\"") { print "monitor_reason=\"" value($0) "\"" }
  section == "daemon" && index($0, "\"state\"") { print "daemon" bits "=\"" value($0) "\"" }
  section == "daemon" && index($0, "\"reason\"") { print "daemon" bits "_reason=\"" value($0) "\"" }
  section == "daemon" && index($0, "\"modules\"") {
    line = $0; sub(/.*\[/, "", line); sub(/\].*/, "", line); gsub(/[", ]+/, " ", line)
    gsub(/[^A-Za-z0-9 ._-]/, "", line)
    print "modules" bits "=\" " line " \""
  }
  section == "zygote" && index($0, "\"64\":") { print "zygote64=\"" value($0) "\"" }
  section == "zygote" && index($0, "\"32\":") { print "zygote32=\"" value($0) "\"" }
' "$STATE")"

case "$monitor_state" in
  0) echo "Monitor: tracing ✅" ;;
  1) echo "Monitor: stopping ⛔" ;;
  2) echo "Monitor: stopped ⛔" ;;
  3) echo "Monitor: exited ❌" ;;
  *) echo "Monitor: unknown ❔" ;;
esac
[ -n "$monitor_reason" ] && echo "  Reason: $monitor_reason"

for bits in 64 32; do
  eval "daemon=\$daemon$bits; reason=\$daemon${bits}_reason; zygote=\$zygote$bits"
  [ -z "$daemon" ] && continue

  if [ "$daemon" = "1" ]; then
    echo "NextZygiskd $bits-bit: running ✅"
  else
    echo "NextZygiskd $bits-bit: not running ❌${reason:+ ($reason)}"
  fi

  if [ "$zygote" = "1" ]; then
    echo "Zygote$bits: injected ✅"
  else
    echo "Zygote$bits: not injected ❌"
  fi
done

# INFO: Same ABI choice as zygiskd: native x86 when available, ARM otherwise.
ABILIST=$(getprop ro.product.cpu.abilist)
case "$ABILIST" in *x86_64*) ABI64=x86_64 ;; *) ABI64=arm64-v8a ;; esac
case "$ABILIST" in *x86*) ABI32=x86 ;; *) ABI32=armeabi-v7a ;; esac

echo
echo "Zygisk modules:"

total=0
working=0

for dir in "$MODULES"/*/ "$MODULES_UPDATE"/*/; do
  [ -d "$dir/zygisk" ] || continue

  id=${dir%/}
  id=${id##*/}
  [ "$id" = "rezygisk" ] && continue

  # INFO: An update in modules_update is reported with its installed copy.
  case "$dir" in
    "$MODULES_UPDATE"/*) [ -d "$MODULES/$id" ] && continue; new=1 ;;
    *) new=0 ;;
  esac

  name=$(prop_value "$dir/module.prop" name)
  [ -z "$name" ] && name=$id
  label="$name ($id)"

  if [ "$new" = "1" ]; then
    echo " 🔄 $label: installed, reboot to activate"

    continue
  fi
  if [ -f "$dir/remove" ]; then
    echo " 🗑️ $label: will be removed on reboot"

    continue
  fi
  if [ -f "$dir/disable" ]; then
    echo " ⏸️ $label: disabled"

    continue
  fi

  total=$((total + 1))
  expected=""
  loaded=""
  missing=""
  daemon_down=""

  for bits in 64 32; do
    eval "daemon=\$daemon$bits; list=\$modules$bits; abi=\$ABI$bits"
    [ -z "$daemon" ] && continue
    [ -f "$dir/zygisk/$abi.so" ] || continue

    expected="$expected $bits"
    case "$list" in
      *" $id "*) loaded="$loaded $bits" ;;
      *)
        missing="$missing $bits"
        [ "$daemon" = "1" ] || daemon_down="$daemon_down $bits"
        ;;
    esac
  done

  if [ -z "$expected" ]; then
    echo " ❌ $label: no Zygisk library for this device ($ABI64)"
  elif [ -z "$missing" ]; then
    working=$((working + 1))
    echo " ✅ $label: working ($(echo $loaded | tr ' ' '/')-bit)"
  else
    note="not loaded in $(echo $missing | tr ' ' '/')-bit"
    [ -n "$daemon_down" ] && note="$note (NextZygiskd $(echo $daemon_down | tr ' ' '/')-bit not running)"
    [ -d "$MODULES_UPDATE/$id" ] && note="$note, update pending reboot"
    if [ -n "$loaded" ]; then
      echo " ⚠️ $label: partial, $note"
    else
      echo " ❌ $label: $note"
    fi
  fi
done

[ "$total" = "0" ] && echo " (none enabled)"

echo
echo "$working of $total enabled Zygisk modules working."
echo
echo "More details: open the NextZygisk WebUI (KernelSU / APatch / MMRL)."

exit 0
