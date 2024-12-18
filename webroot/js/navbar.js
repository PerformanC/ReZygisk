const navbar_data_tag = document.getElementById('cache-navbar-previous')
const small_panel_data_tag = document.getElementById('cache-page-small-previous')

setData('home', navbar_data_tag)

document.getElementById('panel_home').classList.toggle('show')
document.getElementById(`nibg_home`).classList.toggle('show')

document.querySelectorAll('[name=navbutton]').forEach((element) => {
  element.addEventListener('click', (event) => {
    let smallPagePass = false

    const value = event.target.value
    const previous = !navbar_data_tag.getAttribute('content') 
      ? setData('home', navbar_data_tag) 
      : navbar_data_tag.getAttribute('content')

    const small_panel = small_panel_data_tag.getAttribute('content')

    if (small_panel && small_panel.length !== 0) {
      document.getElementById(`small_panel_${small_panel}`).classList.remove('show')
      small_panel_data_tag.removeAttribute('content')
      smallPagePass = true
    }

    if (previous === value && !smallPagePass) return;

    /* INFO: Disable icon on old state */
    const pre_input = document.getElementById(`n_${previous}`)
    const pre_background = document.getElementById(`nibg_${previous}`)

    document.getElementById(`panel_${previous}`).classList.remove('show')
    pre_input.removeAttribute('checked')
    pre_background.classList.remove('show')

    /* INFO: Enable icon on new state */
    const curr_input = document.getElementById(`n_${value}`)
    const i_background = document.getElementById(`nibg_${value}`)

    document.getElementById(`panel_${value}`).classList.toggle('show')
    curr_input.setAttribute('checked', '')
    i_background.classList.toggle('show')

    setData(value, navbar_data_tag)
  })
})

function setData(data, tag) {
  tag.setAttribute('content', data)

  return data
}