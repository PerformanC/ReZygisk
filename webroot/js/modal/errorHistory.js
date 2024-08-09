const button = document.getElementById('rezygisk_errorh')
const backport = document.getElementById('backport_errorh')
const copy_button = document.getElementById('errorh_copy')
const target = document.getElementById('errorh_modal')
const close_button = document.getElementById('errorh_modal_close')
const clearall_button = document.getElementById('errorh_clear_all')
const panel = document.getElementById('errorh_panel')

let sys_module_mode = localStorage.getItem('/cache/modal/error_history/state')
sys_module_mode = setData('closed')

button.addEventListener('click', () => {
  target.style.top = '0%'

  sys_module_mode = setData('opened')
})

backport.addEventListener('click', () => {
  if (sys_module_mode == 'opened') {
    target.style.top = '100%'
    sys_module_mode = setData('closed')
    return
  }

  target.style.top = '0%'
  sys_module_mode = setData('opened')
})


close_button.addEventListener('click', () => {
  target.style.top = '100%'

  sys_module_mode = setData('closed')
})

copy_button.addEventListener('click', () => {
  navigator.clipboard.writeText(panel.innerHTML)
})

clearall_button.addEventListener('click', () => {
  panel.innerHTML = ''
  localStorage.setItem('/system/error', '')
})

function setData(mode) {
  localStorage.setItem('/cache/modal/error_history/state', mode)

  return mode
}