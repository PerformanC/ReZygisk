export function smallPageDisabler(page_name, new_page) {
  const navbar_data_tag = document.getElementById('cache-navbar-previous')
  const small_panel_data_tag = document.getElementById('cache-page-small-previous')

  document.getElementById(`small_panel_${page_name}`).classList.remove('show')
  small_panel_data_tag.removeAttribute('content')

  const previous = navbar_data_tag.getAttribute('content')

  /* INFO: Disable icon on old state */
  const pre_input = document.getElementById(`n_${previous}`)
  const pre_background = document.getElementById(`nibg_${previous}`)

  pre_input.removeAttribute('checked')
  pre_background.classList.remove('show')

  /* INFO: Enable icon on new state */
  const curr_input = document.getElementById(`n_${new_page}`)
  const i_background = document.getElementById(`nibg_${new_page}`)

  document.getElementById(`panel_${new_page}`).classList.toggle('show')
  curr_input.setAttribute('checked', '')
  i_background.classList.toggle('show')

  navbar_data_tag.setAttribute('content', 'settings')
}