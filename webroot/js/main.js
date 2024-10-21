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

  const rootCss = document.querySelector(':root')

  const rezygisk_state = document.getElementById('rezygisk_state')
  const rezygisk_icon_state = document.getElementById('rezygisk_icon_state')

  const code_version = document.getElementById('version_code')
  const root_impl = document.getElementById('root_impl')

  const zygote64_div = document.getElementById('zygote64')
  const zygote32_div = document.getElementById('zygote32')

  const daemon64_div = document.getElementById('daemon64')
  const daemon32_div = document.getElementById('daemon32')

  const zygote32_status_div = document.getElementById('zygote32_status')
  const zygote64_status_div = document.getElementById('zygote64_status')

  const modules_32 = []
  const modules_64 = []

  let zygote64_status = EXPECTED
  let zygote32_status = EXPECTED

  const ptrace64Cmd = await exec('/data/adb/modules/zygisksu/bin/zygisk-ptrace64 info')

  if (ptrace64Cmd.errno === 0) {
    const [ version_line, _, process_line, daemon_running_or_root_impl_line, daemon_running_line, zygote_injected, tracing_state, module_amount_line ] = ptrace64Cmd.stdout.split('\n')
    let modules_amount = module_amount_line.split(': ')[1]

    /* INFO: Root implementation and ReZygisk version parsing */
    if (daemon_running_line) {
      code_version.innerHTML = version_line.split('Tracer ')[1].split('-')[0]
      root_impl.innerHTML = daemon_running_or_root_impl_line.split(': ')[1]

      if (daemon_running_line.split(': ')[1] === 'yes') {
        zygote64_status_div.innerHTML = translations.page.home.info.zygote.injected
      } else {
        zygote64_status_div.innerHTML = translations.page.home.info.zygote.notInjected

        zygote64_status = UNEXPECTED_FAIL
      }
    } else {
      zygote64_status_div.innerHTML = translations.page.home.info.zygote.notInjected

      zygote64_status = UNEXPECTED_FAIL
    }

    const lines = ptrace64Cmd.stdout.split('\n')

    if (modules_amount !== 'N/A') {
      modules_amount = parseInt(modules_amount)

      for (let i = 0; i < modules_amount; i++) {
        const module = lines[6 + i].split(' - ')[1]

        modules_32.push(module)
      }

      document.getElementById('modules_list_not_avaliable').style.display = 'none'
    }
  } else if (ptrace64Cmd.stderr.includes('cannot execute binary file: Exec format error')) {
    zygote64_div.style.display = 'none'
    daemon64_div.style.display = 'none'
  } else {
    setError('ptrace64', `Error while executing zygisk-ptrace64 (${ptrace64Cmd.errno}): ${ptrace64Cmd.stderr}`)

    zygote64_status = UNEXPECTED_FAIL
  }

  const ptrace32Cmd = await exec('/data/adb/modules/zygisksu/bin/zygisk-ptrace32 info')

  if (ptrace32Cmd.errno === 0) {
    const [ version_line, _, process_line, daemon_running_or_root_impl_line, daemon_running_line, zygote_injected, tracing_state, module_amount_line ] = ptrace32Cmd.stdout.split('\n')
    let modules_amount = module_amount_line.split(': ')[1]

    /* INFO: Root implementation and ReZygisk version parsing -- Necessary if 64-bit fails */
    if (daemon_running_line) {
      code_version.innerHTML = version_line.split('Tracer ')[1].split('-')[0]
      root_impl.innerHTML = daemon_running_or_root_impl_line.split(': ')[1]

      if (daemon_running_line.split(': ')[1] === 'yes') {
        zygote32_status_div.innerHTML = translations.page.home.info.zygote.injected
      } else {
        zygote32_status_div.innerHTML = translations.page.home.info.zygote.notInjected

        zygote32_status = UNEXPECTED_FAIL
      }
    } else {
      zygote32_status_div.innerHTML = translations.page.home.info.zygote.notInjected

      zygote32_status = UNEXPECTED_FAIL
    }

    const lines = ptrace32Cmd.stdout.split('\n')

    if (modules_amount !== 'N/A') {
      modules_amount = parseInt(modules_amount)

      for (let i = 0; i < modules_amount; i++) {
        const module = lines[6 + i].split(' - ')[1]

        modules_64.push(module)
      }

      document.getElementById('modules_list_not_avaliable').style.display = 'none'
    }
  } else if (ptrace32Cmd.stderr.includes('not executable: 32-bit ELF file')) {
    zygote32_div.style.display = 'none'
    daemon32_div.style.display = 'none'
  } else {
    setError('ptrace32', `Error while executing zygisk-ptrace32 (${ptrace32Cmd.errno}): ${ptrace32Cmd.stderr}`)

    zygote32_status = UNEXPECTED_FAIL
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

  const modules_64_32 = []

  modules_64.forEach((module) => modules_64_32.push({
    name: module,
    bitsUsed: [ '64 bit' ]
  }))

  modules_32.forEach((module) => {
    const module_index = modules_64_32.findIndex((module_64_32) => module_64_32.name === module)

    if (module_index !== -1) modules_64_32[module_index].bitsUsed.push('32 bit')
    else modules_64_32.push({
      name: module,
      bitsUsed: [ '32 bit' ]
    })
  })

  const modules_list = document.getElementById('modules_list')

  /* INFO: This hides the throbber screen */
  loading_screen.style.display = 'none'

  modules_64_32.forEach((module) => {
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
