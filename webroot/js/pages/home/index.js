import { whichCurrentPage } from '../navbar.js'
import { getStrings, loadPage } from '../pageLoader.js'
import utils from '../utils.js'
import { icon } from '../../icons.js'
import {
  MONITOR_STATE,
  copyText,
  escapeHTML,
  formatDuration,
  getDeviceInfo,
  getModuleProp,
  getState,
  haptic,
  overallStatus,
  startPolling,
  summarizeState,
  timeAgo
} from '../../rz.js'

const RING_LENGTH = 2 * Math.PI * 42

let lastUpdate = 0
let lastDevice = {}
let lastVersion = null
let stopPolling = () => {}
let tickerTimer = null
let refreshing = false

function isActive() {
  return whichCurrentPage() === 'home'
}

function fill(template, values) {
  return String(template || '').replace(/\{(\w+)\}/g, (_, key) => (values[key] !== undefined ? values[key] : ''))
}

function modulesText(count, strings) {
  return count === 1 ? strings.hero.module : fill(strings.hero.modules, { count })
}

function monitorLabel(state, strings) {
  switch (state) {
    case MONITOR_STATE.TRACING: return { text: strings.components.tracing, tone: 'ok' }
    case MONITOR_STATE.STOPPING: return { text: strings.components.stopping, tone: 'warn' }
    case MONITOR_STATE.STOPPED: return { text: strings.components.stopped, tone: 'warn' }
    case MONITOR_STATE.EXITING: return { text: strings.components.exited, tone: 'err' }
    default: return { text: strings.unknown, tone: '' }
  }
}

function tile({ label, value, hint, tone, iconName, action }) {
  return `
    <div class="nz_tile ${action ? 'nz_card_tap' : ''}" ${action ? `data-action="${action}"` : ''}>
      <div class="nz_tile_label">${iconName ? icon(iconName) : ''}<span>${escapeHTML(label)}</span></div>
      <div class="nz_tile_value" style="display: flex; align-items: center; gap: 8px;">
        <span class="nz_dot ${tone ? `nz_dot_${tone}` : ''}"></span>
        <span style="overflow: hidden; text-overflow: ellipsis;">${escapeHTML(value)}</span>
      </div>
      ${hint ? `<div class="nz_tile_hint">${escapeHTML(hint)}</div>` : ''}
    </div>
  `
}

function renderHero(summary, status, strings) {
  const hero = document.getElementById('home_hero')
  if (!hero) return

  hero.setAttribute('data-status', status)

  const titles = {
    ok: strings.status.ok,
    partial: strings.status.partially,
    down: strings.status.notWorking,
    unknown: strings.unknown
  }
  const icons = { ok: 'check', partial: 'warn', down: 'error', unknown: 'help' }

  const title = document.getElementById('home_status_title')
  title.classList.remove('nz_skeleton')
  title.style.width = ''
  title.textContent = titles[status]

  document.getElementById('home_ring_icon').innerHTML = icon(icons[status])

  const ratio = summary.expected ? summary.working / summary.expected : 0
  document.getElementById('home_ring_value').style.strokeDashoffset = String(RING_LENGTH * (1 - ratio))

  const sub = document.getElementById('home_status_sub')
  if (!summary.known) {
    sub.textContent = strings.hero.noState
  } else {
    sub.textContent = fill(strings.hero.injected, { working: summary.working, expected: summary.expected })
  }

  const pills = []
  if (lastVersion) pills.push(`<span class="nz_pill">${escapeHTML(lastVersion.split(' ')[0])}</span>`)
  if (summary.root) pills.push(`<span class="nz_pill nz_pill_muted">${escapeHTML(summary.root === 'Multiple' ? strings.rootImpls.multiple : summary.root)}</span>`)
  if (summary.known) pills.push(`<span class="nz_pill nz_pill_muted">${escapeHTML(modulesText(summary.modules.length, strings))}</span>`)

  document.getElementById('home_status_pills').innerHTML = pills.join('')
}

function renderIssues(summary, strings) {
  const container = document.getElementById('home_issues')
  if (!container) return

  const issues = []

  if (summary.monitor.state !== null && summary.monitor.state !== MONITOR_STATE.TRACING) {
    issues.push({
      tone: summary.monitor.state === MONITOR_STATE.EXITING ? '' : 'warn',
      title: strings.issues.monitor,
      body: summary.monitor.reason || monitorLabel(summary.monitor.state, strings).text
    })
  }

  summary.daemons.filter((daemon) => !daemon.running).forEach((daemon) => {
    issues.push({
      tone: '',
      title: fill(strings.issues.daemon, { bits: daemon.bits }),
      body: daemon.reason || strings.components.notRunning
    })
  })

  container.innerHTML = issues.map((issue) => `
    <div class="nz_banner ${issue.tone === 'warn' ? 'nz_banner_warn' : ''}">
      <div style="flex-shrink: 0; width: 22px; height: 22px; fill: ${issue.tone === 'warn' ? 'var(--warn)' : 'var(--err)'};">${icon(issue.tone === 'warn' ? 'warn' : 'error')}</div>
      <div><b>${escapeHTML(issue.title)}</b>${escapeHTML(issue.body)}</div>
    </div>
  `).join('')
}

