import { exec, fullScreen, toast } from '../kernelsu.js'

import { loadNavbar, setNavbar, whichCurrentPage } from './navbar.js'

/* INFO: Prototypes */
import utils from './utils.js'

const head = document.getElementsByTagName('head')[0]
const miniPageRegex = /mini_(.*)_(.*)/

export const allMainPages = [
  'home',
  'modules',
  'actions',
  'settings'
]

export const allMiniPages = [
  'mini_settings_language',
  'mini_settings_theme'
]

export const allPages = [ ...allMainPages, ...allMiniPages ]


const loadedPageView = []
/* INFO: Direct assignment would link both arrays. We do not want that. */
const sufferedUpdate = [ ...allPages ]
const pageReplacements = allPages.reduce((obj, pageId) => {
  obj[pageId] = []

  return obj
}, {})

async function loadHTML(pageId) {
  if (miniPageRegex.test(pageId)) {
    const miniPageIdData = miniPageRegex.exec(pageId)
    const parentPage = miniPageIdData[1]
    const miniPage = miniPageIdData[2]
    return fetch(`js/pages/${parentPage}/minipage/${miniPage}/index.html`)
      .then((response) => response.text())
      .then((data) => {
        return data
      })
      .catch(() => false)
  } else {
    return fetch(`js/pages/${pageId}/index.html`)
      .then((response) => response.text())
      .then((data) => {
        return data
      })
      .catch(() => false)
  }
}

async function hotReloadStrings(html, pageId) {
  const strings = await getStrings(pageId)
  if (!strings) return html

  pageReplacements[pageId].forEach((replacement, i) => {
    const key = replacement.key.slice(2, -2)

    const split = key.split('.')
    if (split.length === 1) {
      html = html.replace(replacement.value, strings[key])
      pageReplacements[pageId][i].value = strings[key]
    } else {
      let value = strings
      split.forEach((key) => {
        value = value[key]
      })

      html = html.replace(replacement.value, value)
      pageReplacements[pageId][i].value = value
    }
  })

  return html
}

async function solveStrings(html, pageId) {
  const strings = await getStrings(pageId)
  if (!strings) return html

  const regex = /{{(.*?)}}/g
  const matches = html.match(regex)

  if (!matches) return html

  try {
    matches.forEach((match) => {
      const key = match.slice(2, -2)

      const split = key.split('.')
      if (split.length === 1) {
        html = html.replace(match, strings[key])
        pageReplacements[pageId].push({ key: match, value: strings[key] })
      } else {
        let value = strings
        split.forEach((key) => {
          value = value[key]
        })

        html = html.replace(match, value)
        pageReplacements[pageId].push({ key: match, value: value })
      }
    })
  } catch (e) {
    toast(`Failed to load ${localStorage.getItem('language') || 'en_US'} strings. Entering safe mode.`)
  }

  /* INFO: Perform navbar string replacement */
  document.getElementById('nav_home_title').innerHTML = strings.navbar.home
  document.getElementById('nav_actions_title').innerText = strings.navbar.actions
  document.getElementById('nav_modules_title').innerText = strings.navbar.modules
  document.getElementById('nav_settings_title').innerText = strings.navbar.settings

  return html
}

async function getPageScripts(pageId) {
  if (miniPageRegex.test(pageId)) {
    const miniPageIdData = miniPageRegex.exec(pageId)
    const parentPage = miniPageIdData[1]
    const miniPage = miniPageIdData[2]
    return fetch(`js/pages/${parentPage}/minipage/${miniPage}/pageScripts`)
      .then((response) => response.text())
      .then((data) => {
        return data
      })
      .catch(() => false)
  } else {
    return fetch(`js/pages/${pageId}/pageScripts`)
      .then((response) => response.text())
      .then((data) => {
        return data
      })
      .catch(() => false)
  }
}

