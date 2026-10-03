import { whichCurrentPage } from '../../../navbar.js'
import { getStrings } from '../../../pageLoader.js'
import utils from '../../../utils.js'
import { toast } from '../../../../kernelsu.js'
import { icon } from '../../../../icons.js'
import { LOG_TAGS, copyText, escapeHTML, haptic, run, shellQuote } from '../../../../rz.js'

const PAGE_ID = 'mini_actions_logs'
const MAX_LINES = 1500
const POLL_MS = 2000
const LEVELS = [ 'V', 'D', 'I', 'W', 'E' ]
const LOG_REGEX = /^(\d\d-\d\d)\s+(\d\d:\d\d:\d\d)\.\d+\s+(\d+)\s+(\d+)\s+([VDIWEF])\s+(.+?)\s*:\s?(.*)$/

let lines = []
let lastSignature = ''
let paused = false
let minLevel = 'V'
let query = ''
let timer = null
let clearedAt = null

function isActive() {
  return whichCurrentPage() === PAGE_ID
}

function parse(raw) {
  return raw.split('\n').map((line) => {
    const match = LOG_REGEX.exec(line)
    if (!match) return null

    return {
      raw: line,
      date: match[1],
      time: match[2],
      pid: match[3],
      level: match[5],
      tag: match[6],
      msg: match[7]
    }
  }).filter(Boolean)
}

function visibleLines() {
  const minIndex = LEVELS.indexOf(minLevel)
  const q = query.trim().toLowerCase()

  return lines.filter((line) => {
    const levelIndex = line.level === 'F' ? LEVELS.length : LEVELS.indexOf(line.level)
    if (levelIndex < minIndex) return false
    if (q && !line.raw.toLowerCase().includes(q)) return false

    return true
  })
}

function scrollContainer() {
  return document.getElementById(`${PAGE_ID}_content`)
}

function render() {
  const list = document.getElementById('logs_list')
  if (!list) return

  const container = scrollContainer()
  const nearBottom = !container || container.scrollHeight - container.scrollTop - container.clientHeight < 120

  list.innerHTML = visibleLines().map((line) => `
    <div class="nz_log_line nz_log_${line.level}">
      <span class="nz_log_lvl">${line.level}</span>
      <span class="nz_log_time">${escapeHTML(line.time)}</span>
      <span><span class="nz_log_tag">${escapeHTML(line.tag)}</span> <span class="nz_log_msg">${escapeHTML(line.msg)}</span></span>
    </div>
  `).join('')

  if (container && nearBottom) container.scrollTop = container.scrollHeight
}

async function poll() {
  const result = await run(`logcat -d -v threadtime -t ${MAX_LINES} -s ${LOG_TAGS.map(shellQuote).join(' ')} 2>/dev/null`)
  if (!isActive()) return

  let parsed = parse(result.stdout)
  if (clearedAt) parsed = parsed.filter((line) => `${line.date} ${line.time}` > clearedAt)

  const signature = `${parsed.length}:${parsed.length ? parsed[parsed.length - 1].raw : ''}`
  if (signature === lastSignature) return

  lastSignature = signature
  lines = parsed
  render()
}

function schedule() {
  clearTimeout(timer)

  timer = setTimeout(async () => {
    if (!isActive()) return

    if (!paused && !document.hidden) {
      try {
        await poll()
      } catch (error) {
        console.error('Log polling failed:', error)
      }
    }

    schedule()
  }, POLL_MS)
}

function renderLevels(strings) {
  document.getElementById('logs_levels').innerHTML = LEVELS.map((level) => `
    <button class="nz_level_chip" data-level="${level}" aria-pressed="${level === minLevel}">${level === 'V' ? escapeHTML(strings.all) : `${level}+`}</button>
  `).join('')
}

function renderPause(strings) {
  document.getElementById('logs_pause').innerHTML = icon(paused ? 'play' : 'pause')
  document.getElementById('logs_live_text').textContent = paused ? strings.paused : strings.live
  document.querySelector('#logs_live .nz_dot').className = `nz_dot ${paused ? 'nz_dot_warn' : 'nz_dot_ok'}`
}

export async function loadOnce() {

}

export async function loadOnceView() {

}

export async function onceViewAfterUpdate() {

}

export async function load() {
  const strings = await getStrings(PAGE_ID)

  document.getElementById('logs_clear').innerHTML = icon('trash')
  document.getElementById('logs_copy').innerHTML = icon('copy')
  document.getElementById('logs_save').innerHTML = icon('download')
  document.getElementById('logs_search_icon').innerHTML = icon('search')
  document.getElementById('logs_list').setAttribute('data-empty', strings.empty)

  renderPause(strings)
  renderLevels(strings)

  /* INFO: Stick the toolbar right below the mini page header. */
  const header = scrollContainer()?.querySelector('.header')
  const toolbar = document.querySelector('.nz_log_toolbar')
  if (header && toolbar) toolbar.style.top = `${header.offsetHeight}px`

  const search = document.getElementById('logs_search')
  search.value = query

  /* INFO: Mini pages are not covered by utils.removeAllListeners on close, so
             remove our previous handlers before binding new ones. */
  for (const id of [ 'logs_pause', 'logs_clear', 'logs_copy', 'logs_save', 'logs_levels', 'logs_search' ]) {
    utils.removeAllListeners(document.getElementById(id))
  }

  utils.addListener(document.getElementById('logs_pause'), 'click', () => {
    haptic()
    paused = !paused
    renderPause(strings)
  })

  utils.addListener(document.getElementById('logs_clear'), 'click', () => {
    haptic()

    const last = lines[lines.length - 1]
    if (last) clearedAt = `${last.date} ${last.time}`

    lines = []
    lastSignature = ''
    render()
  })

  utils.addListener(document.getElementById('logs_copy'), 'click', () => {
    haptic()
    copyText(visibleLines().map((line) => line.raw).join('\n'), strings.copied)
  })

  utils.addListener(document.getElementById('logs_save'), 'click', async () => {
    haptic()

    const result = await run(
      'D=/sdcard/Download/ReZygisk; mkdir -p "$D"; F="$D/rezygisk-logcat-$(date +%Y%m%d-%H%M%S).txt"; ' +
      `logcat -d -v threadtime -s ${LOG_TAGS.map(shellQuote).join(' ')} > "$F" 2>&1 && echo "$F"`
    )

    const path = result.stdout.trim()
    if (result.ok && path) toast(strings.saved.replace('{path}', path))
    else toast(strings.saveFailed)
  })

  utils.addListener(document.getElementById('logs_levels'), 'click', (event) => {
    const level = event.target.closest('[data-level]')?.getAttribute('data-level')
    if (!level) return

    haptic()
    minLevel = level
    renderLevels(strings)
    render()
  })

  utils.addListener(search, 'input', () => {
    query = search.value
    render()
  })

  render()
  await poll()
  schedule()
}
