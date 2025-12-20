import { setThemeData, themeList } from '../../../../themes/main.js'
import { loadPage } from '../../../pageLoader.js'

async function _setNewThemeIcon() {
  const back_icon = document.getElementById('sp_theme_close')
  const sys_theme = localStorage.getItem('/system/theme')
  if (!sys_theme) return;
  if (sys_theme == "light") {
    back_icon.classList.add('light_icon_mode')
  } else if (back_icon.classList.contains('light_icon_mode')) {
    back_icon.classList.remove('light_icon_mode')
  }
}

export async function loadOnce() {

}

export async function loadOnceView() {

}

export async function onceViewAfterUpdate() {

}

export async function load() {
  _setNewThemeIcon()
  const sp_theme_close = document.getElementById('sp_theme_close')

  sp_theme_close.addEventListener('click', async function themeCloseButtonListener() {
    sp_theme_close.removeEventListener('click', themeCloseButtonListener)
    loadPage('settings')
  })

  document.addEventListener('click', async function themeButtonListener(event) {
    const themeListKey = Object.keys(themeList)
    const getThemeMode = event.target.getAttribute('theme-data')
  
    if (!getThemeMode || typeof getThemeMode !== 'string' || !themeListKey.includes(getThemeMode)) return
  
    document.removeEventListener('click', themeButtonListener)
  
    themeList[getThemeMode](true)
  
    setThemeData(getThemeMode)
    loadPage('settings')
  }, false)
}