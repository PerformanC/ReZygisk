#!/system/bin/sh

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

if [ -d $TMP_PATH ]; then
  rm -rf $TMP_PATH
fi

create_sys_perm $TMP_PATH

if [ -f $MODDIR/lib64/libzygisk.so ];then
  create_sys_perm $TMP_PATH/lib64
  cp $MODDIR/lib64/libzygisk.so $TMP_PATH/lib64/libzygisk.so
  chcon u:object_r:system_file:s0 $TMP_PATH/lib64/libzygisk.so
fi

if [ -f $MODDIR/lib/libzygisk.so ];then
  create_sys_perm $TMP_PATH/lib
  cp $MODDIR/lib/libzygisk.so $TMP_PATH/lib/libzygisk.so
  chcon u:object_r:system_file:s0 $TMP_PATH/lib/libzygisk.so
fi

CPU_ABIS=$(getprop ro.system.product.cpu.abilist)
CPU_ABIS=${CPU_ABIS:-$(getprop ro.product.cpu.abilist)}

if [[ "$CPU_ABIS" == *"arm64-v8a"* || "$CPU_ABIS" == *"x86_64"* ]]; then
  (sleep 0.8; ./bin/zygisk-ptrace64 monitor) &
else
  # INFO: Device is 32-bit only
  
  (sleep 0.8; ./bin/zygisk-ptrace32 monitor) &
fi
