#!/system/bin/sh

set -e

DEBUG=@DEBUG@

MODDIR=${0%/*}

if [ "$ZYGISK_ENABLED" ]; then
  # INFO: post-fs-data.sh exits before restoring module.prop in this case, so restore it
  #         here, otherwise the warning would be prepended once more on every boot.
  if [ -f "$MODDIR/module.prop.bak" ]; then
    cp "$MODDIR/module.prop.bak" "$MODDIR/module.prop"
  fi

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
