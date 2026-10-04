import { exec, toast } from '../../../../kernelsu.js'
import { escapeHTML } from '../../../../rz.js'
import { icon } from '../../../../icons.js'

import { loadPage, setLanguage, reloadPage } from '../../../pageLoader.js'

let availableLanguages = [ -1 /* INFO: To tell we haven't checked yet */ ]

// async function _setNewThemeIcon() {
//   const back_icon = document.getElementById('sp_lang_close')
//   const sys_theme = localStorage.getItem('/ReZygisk/theme')
//   if (!sys_theme) return;
//   if (sys_theme == 'light') {
//     back_icon.classList.add('light_icon_mode')
//   }
//   if (back_icon.classList.contains('light_icon_mode')) {
//     back_icon.classList.remove('light_icon_mode')
//   }
// }

async function _getLanguageData(lang_file) {
  return fetch(`lang/${lang_file}`)
    .then((response) => response.json())
    .then((data) => {
      return data
    })
    .catch(() => false)
}

export async function loadOnce() {
  const langListCmd = await exec('/system/bin/ls /data/adb/modules/rezygisk/webroot/lang')
  if (langListCmd.errno !== 0) {
    toast('Error getting language list!')

    return;
  }

  const langList = langListCmd.stdout.split('\n').map((lang) => lang.trim()).filter((lang) => /^[a-zA-Z_-]+\.json$/.test(lang))
  if (langList.length === 0) {
    toast('No languages found!')

    return;
  }

  availableLanguages = langList
}

function currentLanguage() {
  return localStorage.getItem('/TreatWheel/language') || 'en_US'
}

function markCurrent() {
  const current = currentLanguage()

  document.querySelectorAll('#lang_list [lang-data]').forEach((option) => {
    if (!option.classList.contains('nz_option')) return

    option.setAttribute('aria-checked', option.getAttribute('lang-data').replace('.json', '') === current ? 'true' : 'false')
  })
}

export async function loadOnceView() {
  const languages = await Promise.all(availableLanguages.map(async (langCode) => [ langCode, await _getLanguageData(langCode) ]))

  const lang_list_buf = languages
    .filter(([ , langData ]) => langData && typeof langData.langName === 'string')
    .map(([ langCode, langData ], i) => {
      const code = langCode.replace('.json', '')

      return `
        <div lang-data="${escapeHTML(langCode)}" class="nz_option" role="radio" aria-checked="false" style="animation-delay: ${Math.min(i, 12) * 25}ms;">
          <div class="nz_lang_badge">${escapeHTML(code.split('_')[0].toUpperCase())}</div>
          <div class="nz_option_body">
            <div class="nz_option_title">${escapeHTML(langData.langName)}</div>
            <div class="nz_option_code"><bdi>${escapeHTML(code)}</bdi></div>
          </div>
          <div class="nz_radio">${icon('done')}</div>
        </div>
      `
    })

  document.getElementById('lang_list').innerHTML = `<div class="nz" style="padding-top: 4px;">${lang_list_buf.join('')}</div>`

  /* INFO: The page loader does not await this function, so load() may run before
             the list exists. Mark the current language here too. */
  markCurrent()
}

export async function onceViewAfterUpdate() {

}

export async function load() {
  markCurrent()

  document.addEventListener('click', async function langButtonListener(event) {
    const option = event.target.closest('[lang-data]')
    const getLangLocate = option?.getAttribute('lang-data')
    const main_html = document.getElementById('main_html')
    if (!getLangLocate || typeof getLangLocate !== 'string') return

    document.removeEventListener('click', langButtonListener)

    /* INFO: Strip .json from the end of the filename */
    setLanguage(getLangLocate.replace('.json', ''))
    markCurrent()

    main_html.setAttribute('dir', /^(ar|fa|he|ur)_/.test(getLangLocate) ? 'rtl' : 'ltr')
    main_html.setAttribute('lang', getLangLocate.replace('.json', '').replace('_', '-'))

    loadPage('settings')

    reloadPage()
  }, false)
}
