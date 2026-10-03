import { whichCurrentPage } from '../navbar.js'
import { getStrings } from '../pageLoader.js'
import utils from '../utils.js'
import { icon } from '../../icons.js'
import { copyText, escapeHTML, getModulesDetails, getState, haptic, summarizeState } from '../../rz.js'

let modules = []
let query = ''
let loading = false

function isActive() {
  return whichCurrentPage() === 'modules'
}

function hueOf(text) {
  let hash = 0
  for (let i = 0; i < text.length; i++) hash = (hash * 31 + text.charCodeAt(i)) | 0

  return Math.abs(hash) % 360
}

function moduleCard(mod, strings, index) {
  const name = mod.name || mod.id
  const initial = name.trim().charAt(0).toUpperCase() || '?'

  const tags = mod.bits.map((bits) => `<span class="nz_pill">${escapeHTML(bits)}-bit</span>`)
  if (mod.disabled) tags.push(`<span class="nz_pill nz_pill_warn">${escapeHTML(strings.disabled)}</span>`)
  if (mod.remove) tags.push(`<span class="nz_pill nz_pill_err">${escapeHTML(strings.pendingRemoval)}</span>`)
  if (mod.webui) tags.push(`<span class="nz_pill nz_pill_muted">WebUI</span>`)
  if (mod.action) tags.push(`<span class="nz_pill nz_pill_muted">Action</span>`)

  const meta = [ mod.version, mod.author && `${strings.by} ${mod.author}` ].filter(Boolean).join(' · ')

  return `
    <div class="nz_card nz_card_tap" data-module="${escapeHTML(mod.id)}" data-open="false" style="animation-delay: ${Math.min(index, 12) * 30}ms;">
      <div class="nz_module">
        <div class="nz_avatar" style="--hue: ${hueOf(mod.id)};">${escapeHTML(initial)}</div>
        <div class="nz_module_body">
          <div class="nz_module_name">${escapeHTML(name)}</div>
          <div class="nz_module_meta">${escapeHTML(meta || mod.id)}</div>
          ${mod.description ? `<div class="nz_module_desc">${escapeHTML(mod.description)}</div>` : ''}
          <div class="nz_module_tags">${tags.join('')}</div>
          <div class="nz_module_extra">
            <div class="nz_row"><div class="nz_row_label">ID</div><div class="nz_row_value nz_mono">${escapeHTML(mod.id)}</div></div>
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

  document.getElementById('modules_count').textContent = modules.length
    ? strings.count.replace('{count}', modules.length)
    : ' '

  document.getElementById('modules_search_box').style.display = modules.length > 3 ? '' : 'none'

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
  const filtered = q
    ? modules.filter((mod) => [ mod.name, mod.id, mod.author, mod.description ].some((field) => field && field.toLowerCase().includes(q)))
    : modules

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

    const summary = summarizeState(state)
    const details = await getModulesDetails(summary.modules.map((mod) => mod.id))

    modules = summary.modules.map((mod) => {
      const prop = details[mod.id] || {}

      return {
        id: mod.id,
        bits: mod.bits,
        name: prop.name || null,
        version: prop.version || null,
        versionCode: prop.versionCode || null,
        author: prop.author || null,
        description: prop.description || null,
        disabled: prop['@@disabled'] === '1',
        remove: prop['@@remove'] === '1',
        webui: prop['@@webui'] === '1',
        action: prop['@@action'] === '1'
      }
    }).sort((a, b) => (a.name || a.id).localeCompare(b.name || b.id))

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
    card.setAttribute('data-open', card.getAttribute('data-open') === 'true' ? 'false' : 'true')
  })

  if (modules.length || query) render(strings)
}
