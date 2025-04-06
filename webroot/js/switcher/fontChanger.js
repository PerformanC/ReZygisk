const switcher = document.getElementById('font_switcher')
const rootCss = document.querySelector(':root')

let sys_font = localStorage.getItem('/system/font')
if (!sys_font) sys_font = setData('false')
if (sys_font === 'true') {
  switcher.setAttribute('checked', '')

  setSystemFont()
}

switcher.addEventListener('click', () => {
  sys_font = setData(String(switcher.checked))

  switcher.checked ? setSystemFont() : document.getElementById('font-tag').remove()
})

function setSystemFont() {
  const headTag = document.getElementsByTagName('head')[0]
  const styleTag = document.createElement('style')

  styleTag.id = 'font-tag'
  headTag.appendChild(styleTag)
  styleTag.innerHTML = `
    :root {
      --font-family: system-ui, -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Oxygen, Ubuntu, Cantarell, 'Open Sans', 'Helvetica Neue', sans-serif
    }`
}

function setData(mode) {
  localStorage.setItem('/system/font', mode)

  return mode
}