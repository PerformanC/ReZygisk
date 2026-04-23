#!/system/bin/sh
cp "$MODDIR/module.prop.bak" "$MODDIR/module.prop"
set -e

MODDIR=${0%/*}
if [ "$ZYGISK_ENABLED" ]; then
  exit 0
fi

cd "$MODDIR"

if [ "$(which magisk)" ]; then
  for file in ../*; do
    if [ -d "$file" ] && [ -d "$file/zygisk" ] && ! [ -f "$file/disable" ]; then
      if [ -f "$file/post-fs-data.sh" ]; then
        cd "$file"
        log -p i -t "zygisk-sh" "Manually trigger post-fs-data.sh for $file"
        sh "$(realpath ./post-fs-data.sh)"
        cd "$MODDIR"
      fi
    fi
  done
fi

create_sys_perm() {
  mkdir -p $1
  chmod 555 $1
  chcon u:object_r:system_file:s0 $1
}

export TMP_PATH=/data/adb/rezygisk
rm -rf "$TMP_PATH"

create_sys_perm $TMP_PATH

sh /data/adb/post-fs-data.d/rezygisk.sh

# INFO: Utilize the one with the biggest output, as some devices with Tango have the full list
#         in ro.product.cpu.abilist but others only have a subset there, and the full list in
#         ro.system.product.cpu.abilist
CPU_ABIS_PROP1=$(getprop ro.system.product.cpu.abilist)
CPU_ABIS_PROP2=$(getprop ro.product.cpu.abilist)

if [ "${#CPU_ABIS_PROP2}" -gt "${#CPU_ABIS_PROP1}" ]; then
  CPU_ABIS=$CPU_ABIS_PROP2
else
  CPU_ABIS=$CPU_ABIS_PROP1
fi

if [[ "$CPU_ABIS" == *"arm64-v8a"* || "$CPU_ABIS" == *"x86_64"* ]]; then
  ./bin/zygisk-ptrace64 monitor &
else
  # INFO: Device is 32-bit only

  ./bin/zygisk-ptrace32 monitor &
fi

exit 0
