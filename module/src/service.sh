#!/system/bin/sh

DEBUG=@DEBUG@

MODDIR=${0%/*}

export TMP_PATH=/data/adb/rezygisk
if [ "$ZYGISK_ENABLED" ]; then
  cp "$MODDIR/module.prop" "$TMP_PATH/module.prop"
  sed -i "s|^description=|description=[❌ Disable Magisk's built-in Zygisk] |" "$TMP_PATH/module.prop"
  mount --bind "$TMP_PATH/module.prop" "$MODDIR/module.prop"

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
