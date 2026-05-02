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

  element.removeEventListener(type, cb)
  listeners[element.id][type] = listeners[element.id][type].filter(listener => listener !== cb)
}

function getElementFromListenerId(elementId) {
  if (elementId === 'window') return window

  return document.getElementById(elementId)
}

function removeAllListeners(element) {
  if (element === undefined) {
    for (const elementId of Object.keys(listeners)) {
      const target = getElementFromListenerId(elementId)

      if (target) {
        for (const type of Object.keys(listeners[elementId])) {
          const callbacks = listeners[elementId][type]
          if (!Array.isArray(callbacks)) continue

          callbacks.forEach(listener => target.removeEventListener(type, listener))
        }
      }

      delete listeners[elementId]
    }
  } else {
    if (!listeners[element.id]) return

    for (const type of Object.keys(listeners[element.id])) {
      const callbacks = listeners[element.id][type]
      if (!Array.isArray(callbacks)) continue

      callbacks.forEach(listener => element.removeEventListener(type, listener))
    }

    delete listeners[element.id]
  }
}

function reapplyListeners() {
  /* INFO: First remove all listeners */
  const elementsCopy = { ...listeners }
  removeAllListeners()

  /* INFO: Then reapply them */
  for (const elementId of Object.keys(elementsCopy)) {
    const element = getElementFromListenerId(elementId)
    if (!element) continue

    listeners[elementId] = {}
    for (const type of Object.keys(elementsCopy[elementId])) {
      const callbacks = elementsCopy[elementId][type]
      if (!Array.isArray(callbacks)) continue

      listeners[elementId][type] = [...callbacks]
      callbacks.forEach(listener => {
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