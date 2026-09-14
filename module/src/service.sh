#!/system/bin/sh

set -e

DEBUG=@DEBUG@

MODDIR=${0%/*}

SERVICE_DIR=/data/adb/service.d
SERVICE_SCRIPT="$SERVICE_DIR/.rezygisk.sh"
if [ ! -f "$SERVICE_SCRIPT" ]; then
  if ! mkdir -p "$SERVICE_DIR" \
      || ! cat "$MODDIR/.rezygisk.sh" > "$SERVICE_SCRIPT" \
      || ! chmod +x "$SERVICE_SCRIPT"; then
    exit 1
  fi
elif ! chmod +x "$SERVICE_SCRIPT"; then
  exit 1
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

        # INFO: Don't propagate errexit
        set +e

        sh "$(realpath ./service.sh)" &

        # INFO: Re-enable errexit
        set -e

        cd "$MODDIR"
      fi
    fi
  done
fi

exit 0
