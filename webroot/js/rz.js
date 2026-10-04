/* INFO: Shared data layer for every NextZygisk WebUI page. Batches shell calls,
           de-duplicates in-flight requests and keeps all untrusted strings escaped. */
import { exec, toast } from './kernelsu.js'

export const MODDIR = '/data/adb/modules/rezygisk'
export const STATE_DIR = '/data/adb/rezygisk'
export const STATE_FILE = `${STATE_DIR}/state.json`

export const UMOUNT_DISABLED_FILE = '/data/adb/nextzygisk/umount_disabled'

export async function isUmountDisabled() {
  const result = await run(`[ -f ${UMOUNT_DISABLED_FILE} ] && echo 1 || echo 0`)

  return result.stdout.trim() === '1'
}

export async function setUmountDisabled(disabled) {
  return run(disabled
    ? `mkdir -p /data/adb/nextzygisk && touch ${UMOUNT_DISABLED_FILE}`
    : `rm -f ${UMOUNT_DISABLED_FILE}`)
}

export const LOG_TAGS = [ 'zygisk-core', 'zygiskd', 'zygiskd-companion', 'zygisk-elfutil', 'zygisk-ptrace', 'zygisk-injector' ]
  .flatMap((tag) => [ `${tag}64`, `${tag}32` ])
  .concat([ 'zygisk-sh' ])

/* INFO: Monitor states, mirrors `enum ptracer_tracing_state` in loader/src/ptracer/monitor.c */
export const MONITOR_STATE = {
  TRACING: 0,
  STOPPING: 1,
  STOPPED: 2,
  EXITING: 3
}

const MODULE_ID_REGEX = /^[a-zA-Z][a-zA-Z0-9._-]+$/

export function isSafeModuleId(id) {
  return typeof id === 'string' && MODULE_ID_REGEX.test(id)
}

