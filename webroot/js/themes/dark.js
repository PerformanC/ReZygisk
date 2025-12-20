import { setDarkNav } from './darkNavbar.js'

const rootCss = document.querySelector(':root')

export function setDark(chooseSet) {
  rootCss.style.setProperty('--background', '#141414')
  rootCss.style.setProperty('--font', '#ffffff')
  rootCss.style.setProperty('--desc', '#c9c9c9')
  rootCss.style.setProperty('--dim', '#1c1c1c')
  rootCss.style.setProperty('--icon', '#494949')
  rootCss.style.setProperty('--icon-bc', '#292929')
  rootCss.style.setProperty('--desktop-navbar', '#252525')
  rootCss.style.setProperty('--button-enabled', '#535353')
  rootCss.style.setProperty('--desktop-navicon', '#3a3a3a')
  rootCss.style.setProperty('--button', 'var(--background)')

  if (chooseSet) setData('dark')
  setDarkNav()
}

function setData(mode) {
  localStorage.setItem('/system/theme', mode)

  return mode
}