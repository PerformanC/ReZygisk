Object.prototype.iterate = function(callback) {
  for (let i = 0; i < this.length; i++) {
    callback(this[i], i)
  }
}