export function escapeHTML(value) {
  if (value === undefined || value === null) return ''

  return String(value)
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;')
    .replace(/'/g, '&#39;')
}

export function shellQuote(value) {
  return `'${String(value).replace(/'/g, `'\\''`)}'`
}

export async function run(command) {
  try {
    const result = await exec(command)

    return {
      ok: result.errno === 0,
      errno: result.errno,
      stdout: (result.stdout || '').replace(/\r/g, ''),
      stderr: result.stderr || ''
    }
  } catch (error) {
    return { ok: false, errno: -1, stdout: '', stderr: String(error) }
  }
}

/* INFO: Parses `key=value` lines. Values may contain `=`. */
export function parseKeyValue(text) {
  const out = {}

  text.split('\n').forEach((line) => {
    const idx = line.indexOf('=')
    if (idx <= 0) return

    out[line.slice(0, idx).trim()] = line.slice(idx + 1).trim()
  })

  return out
}

let stateInFlight = null
let lastState = null

export function getCachedState() {
  return lastState
}

/* INFO: Reads and parses state.json. Concurrent callers share the same request. */
export function getState() {
  if (stateInFlight) return stateInFlight

  stateInFlight = (async () => {
    const result = await run(`/system/bin/cat ${STATE_FILE}`)
    if (!result.ok) return null

    try {
      lastState = JSON.parse(result.stdout)
    } catch {
      /* INFO: Older monitors could emit a trailing comma after "reason". Try to recover. */
      try {
        lastState = JSON.parse(result.stdout.replace(/,(\s*[}\]])/g, '$1'))
      } catch {
        return null
      }
    }

    return lastState
  })().finally(() => {
    stateInFlight = null
  })

  return stateInFlight
}

/* INFO: Turns state.json into a flat, UI friendly summary. */
export function summarizeState(state) {
  const summary = {
    known: !!state,
    root: state?.root || null,
    monitor: {
      state: state?.monitor?.state !== undefined ? Number(state.monitor.state) : null,
      reason: state?.monitor?.reason || null
    },
    daemons: [],
    zygotes: [],
    modules: [],
    expected: 0,
    working: 0,
    updatedAt: state?.updated_at ? Number(state.updated_at) : null
  }

  if (!state) return summary

  const moduleMap = new Map()

  for (const bits of [ '64', '32' ]) {
    const daemon = state.rezygiskd?.[bits]
    if (daemon) {
      summary.daemons.push({
        bits,
        running: Number(daemon.state) === 1,
        reason: daemon.reason || null,
        modules: Array.isArray(daemon.modules) ? daemon.modules : []
      })

      ;(daemon.modules || []).forEach((id) => {
        if (!moduleMap.has(id)) moduleMap.set(id, [])
        moduleMap.get(id).push(bits)
      })
    }

    /* INFO: Older monitors only wrote zygote["32"] when injected, so a supported
               ABI (daemon entry present) without a zygote entry means "not injected". */
    if ((state.zygote && state.zygote[bits] !== undefined) || daemon) {
      const injected = Number(state.zygote?.[bits]) === 1

      summary.zygotes.push({ bits, injected })
      summary.expected++
      if (injected) summary.working++
    }
  }

  summary.modules = [ ...moduleMap.entries() ].map(([ id, bits ]) => ({ id, bits }))

  return summary
}

/* INFO: overall = 'ok' | 'partial' | 'down' | 'unknown' */
export function overallStatus(summary) {
  if (!summary.known) return 'unknown'
  if (summary.monitor.state !== null && summary.monitor.state !== MONITOR_STATE.TRACING && summary.working === 0) return 'down'
  if (summary.expected === 0 || summary.working === 0) return 'down'

  const daemonsDown = summary.daemons.some((daemon) => !daemon.running)
  if (summary.working === summary.expected && !daemonsDown && summary.monitor.state === MONITOR_STATE.TRACING) return 'ok'

  return 'partial'
}

/* INFO: Reads several props/commands in ONE exec round-trip. */
export async function getDeviceInfo() {
  const command = [
    'echo "model=$(getprop ro.product.model)"',
    'echo "brand=$(getprop ro.product.brand)"',
    'echo "device=$(getprop ro.product.device)"',
    'echo "android=$(getprop ro.build.version.release)"',
    'echo "sdk=$(getprop ro.build.version.sdk)"',
    'echo "patch=$(getprop ro.build.version.security_patch)"',
    'echo "build=$(getprop ro.build.display.id)"',
    'echo "abi=$(getprop ro.product.cpu.abilist)"',
    'echo "kernel=$(uname -r)"',
    'echo "selinux=$(getenforce 2>/dev/null)"',
    'echo "uptime=$(cut -d. -f1 /proc/uptime)"',
    `echo "monitorpid=$(pidof zygisk-ptrace64 zygisk-ptrace32 2>/dev/null)"`,
    `echo "daemonpid=$(pidof zygiskd64 zygiskd32 2>/dev/null)"`
  ].join(' ; ')

  const result = await run(command)
  if (!result.ok && !result.stdout) return {}

  return parseKeyValue(result.stdout)
}

export async function getModuleProp(moduleId = 'rezygisk') {
  if (moduleId !== 'rezygisk' && !isSafeModuleId(moduleId)) return {}

  const result = await run(`/system/bin/cat /data/adb/modules/${moduleId}/module.prop`)
  if (!result.ok) return {}

  return parseKeyValue(result.stdout)
}

/* INFO: Fetches module.prop + flags of many modules in a single exec. */
export async function getModulesDetails(ids) {
  const safeIds = ids.filter(isSafeModuleId)
  if (safeIds.length === 0) return {}

  const command = safeIds.map((id) => {
    const dir = `/data/adb/modules/${id}`

    return `echo "@@MODULE ${id}" ; /system/bin/cat "${dir}/module.prop" 2>/dev/null ; echo ; ` +
      `test -f "${dir}/disable" && echo "@@disabled=1" ; ` +
      `test -f "${dir}/remove" && echo "@@remove=1" ; ` +
      `test -d "${dir}/webroot" && echo "@@webui=1" ; ` +
      `test -f "${dir}/action.sh" && echo "@@action=1" ; true`
  }).join(' ; ')

  const result = await run(command)
  const details = {}

  let current = null
  result.stdout.split('\n').forEach((line) => {
    if (line.startsWith('@@MODULE ')) {
      current = line.slice(9).trim()
      details[current] = {}

      return
    }
    if (!current) return

    const idx = line.indexOf('=')
    if (idx <= 0) return

    details[current][line.slice(0, idx).trim()] = line.slice(idx + 1).trim()
  })

  return details
}

export function formatDuration(totalSeconds, strings) {
  const seconds = Number(totalSeconds)
  if (!Number.isFinite(seconds) || seconds < 0) return strings?.unknown || '?'

  const d = Math.floor(seconds / 86400)
  const h = Math.floor((seconds % 86400) / 3600)
  const m = Math.floor((seconds % 3600) / 60)

  const parts = []
  if (d) parts.push(`${d}d`)
  if (h || d) parts.push(`${h}h`)
  parts.push(`${m}m`)

  return parts.join(' ')
}

export function timeAgo(date, strings) {
  const seconds = Math.max(0, Math.round((Date.now() - date) / 1000))
  if (seconds < 5) return strings?.justNow || 'just now'

  return (strings?.secondsAgo || '{n}s ago').replace('{n}', seconds < 60 ? `${seconds}s` : `${Math.floor(seconds / 60)}m`)
}

export async function copyText(text, successMessage = 'Copied') {
  try {
    await navigator.clipboard.writeText(text)
  } catch {
    const area = document.createElement('textarea')
    area.value = text
    area.style.position = 'fixed'
    area.style.opacity = '0'
    document.body.appendChild(area)
    area.select()

    try {
      document.execCommand('copy')
    } catch {
      toast('Clipboard unavailable')
      area.remove()

      return false
    }

    area.remove()
  }

  toast(successMessage)

  return true
}

/* INFO: Light haptic tick where supported. */
export function haptic(ms = 8) {
  try {
    if (navigator.vibrate) navigator.vibrate(ms)
  } catch {}
}

export function getConfig() {
  try {
    return JSON.parse(localStorage.getItem('/ReZygisk/webui_config') || '{}')
  } catch {
    return {}
  }
}

export function setConfig(patch) {
  const config = { ...getConfig(), ...patch }
  localStorage.setItem('/ReZygisk/webui_config', JSON.stringify(config))

  return config
}

/* INFO: Refresh interval in ms, 0 means disabled. */
export function getRefreshInterval() {
  const value = getConfig().refreshInterval

  return value === undefined ? 5000 : Number(value)
}

/* INFO: Runs `task` periodically while `isActive()` holds and the document is visible.
           Returns a stop function. Automatically stops when the page becomes inactive. */
export function startPolling(task, isActive, interval = getRefreshInterval()) {
  if (!interval) return () => {}

  let stopped = false
  let timer = null

  const tick = async () => {
    if (stopped) return
    if (!isActive()) {
      stopped = true

      return
    }

    if (!document.hidden) {
      try {
        await task()
      } catch (error) {
        console.error('Polling task failed:', error)
      }
    }

    if (!stopped) timer = setTimeout(tick, interval)
  }

  timer = setTimeout(tick, interval)

  return () => {
    stopped = true
    clearTimeout(timer)
  }
}

/* INFO: Material-ish confirm dialog. Resolves to true/false. */
export function confirmDialog({ title, message, confirm = 'OK', cancel = 'Cancel', danger = false }) {
  return new Promise((resolve) => {
    const scrim = document.createElement('div')
    scrim.className = 'nz nz_scrim'
    scrim.innerHTML = `
      <div class="nz_dialog" role="dialog" aria-modal="true">
        <div class="nz_dialog_title">${escapeHTML(title)}</div>
        <div class="nz_dialog_body">${escapeHTML(message)}</div>
        <div class="nz_dialog_actions">
          <button class="nz_btn nz_btn_text" data-act="cancel">${escapeHTML(cancel)}</button>
          <button class="nz_btn ${danger ? 'nz_btn_danger' : 'nz_btn_filled'}" data-act="ok">${escapeHTML(confirm)}</button>
        </div>
      </div>
    `

    const close = (value) => {
      scrim.classList.add('nz_scrim_out')
      setTimeout(() => scrim.remove(), 180)
      resolve(value)
    }

    scrim.addEventListener('click', (event) => {
      const act = event.target.closest('[data-act]')?.getAttribute('data-act')
      if (act === 'ok') return close(true)
      if (act === 'cancel' || event.target === scrim) return close(false)
    })

    document.body.appendChild(scrim)
  })
}

let ptracerPath = null

/* INFO: 32-bit only devices ship zygisk-ptrace32 only. Both talk to the same monitor socket. */
export async function getPtracer() {
  if (ptracerPath) return ptracerPath

  const result = await run(`for b in ${MODDIR}/bin/zygisk-ptrace64 ${MODDIR}/bin/zygisk-ptrace32; do if [ -x "$b" ]; then echo "$b"; break; fi; done`)
  ptracerPath = result.stdout.trim().split('\n')[0] || `${MODDIR}/bin/zygisk-ptrace64`

  return ptracerPath
}

export async function monitorControl(command) {
  if (![ 'start', 'stop', 'exit' ].includes(command)) return { ok: false }

  return run(`${await getPtracer()} ctl ${command}`)
}

/* INFO: Root hiding modules commonly used alongside NextZygisk, matched by module
           name or ID since their IDs differ between forks. `zygisk` marks modules
           that must be loaded by NextZygisk to do their job. */
export const HIDING_MODULES = [
  { key: 'treatwheel', label: 'Treat Wheel', match: /treat[\s_-]*wheel/i, zygisk: true },
  { key: 'shamiko', label: 'Shamiko', match: /shamiko/i, zygisk: true },
  { key: 'assistant', label: 'Zygisk Assistant', match: /zygisk[\s_-]*assistant/i, zygisk: true },
  { key: 'nohello', label: 'NoHello', match: /no[\s_-]*hello/i, zygisk: true },
  { key: 'tricky', label: 'Tricky Store', match: /tricky[\s_-]*store/i, zygisk: false },
  { key: 'pif', label: 'Play Integrity', match: /play[\s_-]*integrity|playintegrityfix/i, zygisk: true },
  { key: 'susfs', label: 'SUSFS', match: /susfs/i, zygisk: false }
]

/* INFO: Lists every installed module (id, name, enabled) in one exec. */
export async function getInstalledModules() {
  const result = await run(
    'for d in /data/adb/modules/*/; do [ -f "$d/module.prop" ] || continue; ' +
    'i=${d%/}; i=${i##*/}; s=1; [ -f "$d/disable" ] && s=0; [ -f "$d/remove" ] && s=0; ' +
    'n=$(grep -m1 "^name=" "$d/module.prop" | cut -d= -f2-); echo "$i|$s|$n"; done'
  )

  return result.stdout.split('\n').map((line) => {
    const [ id, enabled, ...name ] = line.split('|')
    if (!id || !isSafeModuleId(id)) return null

    return { id, enabled: enabled === '1', name: name.join('|').trim() || id }
  }).filter(Boolean)
}

export function detectHidingModules(installed, loadedIds = []) {
  const found = []

  for (const known of HIDING_MODULES) {
    const mod = installed.find((m) => known.match.test(m.name) || known.match.test(m.id))
    if (!mod) continue

    found.push({ ...known, id: mod.id, name: mod.name, enabled: mod.enabled, loaded: loadedIds.includes(mod.id) })
  }

  return found
}

/* INFO: Zygisk ABI names, mirrors ARCH_STR used by zygiskd to pick zygisk/<abi>.so */
function abiForBits(bits, abilist) {
  if (bits === '64') return abilist.includes('x86_64') ? 'x86_64' : 'arm64-v8a'

  /* INFO: zygiskd is native: on x86 devices (even with ARM translation) it is x86. */
  return abilist.includes('x86') ? 'x86' : 'armeabi-v7a'
}

/* INFO: Every installed module shipping a zygisk/ folder, with the reason it is or
           is not working. Uses one exec for all modules. */
export async function getZygiskModules(summary) {
  const result = await run(
    'echo "@@abilist=$(getprop ro.product.cpu.abilist)"; ' +
    'for d in /data/adb/modules/*/; do i=${d%/}; i=${i##*/}; [ -d "$d/zygisk" ] || continue; [ "$i" = rezygisk ] && continue; ' +
    'echo "@@MODULE $i"; cat "$d/module.prop" 2>/dev/null; echo; ' +
    '[ -f "$d/disable" ] && echo "@@disabled=1"; [ -f "$d/remove" ] && echo "@@remove=1"; ' +
    '[ -d "/data/adb/modules_update/$i" ] && echo "@@update=1"; ' +
    '[ -d "$d/webroot" ] && echo "@@webui=1"; [ -f "$d/action.sh" ] && echo "@@action=1"; ' +
    'echo "@@libs=$(ls "$d/zygisk" 2>/dev/null | grep "\\.so$" | tr "\\n" " ")"; done; ' +
    /* INFO: First installs live in modules_update until the next boot. */
    'for d in /data/adb/modules_update/*/; do i=${d%/}; i=${i##*/}; [ -d "$d/zygisk" ] || continue; [ "$i" = rezygisk ] && continue; ' +
    '[ -d "/data/adb/modules/$i" ] && continue; echo "@@MODULE $i"; cat "$d/module.prop" 2>/dev/null; echo; echo "@@new=1"; ' +
    'echo "@@libs=$(ls "$d/zygisk" 2>/dev/null | grep "\\.so$" | tr "\\n" " ")"; done'
  )

  let abilist = []
  const props = {}
  let current = null

  result.stdout.split('\n').forEach((line) => {
    if (line.startsWith('@@abilist=')) {
      abilist = line.slice(10).split(',').map((abi) => abi.trim()).filter(Boolean)

      return
    }

    if (line.startsWith('@@MODULE ')) {
      current = line.slice(9).trim()
      props[current] = {}

      return
    }
    if (!current) return

    const idx = line.indexOf('=')
    if (idx <= 0) return

    props[current][line.slice(0, idx).trim()] = line.slice(idx + 1).trim()
  })

  const loaded = new Map(summary.modules.map((mod) => [ mod.id, mod.bits ]))

  /* INFO: Modules loaded by the daemon but without zygisk/ in the scan (should not
             happen) are still listed, so nothing the daemon reports is hidden. */
  for (const id of loaded.keys()) {
    if (!props[id] && isSafeModuleId(id)) props[id] = { '@@libs': '' }
  }

  return Object.entries(props).filter(([ id ]) => isSafeModuleId(id)).map(([ id, prop ]) => {
    const libs = (prop['@@libs'] || '').split(' ').map((lib) => lib.replace(/\.so$/, '')).filter(Boolean)
    const loadedBits = loaded.get(id) || []

    /* INFO: For every ABI a daemon runs for, does the module ship a library? */
    const targets = summary.daemons.map((daemon) => {
      const abi = abiForBits(daemon.bits, abilist)

      return {
        bits: daemon.bits,
        abi,
        hasLib: libs.includes(abi),
        daemonRunning: daemon.running,
        loaded: loadedBits.includes(daemon.bits)
      }
    })

    let status = 'unknown'
    let reason = null

    if (prop['@@new'] === '1') {
      status = 'pending'
      reason = { key: 'installed' }
    } else if (prop['@@remove'] === '1') status = 'removal'
    else if (prop['@@disabled'] === '1') status = 'disabled'
    else if (!summary.known) status = 'unknown'
    else {
      const expected = targets.filter((target) => target.hasLib)
      const working = expected.filter((target) => target.loaded)

      if (expected.length === 0) {
        status = 'notLoaded'
        reason = { key: 'noLib', abi: targets.map((target) => target.abi).join(', ') }
      } else if (working.length === expected.length) {
        status = 'working'
      } else {
        const missing = expected.filter((target) => !target.loaded)

        status = working.length ? 'partial' : 'notLoaded'

        if (missing.some((target) => !target.daemonRunning)) reason = { key: 'daemonDown', bits: missing.map((t) => t.bits).join('/') }
        else if (prop['@@update'] === '1') reason = { key: 'update' }
        else reason = { key: 'failed', bits: missing.map((t) => t.bits).join('/') }
      }
    }

    return {
      id,
      name: prop.name || id,
      version: prop.version || null,
      versionCode: prop.versionCode || null,
      author: prop.author || null,
      description: prop.description || null,
      webui: prop['@@webui'] === '1',
      action: prop['@@action'] === '1',
      updatePending: prop['@@update'] === '1',
      libs,
      targets,
      loadedBits,
      status,
      reason
    }
  }).sort((a, b) => {
    /* INFO: Problems first, then by name */
    const rank = { notLoaded: 0, partial: 1, pending: 2, unknown: 3, working: 4, removal: 5, disabled: 6 }

    return (rank[a.status] - rank[b.status]) || a.name.localeCompare(b.name)
  })
}
