import { whichCurrentPage } from '../navbar.js'
import { getStrings } from '../pageLoader.js'
import utils from '../utils.js'
import { icon } from '../../icons.js'
import { copyText, escapeHTML, getState, getZygiskModules, haptic, startPolling, summarizeState } from '../../rz.js'

const STATUS_TONE = {
  working: 'ok',
  partial: 'warn',
  notLoaded: 'err',
  disabled: 'muted',
  removal: 'err',
  pending: 'warn',
  unknown: 'muted'
}

const ISSUES = [ 'partial', 'notLoaded', 'unknown' ]

let modules = []
let query = ''
let filter = 'all'
let loading = false
let lastSignature = ''
let stopPolling = () => {}
const openCards = new Set()

function isActive() {
  return whichCurrentPage() === 'modules'
}

function hueOf(text) {
  let hash = 0
  for (let i = 0; i < text.length; i++) hash = (hash * 31 + text.charCodeAt(i)) | 0

  return Math.abs(hash) % 360
}

function reasonText(mod, strings) {
  if (!mod.reason) return null

  return (strings.reasons[mod.reason.key] || '')
    .replace('{abi}', mod.reason.abi || '')
    .replace('{bits}', mod.reason.bits || '')
}

function moduleCard(mod, strings, index) {
  const initial = mod.name.trim().charAt(0).toUpperCase() || '?'
  const tone = STATUS_TONE[mod.status] || 'muted'
  const reason = reasonText(mod, strings)

  const tags = []
  mod.targets.forEach((target) => {
    if (!target.hasLib) return

    tags.push(`<span dir="ltr" class="nz_pill ${target.loaded ? 'nz_pill_ok' : 'nz_pill_muted'}">${escapeHTML(target.bits)}-bit${target.loaded ? ' ✓' : ''}</span>`)
  })
  if (mod.updatePending) tags.push(`<span class="nz_pill nz_pill_warn">${escapeHTML(strings.updatePending)}</span>`)
  if (mod.webui) tags.push('<span class="nz_pill nz_pill_muted">WebUI</span>')
  if (mod.action) tags.push('<span class="nz_pill nz_pill_muted">Action</span>')

  const meta = [ mod.version, mod.author && `${strings.by} ${mod.author}` ].filter(Boolean).join(' · ')

  const libRows = mod.targets.map((target) => `
    <div class="nz_row">
      <div class="nz_row_label nz_mono">zygisk/${escapeHTML(target.abi)}.so</div>
      <div class="nz_row_value">${escapeHTML(!target.hasLib ? strings.lib.missing : target.loaded ? strings.lib.loaded : strings.lib.notLoaded)}</div>
    </div>
  `).join('')

  return `
    <div class="nz_card nz_card_tap" data-module="${escapeHTML(mod.id)}" data-open="${openCards.has(mod.id)}" style="animation-delay: ${Math.min(index, 12) * 30}ms;">
      <div class="nz_module">
        <div class="nz_avatar" style="--hue: ${hueOf(mod.id)};${mod.status === 'disabled' || mod.status === 'removal' ? ' filter: grayscale(1); opacity: 0.6;' : ''}">${escapeHTML(initial)}</div>
        <div class="nz_module_body">
          <div style="display: flex; align-items: flex-start; justify-content: space-between; gap: 8px;">
            <div style="min-width: 0;">
              <div class="nz_module_name">${escapeHTML(mod.name)}</div>
              <div class="nz_module_meta nz_mono">${escapeHTML(mod.id)}</div>
            </div>
            <span class="nz_pill nz_pill_${tone}" style="flex-shrink: 0;"><span class="nz_dot ${tone === 'muted' ? '' : `nz_dot_${tone}`}"></span>${escapeHTML(strings.status[mod.status])}</span>
          </div>
          ${meta ? `<div class="nz_module_meta">${escapeHTML(meta)}</div>` : ''}
          ${reason ? `<div class="nz_module_desc" style="color: var(--${tone === 'err' ? 'err' : 'warn'}); -webkit-line-clamp: unset;">${escapeHTML(reason)}</div>` : ''}
          ${mod.description ? `<div class="nz_module_desc">${escapeHTML(mod.description)}</div>` : ''}
          <div class="nz_module_tags">${tags.join('')}</div>
          <div class="nz_module_extra">
            ${libRows}
            ${mod.versionCode ? `<div class="nz_row"><div class="nz_row_label">${escapeHTML(strings.versionCode)}</div><div class="nz_row_value nz_mono">${escapeHTML(mod.versionCode)}</div></div>` : ''}
            <div class="nz_row"><div class="nz_row_label">${escapeHTML(strings.path)}</div><div class="nz_row_value nz_mono">/data/adb/modules/${escapeHTML(mod.id)}</div></div>
            <div style="margin-top: 12px;">
              <button class="nz_btn nz_btn_tonal" data-copy-id="${escapeHTML(mod.id)}">${icon('copy')}${escapeHTML(strings.copyId)}</button>
            </div>
          </div>
        </div>
      </div>
    </div>
  `
}