function renderComponents(summary, strings) {
  const container = document.getElementById('home_components')
  if (!container) return

  if (!summary.known) {
    document.getElementById('home_components_title').style.display = 'none'
    container.innerHTML = ''

    return
  }

  document.getElementById('home_components_title').style.display = ''

  const monitor = monitorLabel(summary.monitor.state, strings)
  const tiles = [
    tile({ label: strings.components.monitor, value: monitor.text, tone: monitor.tone, iconName: 'speed', action: 'actions' })
  ]

  summary.daemons.forEach((daemon) => {
    tiles.push(tile({
      label: fill(strings.components.daemon, { bits: daemon.bits }),
      value: daemon.running ? strings.components.running : strings.components.notRunning,
      hint: daemon.running ? modulesText(daemon.modules.length, strings) : daemon.reason,
      tone: daemon.running ? 'ok' : 'err',
      iconName: 'chip'
    }))
  })

  summary.zygotes.forEach((zygote) => {
    tiles.push(tile({
      label: `Zygote${zygote.bits}`,
      value: zygote.injected ? strings.info.zygote.injected : strings.info.zygote.notInjected,
      tone: zygote.injected ? 'ok' : 'err',
      iconName: 'shield'
    }))
  })

  tiles.push(tile({
    label: strings.components.modules,
    value: String(summary.modules.length),
    hint: strings.components.viewAll,
    tone: summary.modules.length ? 'ok' : '',
    iconName: 'modules',
    action: 'modules'
  }))

  container.innerHTML = tiles.join('')
}

function deviceRows(strings) {
  const d = lastDevice
  const androidValue = d.android ? `${d.android}${d.sdk ? ` · ${fill(strings.device.sdk, { sdk: d.sdk })}` : ''}` : null

  return [
    [ strings.device.model, [ d.brand, d.model ].filter(Boolean).join(' ') + (d.device ? ` (${d.device})` : '') ],
    [ strings.androidVersion, androidValue ],
    [ strings.device.patch, d.patch ],
    [ strings.device.build, d.build ],
    [ strings.linuxKernelVersion, d.kernel ],
    [ strings.device.selinux, d.selinux ],
    [ strings.device.abi, d.abi ? d.abi.replace(/,/g, ', ') : null ],
    [ strings.device.uptime, d.uptime ? formatDuration(d.uptime, strings) : null ],
    [ strings.info.root, getCachedRoot(strings) ],
    [ strings.info.version, lastVersion ],
    [ strings.device.pids, [ d.monitorpid && `monitor ${d.monitorpid}`, d.daemonpid && `zygiskd ${d.daemonpid}` ].filter(Boolean).join(' · ') ]
  ].filter(([ , value ]) => value && String(value).trim().length)
}

let cachedRoot = null
function getCachedRoot(strings) {
  if (cachedRoot === 'Multiple') return strings.rootImpls.multiple

  return cachedRoot
}

function renderDevice(strings) {
  const container = document.getElementById('home_device')
  if (!container) return

  const rows = deviceRows(strings)
  container.innerHTML = rows.map(([ label, value ]) => `
    <div class="nz_row">
      <div class="nz_row_label">${escapeHTML(label)}</div>
      <div class="nz_row_value" data-copy="${escapeHTML(value)}">${escapeHTML(value)}</div>
    </div>
  `).join('') || `<div class="nz_row"><div class="nz_row_label">${escapeHTML(strings.unknown)}</div></div>`
}

function renderUpdated(strings) {
  const el = document.getElementById('home_updated')
  if (!el || !lastUpdate) return

  el.textContent = fill(strings.updated, { time: timeAgo(lastUpdate, strings) })
}

async function refresh({ full = false } = {}) {
  if (refreshing) return
  refreshing = true

  const button = document.getElementById('home_refresh')
  if (button) button.classList.add('nz_spin')

  try {
    const [ state, strings ] = await Promise.all([
      getState(),
      getStrings('home'),
      full ? getDeviceInfo().then((info) => { lastDevice = info }) : null,
      full || !lastVersion ? getModuleProp().then((prop) => { lastVersion = prop.version || null }) : null
    ])

    if (!strings || !isActive()) return

    const summary = summarizeState(state)
    cachedRoot = summary.root

    renderHero(summary, overallStatus(summary), strings)
    renderIssues(summary, strings)
    renderComponents(summary, strings)
    if (full) renderDevice(strings)

    lastUpdate = Date.now()
    renderUpdated(strings)
  } finally {
    refreshing = false

    const btn = document.getElementById('home_refresh')
    if (btn) setTimeout(() => btn.classList.remove('nz_spin'), 300)
  }
}

export async function loadOnce() {

}

export async function loadOnceView() {
  await refresh({ full: true })

  /* INFO: This hides the throbber screen */
  loading_screen.style.display = 'none'
}

export async function onceViewAfterUpdate() {
  await refresh({ full: true })
}

export async function load() {
  const strings = await getStrings('home')

  const refreshButton = document.getElementById('home_refresh')
  refreshButton.innerHTML = icon('refresh')
  utils.addListener(refreshButton, 'click', () => {
    haptic()
    refresh({ full: true })
  })

  utils.addListener(document.getElementById('home_components'), 'click', (event) => {
    const target = event.target.closest('[data-action]')
    if (!target) return

    haptic()
    loadPage(target.getAttribute('data-action'))
  })

  utils.addListener(document.getElementById('home_device'), 'click', (event) => {
    const value = event.target.closest('[data-copy]')?.getAttribute('data-copy')
    if (value) copyText(value, strings.copied)
  })

  utils.addListener(document.getElementById('home_copy_device'), 'click', () => {
    const report = deviceRows(strings).map(([ label, value ]) => `${label}: ${value}`).join('\n')
    copyText(report, strings.copied)
  })

  /* INFO: Re-render quickly when coming back to the page, then keep it live. */
  if (lastUpdate && Date.now() - lastUpdate > 1500) refresh()

  stopPolling()
  stopPolling = startPolling(() => refresh(), isActive)

  clearInterval(tickerTimer)
  tickerTimer = setInterval(() => {
    if (!isActive()) return clearInterval(tickerTimer)

    renderUpdated(strings)
  }, 1000)
}
