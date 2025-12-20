const listeners = {}

function addListener(element, type, cb) {
  if (element === window) {
    element.id = 'window'
  }

  if (!listeners[element.id])
    listeners[element.id] = {}

  if (!listeners[element.id][type])
    listeners[element.id][type] = []

  listeners[element.id][type].push(cb)
  element.addEventListener(type, cb)
}

function removeListener(element, type, cb) {
  if (!listeners[element.id] || !listeners[element.id][type]) return

  listeners[element.id][type] = listeners[element.id][type].filter(listener => listener !== cb)
}

function removeAllListeners(element) {
  if (element === undefined) {
    for (const element in listeners) {
      for (const type in listeners[element.id]) {
        listeners[element.id][type].forEach(listener => element.removeEventListener(type, listener))
      }

      delete listeners[element.id]
    }
  } else {
    if (!listeners[element.id]) return

    for (const type in listeners[element.id]) {
      listeners[element.id][type].forEach(listener => element.removeEventListener(type, listener))
    }

    delete listeners[element.id]
  }
}

function reapplyListeners() {
  /* INFO: First remove all listeners */
  const elementsCopy = { ...listeners }
  removeAllListeners()

  /* INFO: Then reapply them */
  for (const elementId in elementsCopy) {
    const element = document.getElementById(elementId)
    if (!element) continue

    for (const type in elementsCopy[elementId]) {
      // INFO: iterate is not defined in any of vanilla JS element event, so we should ignore it
      if (type == "iterate") continue;
      elementsCopy[elementId][type].forEach(listener => {
        element.addEventListener(type, listener)
      })
    }
  }
}

Object.prototype.iterate = function(callback) {
  for (let i = 0; i < this.length; i++) {
    callback(this[i], i)
  }
}

Window.prototype.onceEvent = function(event, callback) {
  this.addEventListener(event, function listener(...args) {
    this.removeEventListener(event, listener)
    callback(...args)
  })
}

Window.prototype.onceTrueEvent = function(event, callback) {
  this.addEventListener(event, function listener(...args) {
    if (!callback(...args)) return;

    this.removeEventListener(event, listener)
  })
}

function isDivOrInsideDiv(element, id) {
  if (element.id == id) return true

  if (element.parentNode) {
    return isDivOrInsideDiv(element.parentNode, id)
  }

  return false
}

export default {
  addListener,
  removeListener,
  removeAllListeners,
  reapplyListeners,
  isDivOrInsideDiv
}