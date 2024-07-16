export function translateModulesPage(new_translations) {
  document.getElementById('panel_modules_header').innerHTML = new_translations.page.modules.header

  /* INFO: arch type */
  const module_element_arch = document.getElementsByClassName('arch_desc')
  for (const module of module_element_arch) {
    module.innerHTML = new_translations.page.modules.arch
  }
}