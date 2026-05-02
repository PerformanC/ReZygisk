import { exec, toast } from '../../../../kernelsu.js'

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

  const langList = langListCmd.stdout.split('\n')
  if (langList.length === 0) {
    toast('No languages found!')

    return;
  }

  availableLanguages = langList
}

export async function loadOnceView() {
  const lang_list_buf = []
  for (let i = 0; i < availableLanguages.length; i++) {
    const langCode = availableLanguages[i]
    const langData = await _getLanguageData(langCode)

    lang_list_buf.push(`
      <div lang-data="${langCode}" class="dim card card_animation" style="padding: 20px 15px; cursor: pointer;">
        <div lang-data="${langCode}" class="dimc" style="font-size: 1.1em;">${langData.langName}</div>
      </div>
    `)
  }

  document.getElementById('lang_list').innerHTML = lang_list_buf.join('')
}

export async function onceViewAfterUpdate() {

}

export async function load() {
  // _setNewThemeIcon()

  // const sp_lang_close = document.getElementById('sp_lang_close')

  // sp_lang_close.addEventListener('click', async function langCloseButtonListener() {
  //   sp_lang_close.removeEventListener('click', langCloseButtonListener)
  //   loadPage('settings')
  // })

  document.addEventListener('click', async function langButtonListener(event) {
    const getLangLocate = event.target.getAttribute('lang-data')
    const main_html = document.getElementById('main_html')
    if (!getLangLocate || typeof getLangLocate !== 'string') return

    document.removeEventListener('click', langButtonListener)

    /* INFO: Strip .json from the end of the filename */
    setLanguage(getLangLocate.replace('.json', ''))

    if (getLangLocate.includes('ar_')) main_html.setAttribute('dir', 'rtl')
    else main_html.setAttribute('dir', 'ltr')

    loadPage('settings')

    reloadPage()
  }, false)
}
