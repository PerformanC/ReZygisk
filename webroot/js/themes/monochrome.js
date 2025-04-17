import { setDarkNav } from './darkNavbar.js'

const rootCss = document.querySelector(':root')

/* INFO: Changes the icons to match the theme */
const close_icons = document.getElementsByClassName('close_icon')
const expand_icons = document.getElementsByClassName('expander')
const sp_lang_close = document.getElementById('sp_lang_close')
const sp_theme_close = document.getElementById('sp_theme_close')

export function setMonochrome(chooseSet) {
  rootCss.style.setProperty('--background', '#141414')
  rootCss.style.setProperty('--font', '#ffffff')
  rootCss.style.setProperty('--desc', '#c9c9c9')
  rootCss.style.setProperty('--dim', '#1c1c1c')
  rootCss.style.setProperty('--icon', '#494949')
  rootCss.style.setProperty('--icon-bc', '#292929')
  rootCss.style.setProperty('--desktop-navbar', '#252525')
  rootCss.style.setProperty('--desktop-navicon', '#3a3a3a')
  rootCss.style.setProperty('--button', 'var(--background)')

  if (chooseSet) setData('monochrome')

  for (const close_icon of close_icons) {
    close_icon.innerHTML = '<img src="assets/close.svg">'
  }

  for (const expand_icon of expand_icons) {
    expand_icon.innerHTML = '<img class="dimc" src="assets/expand.svg">'
  }

  sp_lang_close.innerHTML = '<img src="./assets/back.svg"/>'
  sp_theme_close.innerHTML = '<img src="./assets/back.svg"/>'
  setDarkNav()
}

function setData(mode) {
  localStorage.setItem('/system/theme', mode)

  return mode
}