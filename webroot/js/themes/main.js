import { setAmoled } from './amoled.js'
import { setDark } from './dark.js'
import { setLight } from './light.js'
import { applyAccent } from './accent.js'

// INFO: requirement variables
export const themeList = {
  amoled: () => { setAmoled(true); applyAccent() },
  dark: () => { setDark(true); applyAccent() },
  light: () => { setLight(true); applyAccent() },
  system: (unavaliable) => {
    const isDark = window.matchMedia && window.matchMedia('(prefers-color-scheme: dark)').matches
    if (isDark && unavaliable) setDark()
    else setLight()

    applyAccent()
  },
}

export const setThemeData = (mode) => {
  localStorage.setItem('/ReZygisk/theme', mode)
  return mode
}
