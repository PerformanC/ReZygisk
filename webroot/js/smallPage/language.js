import { 
  getTranslations, 
  setNewLanguage,
  getAvailableLanguages
} from '../language.js'
import { smallPageDisabler } from '../smallPageDesabler.js'

/* INFO: Initial setup */
let index = 0

async function setAvailableLanguage() {
  const availableLanguages = await getAvailableLanguages()
  const langKey = availableLanguages[index]

  index += 1

  getTranslations(langKey).then((data) => {
    document.getElementById('lang_modal_list').innerHTML += `
    <div lang-data="${langKey}" class="dim card card_animation" style="padding: 25px 15px; cursor: pointer;">
      <div lang-data="${langKey}" class="dimc" style="font-size: 1.1em;">${data.langName}</div>
    </div>
    `
  })
  .finally(() => { 
    if (index !== avaliableLanguages.length) setAvailableLanguage() 
  })
}
setAvailableLanguage()

/* INFO: Event setup */
const navbar_data_tag = document.getElementById('cache-navbar-previous')
const small_panel_data_tag = document.getElementById('cache-page-small-previous')

document.getElementById('lang_page_toggle').addEventListener('click', () => {
  const previous = !navbar_data_tag.getAttribute('content') ? setData('home', small_panel_data_tag) : navbar_data_tag.getAttribute('content')
  document.getElementById(`panel_${previous}`).classList.remove('show')
  document.getElementById('small_panel_language').classList.toggle('show')
  small_panel_data_tag.setAttribute('content', 'language')
})

document.getElementById('sp_lang_close').addEventListener('click', () => smallPageDisabler('language', 'settings'))

document.addEventListener('click', async (event) => {
  const getLangLocate = event.target.getAttribute('lang-data')
  if (!getLangLocate || typeof getLangLocate !== 'string') return

  smallPageDisabler('language', 'settings')
  await setNewLanguage(getLangLocate)

  localStorage.setItem('/system/language', getLangLocate)
}, false)

function setData(data, tag) {
  tag.setAttribute('content', data)

  return data
}