import { exec, toast } from '../../kernelsu.js'

import { whichCurrentPage } from '../navbar.js'
import { getStrings } from '../pageLoader.js'

async function _getReZygiskState() {
  let stateCmd = await exec('/system/bin/cat /data/adb/rezygisk/state.json')
  if (stateCmd.errno !== 0) {
    toast('Error getting state of ReZygisk!')

    return;
  }

  try {
    const ReZygiskState = JSON.parse(stateCmd.stdout)
    return ReZygiskState
  } catch {
    return null;
  }
}

async function _getVersion() {
  let moduleProp = await exec('cat /data/adb/modules/rezygisk/module.prop')
  if (moduleProp.errno !== 0) {
    toast('Error getting state of ReZygisk!')

    return;
  }

  let version = '???'
  moduleProp.stdout.split('\n').forEach((line) => {
    if (line.startsWith('version=')) version = line.split('=')[1]
  })

  return version
}

async function _getKernelString() {
  const unameCmd = await exec('/system/bin/uname -r')
  if (unameCmd.errno !== 0) {
    toast('Error getting kernel version!')
    return '???'
  }

  if (unameCmd.stdout && unameCmd.stdout.length !== 0) {
    return unameCmd.stdout.trim()
  } else {
    return '???'
  }
}

async function _getAndroidVersion() {
  const androidVersionCmd = await exec('/system/bin/getprop ro.build.version.release')
  if (androidVersionCmd.errno !== 0) {
    toast('Error getting android version!')
    return '???'
  }

  if (androidVersionCmd.stdout && androidVersionCmd.stdout.length !== 0) {
    return androidVersionCmd.stdout
  } else {
    return '???'
  }
}


export async function loadOnce() {
}

export async function loadOnceView() {
}

export async function onceViewAfterUpdate() {
}

export async function load() {
  let rzState = {
    actuallyWorking: 0,
    expectedWorking: 0
  }

  const rootCss = document.querySelector(':root')
  const rz_state = document.getElementById('rz_state')
  const rz_icon_state = document.getElementById('rz_icon_state')

  const zygote_divs = [
    document.getElementById('zygote64'),
    document.getElementById('zygote32')
  ]

  const zygote_status_divs = [
    document.getElementById('zygote64_status'),
    document.getElementById('zygote32_status')
  ]

  document.getElementById('version_code').innerHTML = await _getVersion()

  /* INFO: Just ensure that they won't appear unless there's info */
  zygote_divs.forEach((zygote_div) => {
    zygote_div.style.display = 'none'
  })

  document.getElementById('kernel_version_div').innerHTML = await _getKernelString()
  document.getElementById('android_version_div').innerHTML = await _getAndroidVersion()

  const ReZygiskState = await _getReZygiskState()

  const strings = await getStrings(whichCurrentPage())

  let root_impl = ReZygiskState.root
  if (!root_impl) root_impl = strings.unknown
  if (root_impl === 'Multiple') root_impl = strings.rootImpls.multiple

  document.getElementById('root_impl').innerHTML = root_impl

  if (ReZygiskState == null) {
    rz_state.innerHTML = strings.unknown
    rz_icon_state.innerHTML = '<img class="brightc" src="assets/mark.svg">'
    document.getElementById('zygote_class').style.display = 'none'
    /* INFO: This hides the throbber screen */
    loading_screen.style.display = 'none'

    return;
  }

  rzState.expectedWorking = (ReZygiskState.zygote['64'] !== undefined ? 1 : 0) + (ReZygiskState.zygote['32'] !== undefined ? 1 : 0)

  if (ReZygiskState.zygote['64'] !== undefined) {
    const zygote64 = ReZygiskState.zygote['64']

    zygote_divs[0].style.display = 'block'

    switch (zygote64) {
      case 1: {
        zygote_status_divs[0].innerHTML = strings.info.zygote.injected

        rzState.actuallyWorking++

        break
      }
      case 0: zygote_status_divs[0].innerHTML = strings.info.zygote.notInjected; break
      default: zygote_status_divs[0].innerHTML = strings.info.zygote.unknown
    }
  }

  if (ReZygiskState.zygote['32'] !== undefined) {
    const zygote32 = ReZygiskState.zygote['32']

    zygote_divs[1].style.display = 'block'

    switch (zygote32) {
      case 1: {
        zygote_status_divs[1].innerHTML = strings.info.zygote.injected

        rzState.actuallyWorking++

        break
      }
      case 0: zygote_status_divs[1].innerHTML = strings.info.zygote.notInjected; break
      default: zygote_status_divs[1].innerHTML = strings.info.zygote.unknown
    }
  }

  if (rzState.expectedWorking === 0 || rzState.actuallyWorking === 0) {
    rz_state.innerHTML = strings.status.notWorking
    document.getElementById('zygote_class').style.display = 'none'
  } else if (rzState.expectedWorking === rzState.actuallyWorking) {
    rz_state.innerHTML = strings.status.ok

    rootCss.style.setProperty('--bright', '#545454')
    rz_icon_state.innerHTML = '<img class="brightc" src="assets/tick.svg">'
  } else {
    rz_state.innerHTML = strings.status.partially

    rootCss.style.setProperty('--bright', '#766000')
    rz_icon_state.innerHTML = '<img class="brightc" src="assets/warn.svg">'
  }

  /* INFO: This hides the throbber screen */
  loading_screen.style.display = 'none'
    }
