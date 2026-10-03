/* INFO: Accent palettes. Each accent has a tone for dark and light surfaces so
           contrast stays readable in every theme. */
const rootCss = document.querySelector(':root')

export const accentList = {
  blue:   { dark: '#8ab4ff', light: '#2f5fd0' },
  teal:   { dark: '#5eead4', light: '#0f766e' },
  green:  { dark: '#86efac', light: '#15803d' },
  amber:  { dark: '#fcd34d', light: '#b45309' },
  coral:  { dark: '#fda4af', light: '#be123c' },
  purple: { dark: '#c4b5fd', light: '#6d28d9' },
  pink:   { dark: '#f9a8d4', light: '#be185d' },
  mono:   { dark: '#e5e5e5', light: '#262626' }
}

function hexToRgb(hex) {
  const value = hex.replace('#', '')

  return [ 0, 2, 4 ].map((i) => parseInt(value.slice(i, i + 2), 16))
}

export function getAccent() {
  const accent = localStorage.getItem('/ReZygisk/accent')

  return accentList[accent] ? accent : 'blue'
}

export function isLightTheme() {
  const theme = localStorage.getItem('/ReZygisk/theme')
  if (theme === 'light') return true
  if (theme === 'system') return !(window.matchMedia && window.matchMedia('(prefers-color-scheme: dark)').matches)

  return false
}

export function applyAccent(accent = getAccent()) {
  const palette = accentList[accent] || accentList.blue
  const light = isLightTheme()
  const hex = light ? palette.light : palette.dark
  const [ r, g, b ] = hexToRgb(hex)

  /* INFO: Pick readable text color on top of the accent (WCAG relative luminance). */
  const luminance = [ r, g, b ]
    .map((c) => c / 255)
    .map((c) => (c <= 0.03928 ? c / 12.92 : Math.pow((c + 0.055) / 1.055, 2.4)))
    .reduce((acc, c, i) => acc + c * [ 0.2126, 0.7152, 0.0722 ][i], 0)

  rootCss.style.setProperty('--accent', hex)
  rootCss.style.setProperty('--accent-rgb', `${r}, ${g}, ${b}`)
  rootCss.style.setProperty('--on-accent', luminance > 0.4 ? '#101418' : '#ffffff')

  /* INFO: Status colors need darker tones on light surfaces too. */
  if (light) {
    rootCss.style.setProperty('--ok', '#15803d')
    rootCss.style.setProperty('--ok-rgb', '21, 128, 61')
    rootCss.style.setProperty('--warn', '#b45309')
    rootCss.style.setProperty('--warn-rgb', '180, 83, 9')
    rootCss.style.setProperty('--err', '#c62828')
    rootCss.style.setProperty('--err-rgb', '198, 40, 40')
    rootCss.style.setProperty('--outline', 'rgba(0, 0, 0, 0.08)')
  } else {
    rootCss.style.setProperty('--ok', '#3ddc84')
    rootCss.style.setProperty('--ok-rgb', '61, 220, 132')
    rootCss.style.setProperty('--warn', '#ffb74d')
    rootCss.style.setProperty('--warn-rgb', '255, 183, 77')
    rootCss.style.setProperty('--err', '#ff6b6b')
    rootCss.style.setProperty('--err-rgb', '255, 107, 107')
    rootCss.style.setProperty('--outline', 'rgba(255, 255, 255, 0.07)')
  }

  return accent
}

export function setAccent(accent) {
  if (!accentList[accent]) return

  localStorage.setItem('/ReZygisk/accent', accent)
  applyAccent(accent)
}
