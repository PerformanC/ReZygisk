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

/* INFO: The manager's --window-inset-* values describe the system bars for a
           WebView drawn edge to edge. Some managers keep those values after
           leaving fullscreen, while the WebView already ends above the bars,
           which doubled the top and bottom spacing until the setting was
           toggled. Only use the insets while the page really covers the whole
           screen height; otherwise the bars are outside the page. */
function syncInsets() {
  const root = document.documentElement
  const cs = getComputedStyle(root)
  const edgeToEdge = window.innerHeight >= screen.height - 2

  const inset = (name) => (edgeToEdge && cs.getPropertyValue(name).trim()) || '0px'

  root.style.setProperty('--rz-inset-top', inset('--window-inset-top'))
  root.style.setProperty('--rz-inset-bottom', inset('--window-inset-bottom'))
}

/* INFO: The manager may update its inset values slightly after resizing. */
function syncInsetsSoon() {
  syncInsets()
  requestAnimationFrame(syncInsets)
  setTimeout(syncInsets, 300)
}

window.addEventListener('resize', syncInsetsSoon)
syncInsets()

/* INFO: Always apply the saved choice, so that "Disable fullscreen" also
           works on managers that open WebUIs in fullscreen by default. */
fullScreen(!ConfigState.disableFullscreen)
syncInsetsSoon()

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