async function getPageCSS(pageId) {
  if (miniPageRegex.test(pageId)) {
    const miniPageIdData = miniPageRegex.exec(pageId)
    const parentPage = miniPageIdData[1]
    const miniPage = miniPageIdData[2]
    return await fetch(`js/pages/${parentPage}/minipage/${miniPage}/index.css`)
      .then((response) => response.text())
      .then((data) => {
        return data
      })
      .catch(() => false)
  } else {
    return fetch(`js/pages/${pageId}/index.css`)
      .then((response) => response.text())
      .then((data) => {
        return data
      })
      .catch(() => false)
  }
}

function importPageJS(pageId) {
  if (miniPageRegex.test(pageId)) {
    const miniPageIdData = miniPageRegex.exec(pageId)
    const parentPage = miniPageIdData[1]
    const miniPage = miniPageIdData[2]
    return import(`./${parentPage}/minipage/${miniPage}/index.js`)
  } else {
    return import(`./${pageId}/index.js`)
  }
}

function unuseHTML(page, pageId) {
  /* INFO: Remove all event listeners from window */
  utils.removeAllListeners()

  if (page.childNodes) page.childNodes.forEach((child) => {
    /* INFO: Append pageId to id and classes */
    if (child.id) child.id = `page_${pageId}:${child.id}`
    if (child.classList) {
      const newClasses = []
      if (child.checked) child.classList.add(`--page_loader:checked=true`)

      for (const className of child.classList) {
        newClasses.push(`page_${pageId}:${className}`)
      }

      child.classList = []
      for (const className of newClasses) {
        child.classList.add(className)
      }
    }

    unuseHTML(child, pageId)
  })
}

async function loadPages() {
  return new Promise((resolve) => {
    /*
      INFO: Usually dynamic HTML leads to a lot of visual problems, which
              can vary from missing CSS for an extremely brief moment to
              a full page re-rendering. This is why we load all pages at
              once and then we just switch between them.
    */

    let amountLoaded = 0
    allPages.forEach(async (page) => {
      const pageHTML = await loadHTML(page)
      if (pageHTML === false) {
        toast('Error loading page')

        return;
      }

      const pageJSScripts = await getPageScripts(page)
      if (pageJSScripts === false) {
        toast(`Error while loading ${page} scripts`)

        return;
      }

      const pageContent = document.getElementById('page_content')
      const pageSpecificContent = document.createElement('div')
      pageSpecificContent.id = `${page}_content`
      pageSpecificContent.innerHTML = pageHTML
      pageSpecificContent.style.display = 'none'

      pageContent.appendChild(pageSpecificContent)
      unuseHTML(pageSpecificContent, page)

      const cssData = await getPageCSS(page)
      if (cssData) {
        const cssCode = document.createElement('style')
        cssCode.id = `${page}_css`
        cssCode.innerHTML = cssData
        cssCode.media = 'not all'

        head.appendChild(cssCode)
      }

      pageJSScripts.split('\n').forEach((line) => {
        if (line.length === 0) return;

        const jsCode = document.createElement('script')
        jsCode.src = line
        jsCode.type = 'module'
        jsCode.id = `${page}_js`

        const first = document.getElementsByTagName('script')[0]
        if (!first) {
          head.appendChild(jsCode)

          return;
        }

        first.parentNode.insertBefore(jsCode, first)
      })

      const pageJS = importPageJS(page)
      pageJS.then((module) => module.loadOnce())

      amountLoaded++
      if (amountLoaded === allPages.length) resolve()
    })
  })
}

function revertHTMLUnuse(page, pageId) {
  if (page.childNodes) page.childNodes.forEach((child) => {
    /* INFO: Remove pageId from id and classes */
    if (child.id) child.id = child.id.split(`page_${pageId}:`)[1]
    if (child.classList) {
      const newClasses = []

      for (const className of child.classList) {
        newClasses.push(className.split(`page_${pageId}:`)[1])
      }

      child.classList = []
      for (const className of newClasses) {
        child.classList.add(className)
      }
    }

    revertHTMLUnuse(child, pageId)
  })
}

