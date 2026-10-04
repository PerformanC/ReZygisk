/* INFO: Fake shell responses so the WebUI can be developed in a desktop browser.
           Matching is done by pattern, as many commands are generated dynamically. */
const state = `
  {
    "root": "KernelSU",
    "monitor": {
      "state": "0"
    },
    "rezygiskd": {
      "64": {
        "state": 1,
        "modules": ["playintegrityfix", "zygisk_lsposed", "shamiko", "treat_wheel"]
      },
      "32": {
        "state": 1,
        "modules": ["playintegrityfix", "zygisk_lsposed"]
      }
    },
    "zygote": {
      "64": 1,
      "32": 1
    },
    "updated_at": ${Math.floor(Date.now() / 1000)}
  }
`

const moduleProps = {
  playintegrityfix: 'id=playintegrityfix\nname=Play Integrity Fix\nversion=v19.1\nversionCode=19100\nauthor=chiteroman & osm0sis\ndescription=Fix Play Integrity verdicts to get a certified device on Android 16 QPR2 and older.',
  zygisk_lsposed: 'id=zygisk_lsposed\nname=LSPosed\nversion=v1.10.2 (7190)\nversionCode=7190\nauthor=LSPosed Developers\ndescription=Another enhanced implementation of Xposed Framework. Supports Android 8.1 ~ 16. Requires Zygisk.',
  treat_wheel: 'id=treat_wheel\nname=Treat Wheel\nversion=v1.4\nversionCode=14\nauthor=Unknown\ndescription=Hides the Zygisk and root environment from apps.',
  shamiko: 'id=shamiko\nname=Shamiko\nversion=v1.2.5 (418)\nversionCode=418\nauthor=LSPosed Developers\ndescription=Hide root and modules from apps on the denylist.'
}

function fakeLogcat() {
  const now = new Date()
  const tags = [ 'zygisk-ptrace64', 'zygiskd64', 'zygisk-core64', 'zygiskd32', 'zygisk-injector64' ]
  const msgs = [
    [ 'I', 'status updated: Monitor: ✅, NextZygisk 64-bit: ✅, NextZygisk 32-bit: ✅' ],
    [ 'I', 'Zygote64 injected' ],
    [ 'D', 'GetProcessFlags: uid 10234 -> 0x2' ],
    [ 'W', 'companion for module shamiko not found, skipping' ],
    [ 'I', 'Loaded 3 modules' ],
    [ 'E', 'failed to read from socket: Connection reset by peer' ],
    [ 'I', 'new zygote 1234 detected, injecting' ]
  ]

  const lines = []
  for (let i = 0; i < 60; i++) {
    const t = new Date(now.getTime() - (60 - i) * 1500)
    const pad = (n, l = 2) => String(n).padStart(l, '0')
    const [ level, msg ] = msgs[i % msgs.length]

    lines.push(`${pad(t.getMonth() + 1)}-${pad(t.getDate())} ${pad(t.getHours())}:${pad(t.getMinutes())}:${pad(t.getSeconds())}.${pad(t.getMilliseconds(), 3)}  1023  1050 ${level} ${tags[i % tags.length]}: ${msg}`)
  }

  return lines.join('\n')
}

