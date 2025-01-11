import { exec } from './kernelsu.js'

const monitor_start = document.getElementById('monitor_start_button')
const monitor_stop = document.getElementById('monitor_stop_button')
const monitor_pause = document.getElementById('monitor_pause_button')

if (monitor_start) {
  monitor_start.addEventListener('click', () => {
    exec('/data/adb/modules/zygisksu/bin/zygisk-ptrace64 ctl start')
  })

  monitor_stop.addEventListener('click', () => {
    exec('/data/adb/modules/zygisksu/bin/zygisk-ptrace64 ctl exit')
  })

  monitor_pause.addEventListener('click', () => {
    exec('/data/adb/modules/zygisksu/bin/zygisk-ptrace64 ctl stop')
  })
}
