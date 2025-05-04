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

async function getModuleNames(modules) {
  const fullCommand = modules.map((mod) => {
    let propPath = `/data/adb/modules/${mod.id}/module.prop`

    return `printf % ; if test -f "${propPath}"; then /system/bin/grep '^name=' "${propPath}" | /system/bin/cut -d '=' -f 2- 2>/dev/null || true; else true; fi ; printf "\\n"`
  }).join(' ; ')

  const result = await exec(fullCommand)
  if (result.errno !== 0) {
    setError('getModuleNames', 'Failed to execute command to retrieve module list names')

    return null
  }

  return result.stdout.split('\n\n')
}

(async () => {
  /* INFO: Test ksu module availability */
  exec('echo "Hello world!"')
    .then(() => console.log('[kernelsu.js] Package is ready!'))
    .catch(err => {
      console.log('[kernelsu.js] Package is not ready! Below is error:')
      console.error(err)
    })

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

  const version = document.getElementById('version')
  const root_impl = document.getElementById('root_impl')

  const monitor_status = document.getElementById('monitor_status')

  const zygote_divs = [
    document.getElementById('zygote64'),
    document.getElementById('zygote32')
  ]

  const zygote_status_divs = [
    document.getElementById('zygote64_status'),
    document.getElementById('zygote32_status')
  ]

  const androidVersionCmd = await exec('/system/bin/getprop ro.build.version.release')
  if (androidVersionCmd.errno !== 0) return setError('WebUI', androidVersionCmd.stderr)

  document.getElementById('android_version_div').innerHTML = androidVersionCmd.stdout
  console.log('[rezygisk.js] Android version: ', androidVersionCmd.stdout)

  const unameCmd = await exec('/system/bin/uname -r')
  if (unameCmd.errno !== 0) return setError('WebUI', unameCmd.stderr)

  document.getElementById('kernel_version_div').innerHTML = unameCmd.stdout
  console.log('[rezygisk.js] Kernel version: ', unameCmd.stdout)

  const catCmd = await exec('/system/bin/cat /data/adb/rezygisk/module.prop')
  console.log(`[rezygisk.js] ReZygisk module infomation:\n${catCmd.stdout}`)

  let expectedWorking = 0
  let actuallyWorking = 0

  const ReZygiskInfo = {
    rootImpl: null,
    monitor: null,
    zygotes: [],
    daemons: []
  }

  if (catCmd.errno === 0) {
    /* INFO: Just ensure that they won't appear unless there's info */
    zygote_divs.forEach((zygote_div) => {
      zygote_div.style.display = 'none'
    })

    version.innerHTML = catCmd.stdout.split('\n').find((line) => line.startsWith('version=')).substring('version='.length).trim()

    let moduleInfo = catCmd.stdout.split('\n').find((line) => line.startsWith('description=')).substring('description='.length).split('[')[1].split(']')[0]

    const daemonModules = []
    moduleInfo.match(/\(([^)]+)\)/g).forEach((area) => {
      moduleInfo = moduleInfo.replace(area, ',')

      const info = area.substring(1, area.length - 1).split(', ')

      const rootImpl = info[0].substring('Root: '.length)

      info[1] = info[1].substring('Modules: '.length)
      const modules = info.slice(1, info.length)

      ReZygiskInfo.rootImpl = rootImpl
      if (modules[0] !== 'None') daemonModules.push(modules)
    })

    const infoArea = moduleInfo.split(', ')
    infoArea.forEach((info) => {
      if (info.startsWith('monitor:')) {
        ReZygiskInfo.monitor = info.substring('monitor: X '.length).trim()
      }

      if (info.startsWith('zygote')) {
        ReZygiskInfo.zygotes.push({
          bits: info.substring('zygote'.length, 'zygote'.length + 'XX'.length),
          state: info.substring('zygoteXX: X '.length).trim()
        })
      }

      if (info.startsWith('daemon')) {
        ReZygiskInfo.daemons.push({
          bits: info.substring('daemon'.length, 'daemon'.length + 'XX'.length),
          state: info.substring('daemonXX: X '.length).trim(),
          modules: daemonModules[ReZygiskInfo.daemons.length] || []
        })
      }
    })

    switch (ReZygiskInfo.monitor) {
      case 'tracing': monitor_status.innerHTML = translations.page.actions.status.tracing; break;
      case 'stopping': monitor_status.innerHTML = translations.page.actions.status.stopping; break;
      case 'stopped': monitor_status.innerHTML = translations.page.actions.status.stopped; break;
      case 'exiting': monitor_status.innerHTML = translations.page.actions.status.exiting; break;
      default: monitor_status.innerHTML = translations.page.actions.status.unknown;
    }

    expectedWorking = ReZygiskInfo.zygotes.length

    for (let i = 0; i < ReZygiskInfo.zygotes.length; i++) {
      const zygote = ReZygiskInfo.zygotes[i]
      /* INFO: Not used ATM */
      /* const daemon = ReZygiskInfo.daemons[i] */

      const zygoteDiv = zygote_divs[zygote.bits === '64' ? 0 : 1]
      const zygoteStatusDiv = zygote_status_divs[zygote.bits === '64' ? 0 : 1]

      zygoteDiv.style.display = 'block'

      switch (zygote.state) {
        case 'injected': {
          zygoteStatusDiv.innerHTML = translations.page.home.info.zygote.injected;

          actuallyWorking++

          break;
        }
        case 'not injected': zygoteStatusDiv.innerHTML = translations.page.home.info.zygote.notInjected; break;
        default: zygoteStatusDiv.innerHTML = translations.page.home.info.zygote.unknown;
      }
    }
  }

  if (expectedWorking === 0 || actuallyWorking === 0) {
    rezygisk_state.innerHTML = translations.page.home.status.notWorking
  } else if (expectedWorking === actuallyWorking) {
    rezygisk_state.innerHTML = translations.page.home.status.ok

    rootCss.style.setProperty('--bright', '#3a4857')
    rezygisk_icon_state.innerHTML = '<img class="brightc" src="assets/tick.svg">'
  } else {
    rezygisk_state.innerHTML = translations.page.home.status.partially

    rootCss.style.setProperty('--bright', '#766000')
    rezygisk_icon_state.innerHTML = '<img class="brightc" src="assets/warn.svg">'
  }

  if (ReZygiskInfo.rootImpl)
    root_impl.innerHTML = ReZygiskInfo.rootImpl

  const all_modules = []
  ReZygiskInfo.daemons.forEach((daemon) => {
    daemon.modules.forEach((module_id) => {
      const module = all_modules.find((mod) => mod.id === module_id)

      if (module) {
        module.bitsUsed.push(daemon.bits)
      } else {
        all_modules.push({
          id: module_id,
          name: null,
          bitsUsed: [ daemon.bits ]
        })
      }
    })
  })

  if (all_modules.length !== 0) {
    document.getElementById('modules_list_not_avaliable').style.display = 'none'

    const module_names = await getModuleNames(all_modules)
    module_names.forEach((module_name, i) => all_modules[i].name = module_name)

    console.log(`[rezygisk.js] Module list:`)
    console.log(all_modules)

    const modules_list = document.getElementById('modules_list')

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
  
  }

  /* INFO: This hides the throbber screen */
  loading_screen.style.display = 'none'
  bottom_nav.style.display = 'flex'


  const start_time = Number(localStorage.getItem('/system/boot-time'))
  console.log('[rezygisk.js] boot time: ', Date.now() - start_time, 'ms')
  localStorage.removeItem('/system/boot_time')
})().catch((err) => setError('WebUI', err.stack ? err.stack : err.message))
