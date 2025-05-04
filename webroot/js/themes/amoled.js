import { setDarkNav } from './darkNavbar.js'

const rootCss = document.querySelector(':root')

/* INFO: Changes the icons to match the theme */
const close_icons = document.getElementsByClassName('close_icon')
const expand_icons = document.getElementsByClassName('expander')
const sp_lang_close = document.getElementById('sp_lang_close')
const sp_theme_close = document.getElementById('sp_theme_close')

export function setAmoled(chooseSet) {
  rootCss.style.setProperty('--background', '#000000')
  rootCss.style.setProperty('--font', '#d9d9d9')
  rootCss.style.setProperty('--desc', '#a9a9a9')
  rootCss.style.setProperty('--dim', '#0b0d0f')
  rootCss.style.setProperty('--icon', '#22292d')
  rootCss.style.setProperty('--icon-bc', '#171b1d')
  rootCss.style.setProperty('--desktop-navbar', '#111417')
  rootCss.style.setProperty('--desktop-navicon', '#1c2225')
  rootCss.style.setProperty('--button', 'var(--background)')

  if (chooseSet) setData('amoled')

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