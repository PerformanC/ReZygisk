import { setThemeData, themeList } from '../../../../themes/main.js'
import { getStrings } from '../../../pageLoader.js'
import utils from '../../../utils.js'
import { icon } from '../../../../icons.js'
import { escapeHTML, haptic } from '../../../../rz.js'

/* INFO: Colors mirror js/themes/<theme>.js, used only for the miniature previews. */
const THEMES = [
  { id: 'system', key: 'system_based', split: true },
  { id: 'dark', key: 'dark', bg: '#141414', surface: '#262626' },
  { id: 'amoled', key: 'amoled', bg: '#000000', surface: '#161616' },
  { id: 'light', key: 'light', bg: '#f2f2f2', surface: '#d6d6d6' }
]

function currentTheme() {
  const theme = localStorage.getItem('/ReZygisk/theme')

  return themeList[theme] ? theme : 'system'
}

function render(strings) {
  const container = document.getElementById('theme_options')
  if (!container) return

  const current = currentTheme()

  container.innerHTML = THEMES.map((theme, i) => `
    <div class="nz_option" role="radio" data-theme="${theme.id}" aria-checked="${theme.id === current}" style="animation-delay: ${i * 40}ms;">
      <div class="nz_theme_preview ${theme.split ? 'nz_theme_preview_split' : ''}" style="${theme.split ? '' : `--p-bg: ${theme.bg}; --p-surface: ${theme.surface};`}"><i></i><i></i><i></i></div>
      <div class="nz_option_body">
        <div class="nz_option_title">${escapeHTML(strings.theme[theme.key])}</div>
        <div class="nz_option_desc">${escapeHTML(strings.description[theme.key])}</div>
      </div>
      <div class="nz_radio">${icon('done')}</div>
    </div>
  `).join('')
}

export async function loadOnce() {
}

export async function loadOnceView() {
}

export async function onceViewAfterUpdate() {
}

export async function load() {
  const strings = await getStrings('mini_settings_theme')
  render(strings)

  const container = document.getElementById('theme_options')
  utils.removeAllListeners(container)

  /* INFO: Applies instantly and stays open, so the change can be seen right away. */
  utils.addListener(container, 'click', (event) => {
    const theme = event.target.closest('[data-theme]')?.getAttribute('data-theme')
    if (!theme || !themeList[theme] || theme === currentTheme()) return

    haptic()

    /* INFO: Store first so the accent picks the tone matching the new theme. */
    setThemeData(theme)
    themeList[theme](true)

    container.querySelectorAll('[data-theme]').forEach((option) => {
      option.setAttribute('aria-checked', option.getAttribute('data-theme') === theme ? 'true' : 'false')
    })
  })
}
