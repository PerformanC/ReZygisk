function setError(place, issue) {
  const fullErrorLog = setErrorData(`${place}: ${issue}`)
  document.getElementById('errorh_panel').innerHTML = fullErrorLog
}

function setErrorData(errorLog) {
  const getPrevious = localStorage.getItem('/system/error')
  const finalLog = getPrevious && getPrevious.length !== 0 ? getPrevious + `\n` + errorLog : errorLog

  localStorage.setItem('/system/error', finalLog)
  return finalLog
}

if (window.onerror) window.onerror = (err) => { 
  setError('WebUI', err.stack ? err.stack : err.message) 
}