import { 
  avaliableLanguages, 
  getTranslations, 
  setNewLanguage
} from '../language.js'
import { smallPageDisabler } from '../smallPageDesabler.js'

/* INFO: Initial setup */
let index = 0

function setAvaliableLanguage() {
  const langKey = avaliableLanguages[index]

  index += 1

  getTranslations(langKey).then((data) => {
    document.getElementById('lang_modal_list').innerHTML += `
    <div lang-data="${langKey}" class="dim card card_animation" style="padding: 25px 15px; cursor: pointer;">
      <div lang-data="${langKey}" class="dimc" style="font-size: 1.1em;">${data.langName}</div>
    </div>
    `
  })
  .finally(() => { 
    if (index !== avaliableLanguages.length) setAvaliableLanguage() 
  })
}
setAvaliableLanguage()

/* INFO: Event setup */
document.getElementById('lang_page_toggle').addEventListener('click', () => {
  const previous = !localStorage.getItem('/cache/navbar/previous') ? setData('home') : localStorage.getItem('/cache/navbar/previous')
  document.getElementById(`panel_${previous}`).classList.remove('show')
  document.getElementById('small_panel_language').classList.toggle('show')
  localStorage.setItem('/cache/page/small/previous', 'language')
})

document.getElementById('sp_lang_close').addEventListener('click', () => smallPageDisabler('language', 'settings'))

document.addEventListener('click', async (event) => {
  const getLangLocate = event.target.getAttribute('lang-data')
  if (!getLangLocate || typeof getLangLocate !== 'string') return

  smallPageDisabler('language', 'settings')
  await setNewLanguage(getLangLocate)

  localStorage.setItem('/system/language', getLangLocate)
}, false)