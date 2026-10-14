var phone = require('./phone').createPhone({
  sendAppMessage: function(msg, ok, fail) { Pebble.sendAppMessage(msg, ok, fail); },
  setTimeout: function(fn, ms) { return setTimeout(fn, ms); },
  clearTimeout: function(id) { clearTimeout(id); },
  now: function() { return Date.now(); },
  storage: localStorage,
  getPosition: function(ok, err, opts) { navigator.geolocation.getCurrentPosition(ok, err, opts); },
  createXhr: function() { return new XMLHttpRequest(); },
  log: function(msg) { console.log('Terminal Dogma: ' + msg); }
});

Pebble.addEventListener('ready', function() {
  console.log('Terminal Dogma: ready, weather ' + phone.onReady());
});

Pebble.addEventListener('showConfiguration', function() {
  Pebble.openURL(phone.configUrl());
});

Pebble.addEventListener('webviewclosed', function(e) {
  phone.onWebviewClosed(e && e.response);
});

Pebble.addEventListener('appmessage', function(e) {
  phone.onAppMessage(e && e.payload);
});
