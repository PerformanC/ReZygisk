#!/system/bin/sh

set -e

export TMP_PATH=/data/adb/rezygisk
rm -rf "$TMP_PATH"

# INFO: Persistent NextZygisk settings
rm -rf /data/adb/nextzygisk

rm -f /data/adb/service.d/rezygisk.sh

# INFO: Only removes if dir is empty
rmdir /data/adb/service.d 2>/dev/null || true

exit 0
