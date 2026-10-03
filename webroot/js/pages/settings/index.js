import { loadPage } from '../pageLoader.js'
import utils from '../utils.js'
import { fullScreen } from '../../kernelsu.js'
import { accentList, getAccent, isLightTheme, setAccent } from '../../themes/accent.js'
import { getRefreshInterval, haptic, setConfig } from '../../rz.js'

function _renderAccents() {
  const current = getAccent()

  document.getElementById('rz_accent_swatches').innerHTML = Object.keys(accentList).map((accent) => `
    <button class="nz_swatch" data-accent="${accent}" aria-label="${accent}" aria-pressed="${accent === current}"
      style="--swatch: ${isLightTheme() ? accentList[accent].light : accentList[accent].dark};"></button>
  `).join('')
}

function _renderRefreshInterval() {
  const current = String(getRefreshInterval())

  document.querySelectorAll('#rz_refresh_interval [data-interval]').forEach((button) => {
    button.setAttribute('aria-pressed', button.getAttribute('data-interval') === current ? 'true' : 'false')
  })
}

function _writeState(ConfigState) {
  return localStorage.setItem('/ReZygisk/webui_config', JSON.stringify(ConfigState))
}

export async function loadOnce() {

}

export async function loadOnceView() {

}

export async function onceViewAfterUpdate() {

}

export async function load() {
  let ConfigState = {
    disableFullscreen: false,
    enableSystemFont: false
  }

  let webui_config = localStorage.getItem('/ReZygisk/webui_config')

  if (!webui_config) {
    localStorage.setItem('/ReZygisk/webui_config', JSON.stringify(ConfigState))
  } else {
    ConfigState = JSON.parse(webui_config)
  }

  _renderAccents()
  utils.addListener(document.getElementById('rz_accent_swatches'), 'click', (event) => {
    const accent = event.target.closest('[data-accent]')?.getAttribute('data-accent')
    if (!accent) return

    haptic()
    setAccent(accent)
    _renderAccents()
  })

  _renderRefreshInterval()
  utils.addListener(document.getElementById('rz_refresh_interval'), 'click', (event) => {
    const interval = event.target.closest('[data-interval]')?.getAttribute('data-interval')
    if (interval === null || interval === undefined) return

    haptic()
    /* INFO: Re-read the config so we never clobber keys written by other switches. */
    ConfigState = { ...ConfigState, ...setConfig({ refreshInterval: Number(interval) }) }
    _renderRefreshInterval()
  })

  utils.addListener(document.getElementById('lang_page_toggle'), 'click', () => {
    loadPage('mini_settings_language')
  })

  utils.addListener(document.getElementById('theme_page_toggle'), 'click', () => {
    loadPage('mini_settings_theme')
  })

  const rz_webui_fullscreen_switch = document.getElementById('rz_webui_fullscreen_switch')
  if (ConfigState.disableFullscreen) rz_webui_fullscreen_switch.checked = true

  utils.addListener(rz_webui_fullscreen_switch, 'click', () => {
    /* INFO: This is swapped, as it meant to disable the fullscreen */
    ConfigState.disableFullscreen = !ConfigState.disableFullscreen
    _writeState(ConfigState)

    fullScreen(!ConfigState.disableFullscreen)
  })

  const rz_webui_font_switch = document.getElementById('rz_webui_font_switch')
  if (ConfigState.enableSystemFont) rz_webui_font_switch.checked = true

  utils.addListener(rz_webui_font_switch, 'click', () => {
    /* INFO: This is swapped, as it meant to enable the system font */
    ConfigState.enableSystemFont = !ConfigState.enableSystemFont

    if (ConfigState.enableSystemFont) {
      const headTag = document.getElementsByTagName('head')[0]
      const styleTag = document.createElement('style')

      styleTag.id = 'font-tag'
      headTag.appendChild(styleTag)
      styleTag.innerHTML = `
        :root {
          --font-family: system-ui, -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Oxygen, Ubuntu, Cantarell, 'Open Sans', 'Helvetica Neue', sans-serif
        }`
    } else {
      const fontTag = document.getElementById('font-tag')
      if (fontTag) fontTag.remove()
    }

    _writeState(ConfigState)
  })
}