const responses = [
  [ /^\[ -f \/data\/adb\/nextzygisk\/umount_disabled \]/, () => (localStorage.getItem('rz_dev_umount_off') === '1' ? '1' : '0') ],
  [ /touch \/data\/adb\/nextzygisk\/umount_disabled$/, () => { localStorage.setItem('rz_dev_umount_off', '1'); return '' } ],
  [ /^rm -f \/data\/adb\/nextzygisk\/umount_disabled$/, () => { localStorage.removeItem('rz_dev_umount_off'); return '' } ],
  /* INFO: Set localStorage 'rz_dev_state' to a raw state.json to preview other states. */
  [ /cat \/data\/adb\/rezygisk\/state\.json$/, () => localStorage.getItem('rz_dev_state') || state ],
  [ /cat \/data\/adb\/modules\/rezygisk\/module\.prop$/, () => 'id=rezygisk\nname=NextZygisk\nversion=v1.0.0 (505-7e3db00-release)\nversionCode=505\nauthor=The PerformanC Organization\ndescription=[Monitor: ✅, NextZygisk 64-bit: ✅, NextZygisk 32-bit: ✅] Standalone implementation of Zygisk.' ],
  [ /^echo "model=/, () => [
    'model=Pixel 9 Pro', 'brand=google', 'device=caiman', 'android=16', 'sdk=36', 'patch=2026-09-05',
    'build=BP3A.250905.014', 'abi=arm64-v8a,armeabi-v7a,armeabi', 'kernel=6.1.134-android14-11-g4c2d3e',
    'selinux=Enforcing', 'uptime=187342', 'monitorpid=812', 'daemonpid=1290 1291'
  ].join('\n') ],
  [ /^echo "@@MODULE /, (command) => {
    const ids = [ ...command.matchAll(/@@MODULE ([^"]+)"/g) ].map((m) => m[1])

    return ids.map((id) => `@@MODULE ${id}\n${moduleProps[id] || ''}\n${id === 'shamiko' ? '@@disabled=1\n' : ''}${id === 'zygisk_lsposed' ? '@@webui=1\n@@action=1\n' : ''}`).join('\n')
  } ],
  [ /^echo "@@abilist=/, () => [
    '@@abilist=arm64-v8a,armeabi-v7a,armeabi',
    '@@MODULE playintegrityfix', moduleProps.playintegrityfix, '@@libs=arm64-v8a.so armeabi-v7a.so',
    '@@MODULE zygisk_lsposed', moduleProps.zygisk_lsposed, '@@webui=1', '@@action=1', '@@libs=arm64-v8a.so armeabi-v7a.so x86.so x86_64.so',
    '@@MODULE shamiko', moduleProps.shamiko, '@@disabled=1', '@@libs=arm64-v8a.so armeabi-v7a.so',
    '@@MODULE treat_wheel', moduleProps.treat_wheel, '@@libs=arm64-v8a.so armeabi-v7a.so',
    '@@MODULE x86_only_mod', 'id=x86_only_mod\nname=Emulator Helper\nversion=v2.0\nauthor=dev', '@@libs=x86.so x86_64.so',
    '@@MODULE new_module', 'id=new_module\nname=Freshly Installed\nversion=v1.0\nauthor=dev', '@@update=1', '@@libs=arm64-v8a.so'
  ].join('\n') ],
  [ /^for d in \/data\/adb\/modules/, () => [
    'playintegrityfix|1|Play Integrity Fix', 'zygisk_lsposed|1|LSPosed', 'shamiko|0|Shamiko',
    'treat_wheel|1|Treat Wheel', 'tricky_store|1|Tricky Store', 'rezygisk|1|NextZygisk'
  ].join('\n') ],
  [ /^for b in .*zygisk-ptrace64/, () => '/data/adb/modules/rezygisk/bin/zygisk-ptrace64' ],
  [ / info 2>&1 \| tail/, () => '# zygisk-ptrace64\nDaemon process PID: 1290\nRoot implementation: KernelSU\nModules: 3\n - Play Integrity Fix\n - LSPosed\n - Shamiko\n\n# zygisk-ptrace32\nDaemon process PID: 1291\nRoot implementation: KernelSU\nModules: 2\n - Play Integrity Fix\n - LSPosed' ],
  [ / ctl (start|stop|exit)$/, () => '[NextZygisk]: command sent' ],
  [ /^logcat -d/, () => fakeLogcat() ],
  [ /\/system\/bin\/ls \/data\/adb\/modules\/rezygisk\/webroot\/lang$/, () => 'ar_EG.json\nde_DE.json\nen_US.json\nes_AR.json\nid_ID.json\nit_IT.json\nja_JP.json\nko_KR.json\npl_PL.json\npt_BR.json\nru_RU.json\ntr_TR.json\nuk_UA.json\nvi_VN.json\nzh_CN.json' ],
  [ /\/sdcard\/Download\/NextZygisk/, () => '/sdcard/Download/NextZygisk/nextzygisk-diagnostics-20261003-120000.txt' ]
]

export function getDevelopmentExecResponse(command) {
  for (const [ pattern, respond ] of responses) {
    if (pattern.test(command)) return Promise.resolve({ errno: 0, stdout: respond(command), stderr: '' })
  }

  return Promise.resolve({ errno: -1, stdout: '', stderr: 'Command not found in development response' })
}
