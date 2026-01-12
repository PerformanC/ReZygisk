import { setDarkNav } from './darkNavbar.js'

const rootCss = document.querySelector(':root')

export function setAmoled(chooseSet) {
  rootCss.style.setProperty('--background', '#000000')
  rootCss.style.setProperty('--font', '#d9d9d9')
  rootCss.style.setProperty('--desc', '#a9a9a9')
  rootCss.style.setProperty('--dim', '#0e0e0eff')
  rootCss.style.setProperty('--icon', '#292929ff')
  rootCss.style.setProperty('--icon-bc', '#202020ff')
  rootCss.style.setProperty('--desktop-navbar', '#161616ff')
  rootCss.style.setProperty('--desktop-navicon', '#242424ff')
  rootCss.style.setProperty('--button', 'var(--background)')

  if (chooseSet) setData('amoled')
  setDarkNav()
}

function setData(mode) {
  localStorage.setItem('/ReZygisk/theme', mode)

  return mode
}