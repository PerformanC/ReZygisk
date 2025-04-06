import { exec } from './kernelsu.js'

import { setError } from './main.js'
import { translateActionsPage } from './translate/actions.js'
import { translateHomePage } from './translate/home.js'
import { translateModulesPage } from './translate/modules.js'
import { translateSettingsPage } from './translate/settings.js'

export async function setNewLanguage(locate, initialize) {
  const old_translations = await getTranslations(initialize ? 'en_US' : localStorage.getItem('/system/language'))
  const new_translations = await getTranslations(locate)

  translateHomePage(old_translations, new_translations)
  translateModulesPage(new_translations)
  translateActionsPage(old_translations, new_translations)
  translateSettingsPage(new_translations)

  /* INFO: navbar info */
  document.getElementById('nav_home_title').innerHTML = new_translations.page.home.header
  document.getElementById('nav_modules_title').innerHTML = new_translations.page.modules.header
  document.getElementById('nav_actions_title').innerHTML = new_translations.page.actions.header
  document.getElementById('nav_settings_title').innerHTML = new_translations.page.settings.header

  /* INFO: Language small page */
  document.getElementById('small_panel_lang_title').innerHTML = new_translations.smallPage.language.header

  /* INFO: Theme small page */
  document.getElementById('small_panel_theme_title').innerHTML = new_translations.smallPage.theme.header
  document.getElementById('small_panel_theme_dark_option').innerHTML = new_translations.smallPage.theme.dark
  document.getElementById('small_panel_theme_light_option').innerHTML = new_translations.smallPage.theme.light
  document.getElementById('small_panel_theme_system_option').innerHTML = new_translations.smallPage.theme.system

  /* INFO: Error history small page */
  document.getElementById('errorh_copy').innerHTML = new_translations.smallPage.errorh.buttons.copy
  document.getElementById('errorh_clear_all').innerHTML = new_translations.smallPage.errorh.buttons.clear
  document.getElementById('small_panel_errorh_title').innerHTML = new_translations.smallPage.errorh.header
  document.getElementById('errorh_panel').placeholder = new_translations.smallPage.errorh.placeholder
}

export async function getTranslations(locate) {
  const translateData = await fetch(`./lang/${locate}.json`)
    .catch((err) => setError('WebUI', err.stack ? err.stack : err.message))

  return translateData.json()
}

export async function getAvailableLanguages() {
  const lsCmd = await exec('ls /data/adb/modules/zygisksu/webroot/lang')

  if (lsCmd.errno !== 0) return setError('WebUI', lsCmd.stderr)

  const languages = []
  lsCmd.stdout.split('\n').forEach((lang) => {
    if (lang.length !== 0)
      languages.push(lang.replace('.json', ''))
  })

  return languages
}