import { exec, fullScreen, toast } from '../kernelsu.js'

import { loadNavbar, setNavbar, whichCurrentPage } from './navbar.js'
import { runMainPageTransition, runMiniPageEnter, runMiniPageLeave } from './animator.js'

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
/* INFO: Prevent overlapping page transitions when users tap navigation rapidly. */
let isPageTransitioning = false
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
    toast(`Failed to load ${localStorage.getItem('/ReZygisk/language') || 'en_US'} strings. Entering safe mode.`)
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

function isMiniPage(pageId) {
  return miniPageRegex.test(pageId)
}

function isHTMLUnused(page, pageId) {
  /* INFO: Detect whether this page DOM is currently in the prefixed/inactive state. */
  if (!page || !page.childNodes) return false

  for (const child of page.childNodes) {
    if (child.id && child.id.startsWith(`page_${pageId}:`)) return true

    if (child.classList) {
      for (const className of child.classList) {
        if (className.startsWith(`page_${pageId}:`)) return true
      }
    }
  }

  return false
}

async function initializePage(pageId, pageSpecificContent, shouldApplyHTMLChanges = true) {
  const module = await importPageJS(pageId)

  if (!sufferedUpdate.includes(pageId)) {
    pageSpecificContent.innerHTML = await hotReloadStrings(pageSpecificContent.innerHTML, pageId)
    if (shouldApplyHTMLChanges) applyHTMLChanges(pageSpecificContent, pageId)

    module.onceViewAfterUpdate()

    sufferedUpdate.push(pageId)
  }

  if (!loadedPageView.includes(pageId)) {
    pageSpecificContent.innerHTML = await solveStrings(pageSpecificContent.innerHTML, pageId)
    if (shouldApplyHTMLChanges) applyHTMLChanges(pageSpecificContent, pageId)

    module.loadOnceView()

    loadedPageView.push(pageId)
  } else if (shouldApplyHTMLChanges) {
    applyHTMLChanges(pageSpecificContent, pageId)
  }

  module.load()
}

