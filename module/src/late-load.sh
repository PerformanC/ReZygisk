#!/system/bin/sh

MODDIR=${0%/*}

if [ "$ZYGISK_ENABLED" ]; then
  exit 0
fi

cd "$MODDIR"

monitor_running() {
  pidof zygisk-ptrace64 >/dev/null 2>&1 && return 0
  pidof zygisk-ptrace32 >/dev/null 2>&1 && return 0
  return 1
}

if ! monitor_running; then
  sh "$MODDIR/post-fs-data.sh"
fi

exit 0
