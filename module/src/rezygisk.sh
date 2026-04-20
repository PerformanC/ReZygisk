#!/system/bin/sh

set -e

# INFO: This script gets moved to /data/adb/post-fs-data.d/rezygisk.sh

# INFO: This script is utilized so that when ReZygisk is disabled, it still can clean up its
#         module.prop, making it not have traces of its old status.

MODDIR=/data/adb/modules/rezygisk

# INFO: Resets ReZygisk's module.prop to its default state which is saved upon installation.
cp "$MODDIR/module.prop.bak" "$MODDIR/module.prop"

exit 0
