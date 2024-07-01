import { lang } from '../lang/index.js'
import { setNewLang } from '../language.js'

const lang_list = document.getElementById('lang_modal_list')
const target = document.getElementById('lang_modal')
const key_list = Object.keys(lang)
let index = 0

for (const lang_key of key_list) {
  index += 1

  const value = lang[lang_key]
  lang_list.innerHTML += `
    <div lang_section="true" id="${lang_key}" class="${index === key_list.length ? '' : 'spliter'}" style="padding-top: 25px; padding-bottom: 25px; font-size: 1.3em;">
      ${value.langName}
    </div>`
}

document.addEventListener('click', (event) => {
  if (!event.target.getAttribute('lang_section')) return;

  setNewLang(event.target.id)

  target.style.display = 'none'
  localStorage.setItem('/cache/modal/language/state', 'closed')
  localStorage.setItem('/system/language', event.target.id)
}, false)