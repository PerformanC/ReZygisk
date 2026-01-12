import { setLightNav } from './lightNavbar.js'

const rootCss = document.querySelector(':root')


export function setLight(chooseSet) {
  rootCss.style.setProperty('--background', '#f2f2f2')
  rootCss.style.setProperty('--font', '#222222ff')
  rootCss.style.setProperty('--desc', '#535353ff')
  rootCss.style.setProperty('--dim', '#e0e0e0')
  rootCss.style.setProperty('--icon', '#acacac')
  rootCss.style.setProperty('--desktop-navbar', '#fefefe')
  rootCss.style.setProperty('--desktop-navicon', '#eeeeee')
  rootCss.style.setProperty('--button-enabled', '#eeeeee')
  rootCss.style.setProperty('--icon-bc', '#c9c9c9')
  rootCss.style.setProperty('--button', '#b3b3b3')

  if (chooseSet) setData('light')

  setLightNav()
}

function setData(mode) {
  localStorage.setItem('/ReZygisk/theme', mode)

  return mode
}
