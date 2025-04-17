import { smallPageDisabler } from '../smallPageDesabler.js'
const panel = document.getElementById('errorh_panel')

/* INFO: Event setup */
const navbar_data_tag = document.getElementById('cache-navbar-previous')
const small_panel_data_tag = document.getElementById('cache-page-small-previous')
const fallback_open = document.getElementById('cache-fallback-open')

document.getElementById('errorh_page_toggle').addEventListener('click', () => {
  const previous = !navbar_data_tag.getAttribute('content') ? setData('home', small_panel_data_tag) : navbar_data_tag.getAttribute('content')
  document.getElementById(`panel_${previous}`).classList.remove('show')
  document.getElementById('small_panel_errorh').classList.toggle('show')
  small_panel_data_tag.setAttribute('content', 'errorh')
})

document.getElementById('backport_errorh').addEventListener('click', () => {
  const previous = !navbar_data_tag.getAttribute('content') ? setData('home', small_panel_data_tag) : navbar_data_tag.getAttribute('content')
  document.getElementById(`panel_${previous}`).classList.remove('show')
  document.getElementById('loading_screen').style.display = 'none'
  document.getElementById('small_panel_errorh').classList.toggle('show')
  document.getElementById('errorh_panel').style.bottom = '1em'
  fallback_open.setAttribute('content', 'opened')
  small_panel_data_tag.setAttribute('content', 'errorh')
})

document.getElementById('sp_errorh_close').addEventListener('click', () => {
  const is_fallback = fallback_open.getAttribute('content')
  if (is_fallback) {
    document.getElementById('errorh_panel').style.bottom = '1em'
    document.getElementById('loading_screen').style.display = 'flex'
  }
  smallPageDisabler('errorh', is_fallback ? 'home' : 'settings', is_fallback ? 'home' : null)
})
document.getElementById('errorh_copy').addEventListener('click', () => {
  navigator.clipboard.writeText(panel.innerHTML)
})

document.getElementById('errorh_clear_all').addEventListener('click', () => {
  panel.innerHTML = ''
  localStorage.setItem('/system/error', '')
})

function setData(mode, tag) {
  tag.setAttribute('content', mode)

  return mode
}