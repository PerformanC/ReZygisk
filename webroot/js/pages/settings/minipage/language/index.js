import { loadPage, setLanguage, reloadPage } from '../../../pageLoader.js'

const availableLanguages = [
  'ar_EG', // @ZG089
  'de_DE', // @Blazzycrafer
  'en_US', // @PerformanC
  'es_AR', // @Flopster101
  'es_ES', // @LuchoModzzz
  'es_MX', // @LuchoModzzz / @Berserkr2k
  'fr_FR', // @GhostFRR
  'id_ID', // @bpanca05 / @LuckyKiddos
  'it_IT', // @thasave14
  'ja_JP', // @Fyphen1223 / @reindex-ot
  'ms_MS', // @sheeplag (https://crowdin.com/profile/sheeplag)
  'nl_NL', // @DragoWing (https://crowdin.com/profile/DragoWing)
  'pt_BR', // @ThePedroo / @CharaDreemurrDT
  'ru_RU', // @Emulond / @AJleKcAHgP68
  'tr_TR', // @witchfuneral
  'uk_UA', // @Kittyskj
  'vi_VN', // @RainyXeon
  'zh_CN'  // @Meltartica / @SheepChef
]

async function _setNewThemeIcon() {
  const back_icon = document.getElementById('sp_lang_close')
  const sys_theme = localStorage.getItem('/ReZygisk/theme')
  if (!sys_theme) return;
  if (sys_theme == "light") {
    back_icon.classList.add('light_icon_mode')
  }
  if (back_icon.classList.contains('light_icon_mode')) {
    back_icon.classList.remove('light_icon_mode')
  }
}

async function _getLanguageData(lang_name) {
  return fetch(`lang/${lang_name}.json`)
    .then((response) => response.json())
    .then((data) => {
      return data
    })
    .catch(() => false)
}

export async function loadOnce() {

}

export async function loadOnceView() {

}

export async function onceViewAfterUpdate() {

}

export async function load() {
  _setNewThemeIcon()
  const lang_list = document.getElementById('lang_list')
  // INFO: Language list must be empty before load
  lang_list.innerHTML = ''
  for (let i = 0; i < availableLanguages.length; i++) {
    const langCode = availableLanguages[i]
    const langData = await _getLanguageData(langCode)

    lang_list.innerHTML += `
      <div lang-data="${langCode}" class="dim card card_animation" style="padding: 25px 15px; cursor: pointer;">
        <div lang-data="${langCode}" class="dimc" style="font-size: 1.1em;">${langData.langName}</div>
      </div>
      `
  }

  const sp_lang_close = document.getElementById('sp_lang_close')

  sp_lang_close.addEventListener('click', async function langCloseButtonListener() {
    sp_lang_close.removeEventListener('click', langCloseButtonListener)
    loadPage('settings')
  })

  document.addEventListener('click', async function langButtonListener(event) {
    const getLangLocate = event.target.getAttribute('lang-data')
    const main_html = document.getElementById('main_html')
    if (!getLangLocate || typeof getLangLocate !== 'string') return

    document.removeEventListener('click', langButtonListener)

    setLanguage(getLangLocate)

    if (getLangLocate.includes('ar_')) main_html.setAttribute("dir", "rtl")
    else main_html.setAttribute("dir", "ltr")

    loadPage('settings')

    reloadPage()
  }, false)
}
