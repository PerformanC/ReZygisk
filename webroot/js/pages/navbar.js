import { loadPage, allPages, allMiniPages } from './pageLoader.js'

export function loadNavbar() {
  document.getElementById('nibg_home').classList.add('show')
}

export function setNavbar(page) {
  /* INFO: Page loader may return false if the page is mini */
  if (allMiniPages.includes(page)) {
    allPages.forEach((page) => {
      document.getElementById(`n_${page}`).removeAttribute('checked')
    })
    document.getElementById(`n_${page}`).setAttribute('checked', '')
    return
  }

  allPages.forEach((page) => {
    document.getElementById(`n_${page}`).removeAttribute('checked')
    document.getElementById(`nibg_${page}`).classList.remove('show')
    document.getElementById(`ni_${page}`).style.background = ''
  })

  document.getElementById(`n_${page}`).setAttribute('checked', '')
  document.getElementById(`nibg_${page}`).classList.add('show')
  document.getElementById(`ni_${page}`).style.background = `url(./assets/${page}/filled.svg)`
}

export function whichCurrentPage() {
  for (const page of allPages) {
    if (document.getElementById(`n_${page}`).hasAttribute('checked')) return page
  }

  return null
}

document.querySelectorAll('[name=navbutton]').forEach((element) => {
  element.addEventListener('click', (event) => {
    const value = event.target.value

    /* INFO: Page loader may return false if the page is already loaded */
    if (!loadPage(value)) return;

    allPages.forEach((page) => {
      document.getElementById(`n_${page}`).removeAttribute('checked')
      document.getElementById(`nibg_${page}`).classList.remove('show')
      document.getElementById(`ni_${page}`).style.background = ''
    })

    document.getElementById(`n_${value}`).setAttribute('checked', '')
    document.getElementById(`nibg_${value}`).classList.add('show')
    document.getElementById(`ni_${value}`).style.background = `url(./assets/${value}/filled.svg)`
  })
})