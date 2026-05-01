import { exec, fullScreen } from './kernelsu.js'
import { setDark } from './themes/dark.js'
import { setThemeData, themeList } from './themes/main.js'
import { setLight } from './themes/light.js'
import { loadPage } from './pages/pageLoader.js'

/* INFO: This sets the default theme to system if not set */
let sys_theme = localStorage.getItem('/ReZygisk/theme')
if (!sys_theme) sys_theme = setThemeData('system')
themeList[sys_theme](true)

const ConfigState = JSON.parse(localStorage.getItem('/ReZygisk/webui_config') || '{}')

if (!ConfigState.disableFullscreen) fullScreen(true)

if (ConfigState.enableSystemFont) {
  const headTag = document.getElementsByTagName('head')[0]
  const styleTag = document.createElement('style')
  styleTag.id = 'font-tag'
  headTag.appendChild(styleTag)
  styleTag.innerHTML = `
    :root {
      --font-family: system-ui, -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Oxygen, Ubuntu, Cantarell, 'Open Sans', 'Helvetica Neue', sans-serif
    }`
}

/* INFO: This code are meant to load the link with any card have credit-link attribute inside it */
document.addEventListener('click', async (event) => {
  const getLink = event.target.getAttribute('credit-link')
  if (!getLink || typeof getLink !== 'string') return;

  const ptrace64Cmd = await exec(`am start -a android.intent.action.VIEW -d https://${getLink}`).catch(() => {
    return window.open(`https://${getLink}`, "_blank", 'toolbar=0,location=0,menubar=0')
  })

  if (ptrace64Cmd.errno !== 0) return window.open(`https://${getLink}`, "_blank", 'toolbar=0,location=0,menubar=0')
}, false)

window.matchMedia('(prefers-color-scheme: dark)').addEventListener('change', (event) => {
  if (sys_theme !== 'system') return

  const newColorScheme = event.matches ? 'dark' : 'light'
  if (newColorScheme === 'dark') setDark()
  else if (newColorScheme === 'light') setLight()
})

/*
 * INFO: This back gesture system is based in hijacking
 * webview history so it may not usable in TW generic
 * webui framework.
 * 
 * This system is supposed to work something like
 * directory in file system. This is the reason why we
 * have to hijack the webview history system because it
 * only works differently (stack state by state), not like
 * what we're expecting and somehow we still need to take
 * advantage of back gesture in webview .
 * 
 * For example: home -> module -> mini_settings_language.
 * 
 * In this example, assume home is level 0, module is
 * level 1 and mini_settings_language is level 2.
 * 
 * If user in mini_settings_language and use back function
 * they can only got back one by one, like from level 2 to
 * level 1 to level 0.
 * 
 * Now this code is very experimental and need more testing.
 */
const pageHistory = []
const pageLevel = [
  ['home'],
  ['modules', 'actions', 'settings'],
  ['mini_settings_language', 'mini_settings_theme']
]

/* 
 * INFO: This function will check the level of the page.
 * It will based in the index number of the element in
 * pageLevel array and return the index number of which
 * elements contain that pageId requested.
 */
function pageLevelFinder(pageId) {
  for (let i = 0; i < pageLevel.length; i++) {
    const level = pageLevel[i]
    if (level.includes(pageId)) return i
  }
}

/*
 * INFO: This is pagechanged event, hope this will be
 * in generic TW webui framework WebUI. This event will
 * run when page changed, we will use it to check
 * whenever changed to another page id.
 */
window.addEventListener('pagechanged', function (e) {
  const newPageLevel = pageLevelFinder(e.detail.pageId)

  if (!pageHistory[newPageLevel]) {
    pageHistory.push(e.detail.pageId)
    history.pushState({ pageLevel: newPageLevel }, '', location.pathname)
  }

  if (pageHistory[newPageLevel] !== e.detail.pageId) {
    pageHistory[newPageLevel] = e.detail.pageId
    history.replaceState({ pageLevel: newPageLevel }, '', location.pathname)
  }

  if ((pageHistory.length - 1) - newPageLevel === 1)
    pageHistory.splice(-1, 1)

  if (e.detail.pageId == 'home') history.go(-1)
})

window.addEventListener('popstate', async () => {
  const oldPageId = pageHistory[pageHistory.length - 2]
  if (!oldPageId) return;
  await loadPage(oldPageId)
})