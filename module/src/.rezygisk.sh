if [ -f /data/adb/modules/rezygisk/disable ]; then
  cat /data/adb/modules/rezygisk/module.prop.orig > /data/adb/modules/rezygisk/module.prop
  rm /data/adb/service.d/.rezygisk.sh
fi
