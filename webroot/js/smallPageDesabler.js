export function smallPageDisabler(page_name, new_page) {
  document.getElementById(`small_panel_${page_name}`).classList.remove('show')
  localStorage.removeItem('/cache/page/small/previous')

  const previous = localStorage.getItem('/cache/navbar/previous')

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

  localStorage.setItem('/cache/navbar/previous', 'settings')
}