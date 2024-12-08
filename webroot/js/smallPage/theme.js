import { smallPageDisabler } from '../smallPageDesabler.js'
import { setAmoled } from '../themes/amoled.js'
import { setDark } from '../themes/dark.js'
import { setLight } from '../themes/light.js'
import { setMonochrome } from '../themes/monochrome.js'

// INFO: requirement variables
let sys_theme
const page_toggle = document.getElementById('theme_page_toggle')
const themeList = {
  amoled: () => setAmoled(true),
  dark: () => setDark(true),
  light: () => setLight(true),
  monochrome: () => setMonochrome(true),
  system: (unavaliable) => {
    const isDark = window.matchMedia && window.matchMedia('(prefers-color-scheme: dark)').matches
    if (isDark && unavaliable) setDark() 
    else setLight()
  },
}
const setData = (mode) => {
  localStorage.setItem('/system/theme', mode)
  return mode
}

// INFO: Initial open logic
sys_theme = localStorage.getItem('/system/theme')
if (!sys_theme) sys_theme = setData('dark')
themeList[sys_theme](true)

// INFO: Event logic
const navbar_data_tag = document.getElementById('cache-navbar-previous')
const small_panel_data_tag = document.getElementById('cache-page-small-previous')

document.getElementById('sp_theme_close').addEventListener('click', () => smallPageDisabler('theme', 'settings'))

document.addEventListener('click', async (event) => {
  const themeListKey = Object.keys(themeList)
  const getThemeMode = event.target.getAttribute('theme-data')

  if (!getThemeMode || typeof getThemeMode !== 'string' || !themeListKey.includes(getThemeMode)) return

  themeList[getThemeMode](true)

  smallPageDisabler('theme', 'settings')

  sys_theme = setData(getThemeMode)
}, false)

page_toggle.addEventListener('click', () => {
  const previous = !navbar_data_tag.getAttribute('content') ? setTagData('home', small_panel_data_tag) : navbar_data_tag.getAttribute('content')
  document.getElementById(`panel_${previous}`).classList.remove('show')
  document.getElementById('small_panel_theme').classList.toggle('show')
    small_panel_data_tag.setAttribute('content', 'theme')
})

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

function setTagData(data, tag) {
  tag.setAttribute('content', data)

  return data
}
