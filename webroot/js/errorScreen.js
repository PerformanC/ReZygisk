const button = document.getElementById('esc-copy-button')
const log = document.getElementById('esc-log')

button.addEventListener('click', () => {
  navigator.clipboard.writeText(log.innerHTML)
})