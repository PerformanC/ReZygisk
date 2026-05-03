import { setThemeData, themeList } from '../../../../themes/main.js'
import { loadPage } from '../../../pageLoader.js'

export async function loadOnce() {
}

export async function loadOnceView() {
}

export async function onceViewAfterUpdate() {
}

export async function load() {
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