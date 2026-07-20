#!/system/bin/sh

MODDIR=${0%/*}

if [ "$ZYGISK_ENABLED" ]; then
  exit 0
fi

cd "$MODDIR"

# INFO: KernelSU LKM late-load (ksud late-load) skips post-fs-data.sh.
#         Bootstrap ReZygisk when the ptrace monitor is not running yet.
monitor_running() {
  pidof zygisk-ptrace64 >/dev/null 2>&1 && return 0
  pidof zygisk-ptrace32 >/dev/null 2>&1 && return 0
  return 1
}

if ! monitor_running; then
  sh "$MODDIR/post-fs-data.sh"
  sleep 1
fi

# INFO: Zygote started before root was available; restart it so ReZygisk
#         injects into new app processes (soft reboot, not a full reboot).
killall zygote64 zygote 2>/dev/null

exit 0
