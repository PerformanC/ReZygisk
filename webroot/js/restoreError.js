const errorh_panel = document.getElementById('errorh_panel')
let sys_error = localStorage.getItem('/system/error')

if (!sys_error) {
  localStorage.setItem('/system/error', '')

  sys_error = localStorage.getItem('/system/error')
}

if (sys_error.length !== 0) errorh_panel.innerHTML = sys_error