import { exec } from './kernelsu.js'
import { setDark } from './themes/dark.js'
import { setThemeData, themeList } from './themes/main.js'
import { setLight } from './themes/light.js'

/* INFO: This code are meant to load the link with any card have credit-link attribute inside it */
document.addEventListener('click', async (event) => {
  const getLink = event.target.getAttribute('credit-link')
  if (!getLink || typeof getLink !== 'string') return;

  const ptrace64Cmd = await exec(`am start -a android.intent.action.VIEW -d https://${getLink}`).catch(() => {
    return window.open(`https://${getLink}`, "_blank", 'toolbar=0,location=0,menubar=0')
  })
  if (ptrace64Cmd.errno !== 0) return window.open(`https://${getLink}`, "_blank", 'toolbar=0,location=0,menubar=0')
}, false)

// INFO: Initial open logic
let sys_theme = localStorage.getItem('/ReZygisk/theme')
if (!sys_theme) sys_theme = setThemeData('dark')
themeList[sys_theme](true)

window.matchMedia('(prefers-color-scheme: dark)').addEventListener('change', event => {
  if (sys_theme !== "system") return
  const newColorScheme = event.matches ? "dark" : "light";

  switch (newColorScheme) {
    case 'dark':
      setDark()
      break
    case 'light':
      setLight()
      break
  }
});