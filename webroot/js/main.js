import { fullScreen, exec, toast } from './kernelsu.js'

import { setNewLanguage, getTranslations } from './language.js'

export function setError(place, issue) {
  const fullErrorLog = setErrorData(`${place}: ${issue}`)
  document.getElementById('errorh_panel').innerHTML = fullErrorLog
  toast(`${place}: ${issue}`)
}

export function setLangData(mode) {
  localStorage.setItem('/system/language', mode)

  return localStorage.getItem('/system/language')
}

export function setErrorData(errorLog) {
  const getPrevious = localStorage.getItem('/system/error')
  const finalLog = getPrevious && getPrevious.length !== 0 ? getPrevious + `\n` + errorLog : errorLog

  localStorage.setItem('/system/error', finalLog)
  return finalLog
}

(async () => {
  const EXPECTED = 1
  const UNEXPECTED_FAIL = 2

  fullScreen(true)

  let sys_lang = localStorage.getItem('/system/language')

  if (!sys_lang) sys_lang = setLangData('en_US')
  if (sys_lang !== 'en_US') await setNewLanguage(sys_lang, true)

  const translations = await getTranslations(sys_lang)

  const loading_screen = document.getElementById('loading_screen')
  const bottom_nav = document.getElementById('navbar_support_div')

  const rootCss = document.querySelector(':root')

  const rezygisk_state = document.getElementById('rezygisk_state')
  const rezygisk_icon_state = document.getElementById('rezygisk_icon_state')

  const code_version = document.getElementById('version_code')
  const root_impl = document.getElementById('root_impl')

  const zygote64_div = document.getElementById('zygote64')
  const zygote32_div = document.getElementById('zygote32')

  const monitor_status = document.getElementById('monitor_status')

  const zygote32_status_div = document.getElementById('zygote32_status')
  const zygote64_status_div = document.getElementById('zygote64_status')

  const modules_32 = []
  const modules_64 = []

  let zygote64_status = EXPECTED
  let zygote32_status = EXPECTED

  const lscpuCmd = await exec('/system/bin/getprop ro.product.cpu.abilist')
  if (lscpuCmd.errno !== 0) return setError('WebUI', lscpuCmd.stderr)

  const has64BitSupport = lscpuCmd.stdout.includes('arm64-v8a') || lscpuCmd.stdout.includes('x86_64')
  const has32BitSupport = lscpuCmd.stdout.includes('armeabi-v7a') || lscpuCmd.stdout.includes('armeabi') || lscpuCmd.stdout.includes('x86')

  if (!has64BitSupport) {
    zygote64_div.style.display = 'none'
  }

  if (!has32BitSupport) {
    zygote32_div.style.display = 'none'
  }

  const catCmd = await exec('/system/bin/cat /data/adb/rezygisk/status')

  if (catCmd.errno === 0) {
    const [ Version, Tracing, Daemon64, Zygote64 ] = catCmd.stdout.split('\n')
    let hasOffset = false
    /* TODO: Show if daemon is running */

    code_version.innerHTML = Version.split(': ')[1]

    const tracingState = Tracing.split(': ')[1].split(' ')[0]
    switch (tracingState) {
      case '1': monitor_status.innerHTML = translations.page.actions.status.tracing; break;
      case '2': monitor_status.innerHTML = translations.page.actions.status.stopping; break;
      case '3': monitor_status.innerHTML = translations.page.actions.status.stopped; break;
      case '4': monitor_status.innerHTML = translations.page.actions.status.exiting; break;
      default: monitor_status.innerHTML = translations.page.actions.status.unknown;
    }

    if (has64BitSupport && Daemon64 && Daemon64.startsWith('Daemon64:')) {
      hasOffset = true

      let daemon64_status = Daemon64.split(': ').slice(1).join(': ')
      let daemon64_info = null
      if (daemon64_status.split(' ')[1]) {
        daemon64_info = daemon64_status.split(' ').slice(1).join(' ')
        daemon64_status = daemon64_status.split(' ')[0]

        root_impl.innerHTML = daemon64_info.split('Root: ')[1].split(',')[0]
        
        const modules = daemon64_info.split('Modules: ')[1].split(')')[0].split(', ')
        if (modules[0] !== 'None') modules_64.push(...modules)
      }

      const zygote64_injection_status = Zygote64.split(': ')[1]

      /* TODO: add handling for unknown status */
      if (zygote64_injection_status === 'injected') {
        zygote64_status_div.innerHTML = translations.page.home.info.zygote.injected
      } else if (zygote64_injection_status === 'not injected') {
        zygote64_status_div.innerHTML = translations.page.home.info.zygote.notInjected

        zygote64_status = UNEXPECTED_FAIL
      } else {
        zygote64_status_div.innerHTML = translations.page.home.info.zygote.unknown

        zygote64_status = UNEXPECTED_FAIL
      }
    } else if (has64BitSupport && (!Daemon64 || !Daemon64.startsWith('Daemon64:'))) {
      zygote64_div.style.display = 'none'

      zygote64_status = UNEXPECTED_FAIL
    }

    if (has32BitSupport) {
      let Daemon32 = null
      let Zygote32 = null

      if (hasOffset) {
        Daemon32 = catCmd.stdout.split('\n')[4]
        Zygote32 = catCmd.stdout.split('\n')[5]
      } else {
        Daemon32 = catCmd.stdout.split('\n')[2]
        Zygote32 = catCmd.stdout.split('\n')[3]
      }

      if (Daemon32 && Daemon32.startsWith('Daemon32:')) {
        /* INFO: Daemon32 is supported */
        let daemon32_status = Daemon32.split(': ').slice(1).join(': ')
        let daemon32_info = null
        if (daemon32_status.split(' ')[1]) {
          daemon32_info = daemon32_status.split(' ').slice(1).join(' ')
          daemon32_status = daemon32_status.split(' ')[0]

          root_impl.innerHTML = daemon32_info.split('Root: ')[1].split(',')[0]

          const modules = daemon32_info.split('Modules: ')[1].split(')')[0].split(', ')
          if (modules[0] !== 'None') modules_32.push(...modules)
        }

        const zygote32_injection_status = Zygote32.split(': ')[1]

        if (zygote32_injection_status === 'injected') {
          zygote32_status_div.innerHTML = translations.page.home.info.zygote.injected
        } else if (zygote32_injection_status === 'not injected') {
          zygote32_status_div.innerHTML = translations.page.home.info.zygote.notInjected

          zygote32_status = UNEXPECTED_FAIL
        } else {
          zygote32_status_div.innerHTML = translations.page.home.info.zygote.unknown

          zygote32_status = UNEXPECTED_FAIL
        }
      } else {
        zygote32_div.style.display = 'none'

        zygote32_status = UNEXPECTED_FAIL
      }
    }
  }

  if (zygote32_status === EXPECTED && zygote64_status === EXPECTED) {
    rezygisk_state.innerHTML = translations.page.home.status.ok

    rootCss.style.setProperty('--bright', '#3a4857')
    rezygisk_icon_state.innerHTML = '<img class="brightc" src="assets/tick.svg">'
  } else if (zygote64_status === EXPECTED ^ zygote32_status.innerHTML === EXPECTED) {
    rezygisk_state.innerHTML = translations.page.home.status.partially

    rootCss.style.setProperty('--bright', '#766000')
    rezygisk_icon_state.innerHTML = '<img class="brightc" src="assets/warn.svg">'
  } else {
    rezygisk_state.innerHTML = translations.page.home.status.notWorking
  }

  const all_modules = []

  modules_64.forEach((module) => all_modules.push({
    name: module,
    bitsUsed: [ '64 bit' ]
  }))

  modules_32.forEach((module) => {
    const module_index = all_modules.findIndex((module_64_32) => module_64_32.name === module)

    if (module_index !== -1) all_modules[module_index].bitsUsed.push('32 bit')
    else all_modules.push({
      name: module,
      bitsUsed: [ '32 bit' ]
    })
  })

  if (all_modules.length !== 0)
    document.getElementById('modules_list_not_avaliable').style.display = 'none'

  const modules_list = document.getElementById('modules_list')

  /* INFO: This hides the throbber screen */
  loading_screen.style.display = 'none'
  bottom_nav.style.display = 'flex'

  all_modules.forEach((module) => {
    modules_list.innerHTML += 
      `<div class="dim card" style="padding: 25px 15px; cursor: pointer;">
        <div class="dimc" style="font-size: 1.1em;">${module.name}</div>
        <div class="dimc desc" style="font-size: 0.9em; margin-top: 3px; white-space: nowrap; align-items: center; display: flex;">
          <div class="dimc arch_desc">${translations.page.modules.arch}</div>
          <div class="dimc" style="margin-left: 5px;">${module.bitsUsed.join(' / ')}</div>
        </div>
      </div>`
  })
})().catch((err) => setError('WebUI', err.stack ? err.stack : err.message))