function unuseHTML(page, pageId) {
  /* INFO: Remove all event listeners from window */
  utils.removeAllListeners()
  const pagePrefix = `page_${pageId}:`

  if (page.childNodes) page.childNodes.forEach((child) => {
    /* INFO: Append pageId to id and classes */
    if (child.id && !child.id.startsWith(pagePrefix)) child.id = `${pagePrefix}${child.id}`
    if (child.classList) {
      const newClasses = []
      if (child.checked) child.classList.add(`--page_loader:checked=true`)

      for (const className of child.classList) {
        if (className.startsWith(pagePrefix)) {
          newClasses.push(className)
          continue
        }

        newClasses.push(`${pagePrefix}${className}`)
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
      if (isMiniPage(page)) pageSpecificContent.classList.add('page_loader_mini_layer')

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
  const pagePrefix = `page_${pageId}:`

  if (page.childNodes) page.childNodes.forEach((child) => {
    /* INFO: Remove pageId from id and classes */
    if (child.id && child.id.startsWith(pagePrefix)) {
      while (child.id.startsWith(pagePrefix)) {
        child.id = child.id.slice(pagePrefix.length)
      }
    }

    if (child.classList) {
      const newClasses = []

      for (const className of child.classList) {
        let normalizedClassName = className

        while (normalizedClassName.startsWith(pagePrefix)) {
          normalizedClassName = normalizedClassName.slice(pagePrefix.length)
        }

        if (normalizedClassName.length > 0) {
          newClasses.push(normalizedClassName)
        }
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
  /* INFO: Ignore navigation to the same page or while another transition is still running. */
  if (whichCurrentPage() === pageId) return false
  if (isPageTransitioning) return false

  isPageTransitioning = true

  try {
    const currentPage = whichCurrentPage()
    setNavbar(pageId)

    const targetIsMiniPage = isMiniPage(pageId)
    const currentIsMiniPage = currentPage ? isMiniPage(currentPage) : false
    /* INFO: Main-to-main transitions switch CSS at animation midpoint to avoid style conflicts. */
    const isMainToMain = currentPage && !currentIsMiniPage && !targetIsMiniPage
    const pageSpecificContent = document.getElementById(`${pageId}_content`)
    const targetNeedsRevert = isHTMLUnused(pageSpecificContent, pageId)

    if (targetNeedsRevert) revertHTMLUnuse(pageSpecificContent, pageId)
    if (!isMainToMain) document.getElementById(`${pageId}_css`).media = 'all'

    if (!currentPage) {
      await initializePage(pageId, pageSpecificContent, targetNeedsRevert)
      pageSpecificContent.style.display = 'block'

      return true
    }

    const currentPageContent = document.getElementById(`${currentPage}_content`)

    if (!currentIsMiniPage && targetIsMiniPage) {
      await initializePage(pageId, pageSpecificContent, targetNeedsRevert)
      await runMiniPageEnter(pageSpecificContent)

      return true
    }

    if (currentIsMiniPage) {
      /* INFO: Leaving a mini page always closes its overlay first, then opens the target page. */
      await runMiniPageLeave(currentPageContent)
      unuseHTML(currentPageContent, currentPage)
      document.getElementById(`${currentPage}_css`).media = 'not all'

      await initializePage(pageId, pageSpecificContent, targetNeedsRevert)

      if (targetIsMiniPage) {
        await runMiniPageEnter(pageSpecificContent)
      } else {
        pageSpecificContent.style.display = 'block'
      }

      return true
    }

    const transitionDirection = allMainPages.indexOf(pageId) > allMainPages.indexOf(currentPage) ? 1 : -1

    await runMainPageTransition(currentPageContent, pageSpecificContent, transitionDirection, async () => {
      unuseHTML(currentPageContent, currentPage)
      document.getElementById(`${currentPage}_css`).media = 'not all'
      document.getElementById(`${pageId}_css`).media = 'all'

      await initializePage(pageId, pageSpecificContent, targetNeedsRevert)
    })

    return true
  } catch (error) {
    /* INFO: Keep transition errors visible without breaking future navigation attempts. */
    console.error('Page transition failed:', error)
    toast('Error while changing page.')

    return false
  } finally {
    isPageTransitioning = false
  }
}

export async function reloadPage() {
  const pageId = whichCurrentPage()

  const pageSpecificContent = document.getElementById(`${pageId}_content`)
  pageSpecificContent.innerHTML = await solveStrings(await loadHTML(pageId), pageId)

  const module = await importPageJS(pageId)
  module.load()
  /* INFO: When reloading the page, due to the way the HTML is reloaded, the JavaScript
             listeners are lost, so we need to reapply them to ensure everything works. */
  utils.reapplyListeners()
}

export function getStrings(pageId, forceDefault = false) {
  return fetch(`lang/${forceDefault ? 'en_US' : (localStorage.getItem('/ReZygisk/language') || 'en_US')}.json`)
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
    .catch(() => {
       if (!forceDefault) {
        toast('Error loading strings for the selected language, loading default (en_US) strings.')
        
        return getStrings(pageId, true)
       }

      toast('Error loading default strings!')

      return false
    })
}

export function setLanguage(langId) {
  localStorage.setItem('/ReZygisk/language', langId)

  sufferedUpdate.length = 0
}

(async () => {
  await loadPages()

  let webui_config = localStorage.getItem('/ReZygisk/webui_config')

  if (!webui_config) {
    webui_config = {
      disableFullscreen: false,
      enableSystemFont: false
    }
    localStorage.setItem('/ReZygisk/webui_config', JSON.stringify(webui_config))
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

/* INFO: Global error handling to catch any unhandled errors and log them to a file for debugging purposes. */
window.addEventListener('error', function (event) {
  toast('An error occurred. See log file.')

  console.error('Unhandled error:', event.error)

  exec(`echo "Error: ${event.message}\n\n${event.error.stack}" > /data/adb/rezygisk/webui_error.log`)
})

window.addEventListener('unhandledrejection', function (event) {
  toast('An error occurred. See log file.')

  console.error('Unhandled promise rejection:', event.reason)

  exec(`echo "Error (Unhandled Rejection): ${event.reason}\n\n${event.reason.stack}" > /data/adb/rezygisk/webui_error.log`)
})