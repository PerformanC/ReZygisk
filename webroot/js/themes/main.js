import { setAmoled } from './amoled.js'
import { setDark } from './dark.js'
import { setLight } from './light.js'

// INFO: requirement variables
export const themeList = {
  amoled: () => setAmoled(true),
  dark: () => setDark(true),
  light: () => setLight(true),
  system: (unavaliable) => {
    const isDark = window.matchMedia && window.matchMedia('(prefers-color-scheme: dark)').matches
    if (isDark && unavaliable) setDark() 
    else setLight()
  },
}

export const setThemeData = (mode) => {
  localStorage.setItem('/ReZygisk/theme', mode)
  return mode
}