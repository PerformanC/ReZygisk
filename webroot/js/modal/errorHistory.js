const button = document.getElementById('rezygisk_errorh')
const backport = document.getElementById('backport_errorh')
const copy_button = document.getElementById('errorh_copy')
const target = document.getElementById('errorh_modal')
const close_button = document.getElementById('errorh_modal_close')
const clearall_button = document.getElementById('errorh_clear_all')
const panel = document.getElementById('errorh_panel')
const error_history_data_state = document.getElementById('cache-modal-error-history-state')

let sys_module_mode = error_history_data_state.getAttribute('content')
sys_module_mode = setData('closed', error_history_data_state)

button.addEventListener('click', () => {
  target.style.top = '0%'

  sys_module_mode = setData('opened', error_history_data_state)
})

backport.addEventListener('click', () => {
  if (sys_module_mode == 'opened') {
    target.style.top = '100%'
    sys_module_mode = setData('closed', error_history_data_state)
    return
  }

  target.style.top = '0%'
  sys_module_mode = setData('opened', error_history_data_state)
})


close_button.addEventListener('click', () => {
  target.style.top = '100%'

  sys_module_mode = setData('closed', error_history_data_state)
})

copy_button.addEventListener('click', () => {
  navigator.clipboard.writeText(panel.innerHTML)
})

clearall_button.addEventListener('click', () => {
  panel.innerHTML = ''
  localStorage.setItem('/system/error', '')
})

function setData(mode, tag) {
  tag.setAttribute('content', mode)

  return mode
}