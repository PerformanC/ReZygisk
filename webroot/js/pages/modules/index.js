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

async function _getModuleNames(modules) {
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

export async function loadOnce() {

}

export async function loadOnceView() {

}

export async function onceViewAfterUpdate() {

}

export async function load() {
  const ReZygiskState = await _getReZygiskState()
  const all_modules = []
	const strings = await getStrings(whichCurrentPage())

  Object.keys(ReZygiskState.rezygiskd).forEach((daemon_bit) => {
    const daemon = ReZygiskState.rezygiskd[daemon_bit]

    if (daemon.modules && daemon.modules.length > 0) {
			console.log(daemon.modules)
      daemon.modules.forEach((module_id) => {
        const module = all_modules.find((mod) => mod.id === module_id)
        if (module) {
          module.bitsUsed.push(daemon_bit)
        } else {
          all_modules.push({
            id: module_id,
            name: null,
            bitsUsed: [ daemon_bit ]
          })
        }
      })
    }
  })

  if (all_modules.length !== 0) {
		const modules_list = document.getElementById('modules_list')
		modules_list.innerHTML = `
		  <div id="modules_list_not_avaliable" class="not_avaliable">
				${strings.notAvaliable}
			</div>
		`
    document.getElementById('modules_list_not_avaliable').style.display = 'none'

    const module_names = await _getModuleNames(all_modules)
    module_names.forEach((module_name, i) => all_modules[i].name = module_name)

    console.log(`[rezygisk.js] Module list:`)
    console.log(all_modules)

    all_modules.forEach((module) => {
      modules_list.innerHTML += 
        `<div class="dim card" style="padding: 25px 15px; cursor: pointer;">
          <div class="dimc" style="font-size: 1.1em;">${module.name}</div>
          <div class="dimc desc" style="font-size: 0.9em; margin-top: 3px; white-space: nowrap; align-items: center; display: flex;">
            <div class="dimc arch_desc">${strings.arch}</div>
            <div class="dimc" style="margin-left: 5px;">${module.bitsUsed.join(' / ')}</div>
          </div>
        </div>`
    })
  
  }
}
