import { smallPageDisabler } from '../smallPageDesabler.js'
import { setDark } from '../themes/dark.js'
import { setLight } from '../themes/light.js'

const page_toggle = document.getElementById('theme_page_toggle')

let sys_theme = localStorage.getItem('/system/theme')
if (!sys_theme) sys_theme = setData('dark')

if (sys_theme == "system") {
  const isDark = window.matchMedia && window.matchMedia('(prefers-color-scheme: dark)').matches
  if (!isDark) setLight()
} else if (sys_theme === 'light') setLight(true)

document.getElementById('sp_theme_close').addEventListener('click', () => smallPageDisabler('theme', 'settings'))

page_toggle.addEventListener('click', () => {
  const previous = !localStorage.getItem('/cache/navbar/previous') ? setData('home') : localStorage.getItem('/cache/navbar/previous')
  document.getElementById(`panel_${previous}`).classList.remove('show')
  document.getElementById('small_panel_theme').classList.toggle('show')
  localStorage.setItem('/cache/page/small/previous', 'theme')
})

document.addEventListener('click', async (event) => {
  const getSystemTheme = event.target.getAttribute('theme-data')
  if (!getSystemTheme || typeof getSystemTheme !== 'string') return

  switch (getSystemTheme) {
    case 'dark': 
      setDark(true)
      break
    case 'light': 
      setLight(true)
      break
    case 'system':
      const isDark = window.matchMedia && window.matchMedia('(prefers-color-scheme: dark)').matches
      if (isDark) setDark() 
      else setLight()
      break
  }

  smallPageDisabler('theme', 'settings')

  sys_theme = setData(getSystemTheme)
}, false)

window.matchMedia('(prefers-color-scheme: dark)').addEventListener('change', event => {
  if (sys_theme !== "system") return
  const newColorScheme = event.matches ? "dark" : "light";

  switch (newColorScheme) {
    case 'dark':
      setDark()
      break
    case 'light':
      setLight()
      break
  }
});

function setData(mode) {
  localStorage.setItem('/system/theme', mode)

  return mode
}
