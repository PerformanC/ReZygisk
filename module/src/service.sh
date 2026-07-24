#!/system/bin/sh

set -e

DEBUG=@DEBUG@

MODDIR=${0%/*}
if [ ! -f /data/adb/service.d/.rezygisk.sh ]; then
  mkdir -p /data/adb/service.d
  cp "$MODDIR/.rezygisk.sh" > /data/adb/service.d/.rezygisk.sh
  chmod +x /data/adb/service.d/.rezygisk.sh
fi

if [ "$ZYGISK_ENABLED" ]; then
  sed -i "s|^description=|description=[❌ Disable Magisk's built-in Zygisk] |" "$MODDIR/module.prop"

  exit 0
fi

cd "$MODDIR"

if [ "$(which magisk)" ]; then
  for file in ../*; do
    if [ -d "$file" ] && [ -d "$file/zygisk" ] && ! [ -f "$file/disable" ]; then
      if [ -f "$file/service.sh" ]; then
        cd "$file"
        log -p i -t "zygisk-sh" "Manually trigger service.sh for $file"
        sh "$(realpath ./service.sh)" &
        cd "$MODDIR"
      fi
    fi
  done
fi

exit 0
