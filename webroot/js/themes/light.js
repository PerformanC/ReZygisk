import { 
  light_close_icon,
  light_expand_icon,
  light_page_exit_icon,
} from './lightIcon.js'
import { setLightNav } from './lightNavbar.js'

const rootCss = document.querySelector(':root')

/* INFO: Changes the icons to match the theme */
const close_icons = document.getElementsByClassName('close_icon')
const expand_icons = document.getElementsByClassName('expander')
const sp_lang_close = document.getElementById('sp_lang_close')
const sp_theme_close = document.getElementById('sp_theme_close')
const sp_errorh_close = document.getElementById('sp_errorh_close')

export function setLight(chooseSet) {
  rootCss.style.setProperty('--background', '#f2f2f2')
  rootCss.style.setProperty('--font', '#181c20')
  rootCss.style.setProperty('--desc', '#484d53')
  rootCss.style.setProperty('--dim', '#e0e0e0')
  rootCss.style.setProperty('--icon', '#acacac')
  rootCss.style.setProperty('--desktop-navbar', '#fefefe')
  rootCss.style.setProperty('--desktop-navicon', '#eeeeee')
  rootCss.style.setProperty('--icon-bc', '#c9c9c9')
  rootCss.style.setProperty('--button', '#b3b3b3')

  if (chooseSet) setData('light')

  for (const close_icon of close_icons) {
    close_icon.innerHTML = light_close_icon
  }

  for (const expand_icon of expand_icons) {
    expand_icon.innerHTML = light_expand_icon
  }

  sp_lang_close.innerHTML = light_page_exit_icon
  sp_theme_close.innerHTML = light_page_exit_icon
  sp_errorh_close.innerHTML = light_page_exit_icon
  setLightNav()
}

function setData(mode) {
  localStorage.setItem('/system/theme', mode)

  return mode
}
