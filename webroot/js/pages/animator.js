/* INFO: Wait until the CSS animation ends, with a timeout fallback to avoid hanging transitions. */
function waitForAnimationEnd(element, fallbackMs = 280) {
  return new Promise((resolve) => {
    let done = false

    const finalize = () => {
      if (done) return
      done = true
      element.removeEventListener('animationend', finalize)
      resolve()
    }

    element.addEventListener('animationend', finalize, { once: true })
    setTimeout(finalize, fallbackMs)
  })
}

export async function runMainPageTransition(currentPageContent, nextPageContent, direction = 1, onHalfway = null) {
  /* INFO: Flip the slide direction based on main page ordering (left/right navigation). */
  const inFrom = direction > 0 ? '34%' : '-34%'
  const outTo = direction > 0 ? '-24%' : '24%'

  currentPageContent.style.setProperty('--page_loader_main_out_to', outTo)
  nextPageContent.style.setProperty('--page_loader_main_in_from', inFrom)

  currentPageContent.classList.add('page_loader_main_out')
  await waitForAnimationEnd(currentPageContent)

  currentPageContent.classList.remove('page_loader_main_out')
  currentPageContent.style.removeProperty('--page_loader_main_out_to')
  currentPageContent.style.display = 'none'

  /* INFO: Run a midpoint callback between out/in animations so loader can switch active page/CSS safely. */
  if (onHalfway) await onHalfway()

  nextPageContent.classList.add('page_loader_main_in')
  nextPageContent.style.display = 'block'
  await waitForAnimationEnd(nextPageContent)

  nextPageContent.classList.remove('page_loader_main_in')
  nextPageContent.style.removeProperty('--page_loader_main_in_from')
}

export async function runMiniPageEnter(pageSpecificContent) {
  /* INFO: Mini pages slide in as an overlay from the right side. */
  pageSpecificContent.classList.add('page_loader_mini_in')
  pageSpecificContent.style.display = 'block'

  await waitForAnimationEnd(pageSpecificContent)

  pageSpecificContent.classList.remove('page_loader_mini_in')
}

export async function runMiniPageLeave(pageSpecificContent) {
  /* INFO: Mini pages slide out to the right and then become hidden. */
  pageSpecificContent.classList.add('page_loader_mini_out')

  await waitForAnimationEnd(pageSpecificContent)

  pageSpecificContent.classList.remove('page_loader_mini_out')
  pageSpecificContent.style.display = 'none'
}