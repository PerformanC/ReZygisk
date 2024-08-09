export function translateActionPage(old_translations, new_translations) {
  /* INFO: action card */
  document.getElementById('panel_actions_header').innerHTML = new_translations.page.action.header

  /* INFO: daemon32 small card */
  document.getElementById('daemon32_title').innerHTML = new_translations.page.action.monitor["32"]
  if (document.getElementById('daemon32_stop_button')) { /* INFO: Not all devices have 32-bit support */
    document.getElementById('daemon32_stop_button').innerHTML = new_translations.page.action.daemonButton.stop
    document.getElementById('daemon32_start_button').innerHTML = new_translations.page.action.daemonButton.start
    document.getElementById('daemon32_pause_button').innerHTML = new_translations.page.action.daemonButton.pause
  }

  /* INFO: daemon64 small card */
  document.getElementById('daemon64_title').innerHTML = new_translations.page.action.monitor["64"]
  if (document.getElementById('daemon64_stop_button')) { /* INFO: Not all devices have 64-bit support */
    document.getElementById('daemon64_stop_button').innerHTML = new_translations.page.action.daemonButton.stop
    document.getElementById('daemon64_start_button').innerHTML = new_translations.page.action.daemonButton.start
    document.getElementById('daemon64_pause_button').innerHTML = new_translations.page.action.daemonButton.pause
  }

  /* INFO: daemon32 status card */
  const daemon32_status = document.getElementById('daemon32_status')
  switch (daemon32_status.innerHTML.replace(/(\r\n|\n|\r)/gm, '').trim()) {
    case old_translations.page.action.status.unknown: {
      daemon32_status.innerHTML = new_translations.page.action.status.unknown

      break
    }
    case old_translations.page.action.status.notWorking: {
      daemon32_status.innerHTML = new_translations.page.action.status.notWorking

      break
    }
    case old_translations.page.action.status.ok: {
      daemon32_status.innerHTML = new_translations.page.action.status.ok

      break
    }
  }

  /* INFO: daemon64 status card */
  const daemon64_status = document.getElementById('daemon64_status')
  switch (daemon64_status.innerHTML.replace(/(\r\n|\n|\r)/gm, '').trim()) {
    case old_translations.page.action.status.unknown: {
      daemon64_status.innerHTML = new_translations.page.action.status.unknown

      break
    }
    case old_translations.page.action.status.notWorking: {
      daemon64_status.innerHTML = new_translations.page.action.status.notWorking

      break
    }
    case old_translations.page.action.status.ok: {
      daemon64_status.innerHTML = new_translations.page.action.status.ok

      break
    }
  }
}