function render(strings) {
  const list = document.getElementById('modules_list')
  if (!list) return

  const working = modules.filter((mod) => mod.status === 'working').length
  const issues = modules.filter((mod) => ISSUES.includes(mod.status)).length

  document.getElementById('modules_count').textContent = modules.length
    ? [ strings.count.replace('{count}', modules.length), strings.countWorking.replace('{count}', working), issues ? strings.countIssues.replace('{count}', issues) : null ].filter(Boolean).join(' · ')
    : ' '

  document.getElementById('modules_search_box').style.display = modules.length > 3 ? '' : 'none'
  document.getElementById('modules_filter').style.display = modules.length > 1 ? '' : 'none'
  document.querySelectorAll('#modules_filter [data-filter]').forEach((button) => {
    button.setAttribute('aria-pressed', button.getAttribute('data-filter') === filter ? 'true' : 'false')
  })

  if (modules.length === 0) {
    list.innerHTML = `
      <div class="nz_empty">
        ${icon('modules')}
        <div style="font-size: 16px; color: var(--font);">${escapeHTML(strings.notAvaliable)}</div>
        <div style="font-size: 13px;">${escapeHTML(strings.emptyHint)}</div>
      </div>
    `

    return
  }

  const q = query.trim().toLowerCase()
  const filtered = modules
    .filter((mod) => filter === 'all' || (filter === 'working' ? mod.status === 'working' : ISSUES.includes(mod.status)))
    .filter((mod) => !q || [ mod.name, mod.id, mod.author, mod.description ].some((field) => field && field.toLowerCase().includes(q)))

  if (filtered.length === 0) {
    list.innerHTML = `<div class="nz_empty">${icon('search')}<div>${escapeHTML(strings.noResults)}</div></div>`

    return
  }

  list.innerHTML = filtered.map((mod, i) => moduleCard(mod, strings, i)).join('')
}

async function refresh() {
  if (loading) return
  loading = true

  const button = document.getElementById('modules_refresh')
  if (button) button.classList.add('nz_spin')

  try {
    const [ state, strings ] = await Promise.all([ getState(), getStrings('modules') ])
    if (!strings) return

    const next = await getZygiskModules(summarizeState(state))
    const signature = JSON.stringify(next)

    /* INFO: Polling must not replay the card animations when nothing changed. */
    if (signature === lastSignature && document.getElementById('modules_list')?.childElementCount) return

    lastSignature = signature
    modules = next

    if (isActive()) render(strings)
  } finally {
    loading = false

    const btn = document.getElementById('modules_refresh')
    if (btn) setTimeout(() => btn.classList.remove('nz_spin'), 300)
  }
}

export async function loadOnce() {

}

export async function loadOnceView() {
  await refresh()
}

export async function onceViewAfterUpdate() {
  await refresh()
}

export async function load() {
  const strings = await getStrings('modules')

  const refreshButton = document.getElementById('modules_refresh')
  refreshButton.innerHTML = icon('refresh')
  document.getElementById('modules_search_icon').innerHTML = icon('search')

  utils.addListener(refreshButton, 'click', () => {
    haptic()
    refresh()
  })

  const search = document.getElementById('modules_search')
  search.value = query
  utils.addListener(search, 'input', () => {
    query = search.value
    render(strings)
  })

  utils.addListener(document.getElementById('modules_filter'), 'click', (event) => {
    const value = event.target.closest('[data-filter]')?.getAttribute('data-filter')
    if (!value) return

    haptic()
    filter = value
    render(strings)
  })

  utils.addListener(document.getElementById('modules_list'), 'click', (event) => {
    const copyId = event.target.closest('[data-copy-id]')?.getAttribute('data-copy-id')
    if (copyId) {
      event.stopPropagation()
      copyText(copyId, strings.copied)

      return
    }

    const card = event.target.closest('[data-module]')
    if (!card) return

    haptic()

    const id = card.getAttribute('data-module')
    const open = card.getAttribute('data-open') !== 'true'
    card.setAttribute('data-open', open ? 'true' : 'false')

    if (open) openCards.add(id)
    else openCards.delete(id)
  })

  if (modules.length || query) render(strings)

  /* INFO: Live status, re-rendering keeps expanded cards open. */
  stopPolling()
  stopPolling = startPolling(() => refresh(), isActive)
}
