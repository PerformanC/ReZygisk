export function translateActionsPage(old_translations, new_translations) {
  /* INFO: actions card */
  document.getElementById('panel_actions_header').innerHTML = new_translations.page.actions.header

  /* INFO: monitor small card */
  document.getElementById('monitor_title').innerHTML = new_translations.page.actions.monitor
  if (document.getElementById('monitor_stop_button')) { /* INFO: Not all devices have 32-bit support */
    document.getElementById('monitor_stop_button').innerHTML = new_translations.page.actions.monitorButton.stop
    document.getElementById('monitor_start_button').innerHTML = new_translations.page.actions.monitorButton.start
    document.getElementById('monitor_pause_button').innerHTML = new_translations.page.actions.monitorButton.pause
  }

  /* INFO: monitor status card */
  const monitor_status = document.getElementById('monitor_status')
  switch (monitor_status.innerHTML.replace(/(\r\n|\n|\r)/gm, '').trim()) {
    case old_translations.page.actions.status.tracing: {
      monitor_status.innerHTML = new_translations.page.actions.status.tracing

      break
    }
    case old_translations.page.actions.status.stopping: {
      monitor_status.innerHTML = new_translations.page.actions.status.stopping

      break
    }
    case old_translations.page.actions.status.stopped: {
      monitor_status.innerHTML = new_translations.page.actions.status.stopped

      break
    }
    case old_translations.page.actions.status.exiting: {
      monitor_status.innerHTML = new_translations.page.actions.status.exiting

      break
    }
    default: monitor_status.innerHTML = new_translations.page.actions.status.unknown
  }
}