function applyHTMLChanges(page, pageId) {
  if (page.childNodes) page.childNodes.forEach((child) => {
    /* INFO: Remove pageId from id and classes */
    if (child.classList) {
      const newClasses = []

      for (const className of child.classList) {
        if (className.startsWith(`--page_loader:checked=true`)) {
          child.checked = true
        }

        newClasses.push(className)
      }

      child.classList = []
      for (const className of newClasses) {
        child.classList.add(className)
      }
    }

    applyHTMLChanges(child, pageId)
  })
}

export async function loadPage(pageId) {
  console.log("Loaded: ", pageId)
  if (whichCurrentPage() === pageId) return false

  const currentPage = whichCurrentPage()
  if (currentPage) {
    const currentPageContent = document.getElementById(`${currentPage}_content`)
    currentPageContent.style.display = 'none'

    unuseHTML(currentPageContent, currentPage)
    document.getElementById(`${currentPage}_css`).media = 'not all'
  }

  const pageSpecificContent = document.getElementById(`${pageId}_content`)
  revertHTMLUnuse(pageSpecificContent, pageId)
  document.getElementById(`${pageId}_css`).media = 'all'

  setNavbar(pageId)

  const module = await importPageJS(pageId)
  if (!sufferedUpdate.includes(pageId)) {
    pageSpecificContent.innerHTML = await hotReloadStrings(pageSpecificContent.innerHTML, pageId)
    applyHTMLChanges(pageSpecificContent, pageId)

    module.onceViewAfterUpdate()

    sufferedUpdate.push(pageId)
  }

  if (!loadedPageView.includes(pageId)) {
    pageSpecificContent.innerHTML = await solveStrings(pageSpecificContent.innerHTML, pageId)
    applyHTMLChanges(pageSpecificContent, pageId)

    module.loadOnceView()

    loadedPageView.push(pageId)
  } else {
    applyHTMLChanges(pageSpecificContent, pageId)
  }

  module.load()

  pageSpecificContent.style.display = 'block'
}

export async function reloadPage() {
  const pageId = whichCurrentPage()

  const pageSpecificContent = document.getElementById(`${pageId}_content`)
  pageSpecificContent.innerHTML = await solveStrings(await loadHTML(pageId), pageId)

  /* INFO: When reloading the page, due to the way the HTML is reloaded, the JavaScript
             listeners are lost, so we need to reapply them to ensure everything works. */
  utils.reapplyListeners()
}

export function getStrings(pageId) {
  return fetch(`lang/${localStorage.getItem('language') || 'en_US'}.json`)
    .then((response) => response.json())
    .then((data) => {
      return {
        ...data.pages[pageId],
        ...data.globals,
        navbar: {
          home: data.pages.home.title,
          modules: data.pages.modules.title,
          actions: data.pages.actions.title,
          settings: data.pages.settings.title
        }
      }
    })
    .catch(() => false)
}

export function setLanguage(langId) {
  localStorage.setItem('language', langId)

  sufferedUpdate.length = 0
}

(async () => {
  await loadPages()

  let webui_config = localStorage.getItem('/system/webui_config')

  if (!webui_config) {
    webui_config = {
      disableFullscreen: false,
      enableSystemFont: false
    }
    localStorage.setItem('/system/webui_config', JSON.stringify(webui_config))
  } else {
    webui_config = JSON.parse(webui_config)
  }

  if (!webui_config.disableFullscreen) fullScreen(true)
  if (webui_config.enableSystemFont) {
    const headTag = document.getElementsByTagName('head')[0]
    const styleTag = document.createElement('style')

    styleTag.id = 'font-tag'
    headTag.appendChild(styleTag)
    styleTag.innerHTML = `
      :root {
        --font-family: system-ui, -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Oxygen, Ubuntu, Cantarell, 'Open Sans', 'Helvetica Neue', sans-serif
      }`
  }

  loadPage('home')
  loadNavbar()
})()
