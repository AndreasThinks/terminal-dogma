var SETTINGS_KEY = 'nerv-watchface-settings-v1';

var DEFAULTS = {
  CLOCK_FORMAT: 0,
  BOTTOM_LAYOUT: 0,
  ACCENT_COLOR: 0,
  TEMP_UNIT: 0,
  SYNC_SOURCE: 0
};

var OPEN_METEO_URL = 'https://api.open-meteo.com/v1/forecast';

function loadSettings() {
  var saved = {};
  try {
    saved = JSON.parse(localStorage.getItem(SETTINGS_KEY) || '{}');
  } catch (e) {
    saved = {};
  }
  var settings = {};
  Object.keys(DEFAULTS).forEach(function(key) {
    var value = parseInt(saved[key], 10);
    settings[key] = isNaN(value) ? DEFAULTS[key] : value;
  });
  return settings;
}

function saveSettings(settings) {
  localStorage.setItem(SETTINGS_KEY, JSON.stringify(settings));
}

function sendSettings(settings) {
  Pebble.sendAppMessage(settings, function() {
    console.log('NERV settings sent');
  }, function(e) {
    console.log('NERV settings failed: ' + JSON.stringify(e));
  });
}

function fetchWeather(lat, lon) {
  var url = OPEN_METEO_URL + '?latitude=' + lat +
    '&longitude=' + lon +
    '&current=temperature_2m';

  var req = new XMLHttpRequest();
  req.open('GET', url, true);
  req.onload = function() {
    if (req.status !== 200) {
      console.log('Open-Meteo HTTP ' + req.status);
      return;
    }
    try {
      var data = JSON.parse(req.responseText);
      if (!data || !data.current) return;

      var temp = Math.round(data.current.temperature_2m);
      var msg = {};
      msg.TEMPERATURE = temp;

      Pebble.sendAppMessage(msg, function() {
        console.log('Weather sent: ' + temp + '\u00B0C');
      }, function(e) {
        console.log('Weather send failed: ' + JSON.stringify(e));
      });
    } catch (e) {
      console.log('Open-Meteo parse error: ' + e);
    }
  };
  req.onerror = function() {
    console.log('Open-Meteo network error');
  };
  req.send();
}

function option(value, label, selected) {
  return '<option value="' + value + '"' + (selected === value ? ' selected' : '') + '>' + label + '</option>';
}

function buildConfigHtml(settings) {
  return '<!doctype html><html><head><meta name="viewport" content="width=device-width,initial-scale=1">' +
    '<title>NERV Watchface</title>' +
    '<style>' +
    'body{margin:0;background:#070707;color:#f4f4f4;font-family:-apple-system,BlinkMacSystemFont,Segoe UI,sans-serif;}' +
    '.wrap{padding:18px;max-width:520px;margin:0 auto;}' +
    'h1{font-size:22px;margin:0 0 4px;color:#ff2424;letter-spacing:.04em;}' +
    'p{color:#aaa;margin:0 0 18px;font-size:13px;line-height:1.35;}' +
    'label{display:block;margin:16px 0 6px;color:#ffd400;font-size:12px;text-transform:uppercase;font-weight:700;letter-spacing:.06em;}' +
    'select{width:100%;box-sizing:border-box;background:#111;color:#fff;border:1px solid #444;border-radius:8px;padding:12px;font-size:16px;}' +
    'button{width:100%;margin-top:22px;background:#ff2424;color:#000;border:0;border-radius:8px;padding:13px;font-weight:800;font-size:16px;text-transform:uppercase;}' +
    '.note{margin-top:12px;color:#777;font-size:12px;}' +
    '</style></head><body><div class="wrap">' +
    '<h1>NERV WATCHFACE</h1><p>Configure display format, MAGI-style modules, weather units, and the SYNC rail source.</p>' +
    '<label>Clock format</label><select id="CLOCK_FORMAT">' +
      option(0, 'System default', settings.CLOCK_FORMAT) + option(1, '24 hour', settings.CLOCK_FORMAT) + option(2, '12 hour', settings.CLOCK_FORMAT) +
    '</select>' +
    '<label>Bottom modules</label><select id="BOTTOM_LAYOUT">' +
      option(0, 'Date | Steps | Weather', settings.BOTTOM_LAYOUT) + option(1, 'Date | Battery | Weather', settings.BOTTOM_LAYOUT) + option(2, 'Date | Steps | Battery', settings.BOTTOM_LAYOUT) +
    '</select>' +
    '<label>HUD accent</label><select id="ACCENT_COLOR">' +
      option(0, 'NERV red', settings.ACCENT_COLOR) + option(1, 'Amber', settings.ACCENT_COLOR) + option(2, 'Green', settings.ACCENT_COLOR) + option(3, 'Cyan', settings.ACCENT_COLOR) +
    '</select>' +
    '<label>Temperature unit</label><select id="TEMP_UNIT">' +
      option(0, 'Celsius', settings.TEMP_UNIT) + option(1, 'Fahrenheit', settings.TEMP_UNIT) +
    '</select>' +
    '<label>SYNC rail source</label><select id="SYNC_SOURCE">' +
      option(0, 'Data freshness', settings.SYNC_SOURCE) + option(1, 'Steps progress', settings.SYNC_SOURCE) + option(2, 'Battery level', settings.SYNC_SOURCE) + option(3, 'Aesthetic diagnostic', settings.SYNC_SOURCE) +
    '</select>' +
    '<button id="save">Save</button>' +
    '<div class="note">Weather fetches live from Open-Meteo using GPS. SYNC can show data freshness, steps toward 10k, battery, or a fixed diagnostic state. Logo stays NERV red.</div>' +
    '</div><script>' +
    'document.getElementById("save").onclick=function(){var s={};["CLOCK_FORMAT","BOTTOM_LAYOUT","ACCENT_COLOR","TEMP_UNIT","SYNC_SOURCE"].forEach(function(k){s[k]=parseInt(document.getElementById(k).value,10);});location.href="pebblejs://close#"+encodeURIComponent(JSON.stringify(s));};' +
    '</script></body></html>';
}

Pebble.addEventListener('ready', function() {
  sendSettings(loadSettings());
  navigator.geolocation.getCurrentPosition(function(pos) {
    fetchWeather(pos.coords.latitude, pos.coords.longitude);
  }, function(err) {
    console.log('Geolocation error: ' + JSON.stringify(err));
  }, {timeout: 15000, maximumAge: 600000});
});

Pebble.addEventListener('showConfiguration', function() {
  var html = buildConfigHtml(loadSettings());
  Pebble.openURL('data:text/html,' + encodeURIComponent(html));
});

Pebble.addEventListener('webviewclosed', function(e) {
  if (!e || !e.response) return;
  var settings;
  try {
    settings = JSON.parse(decodeURIComponent(e.response));
  } catch (err) {
    console.log('No valid NERV settings response');
    return;
  }
  saveSettings(settings);
  sendSettings(settings);
});

Pebble.addEventListener('appmessage', function(e) {
  if (e.payload && e.payload.REQUEST_WEATHER) {
    navigator.geolocation.getCurrentPosition(function(pos) {
      fetchWeather(pos.coords.latitude, pos.coords.longitude);
    }, function(err) {
      console.log('Geolocation error on request: ' + JSON.stringify(err));
    }, {timeout: 15000, maximumAge: 600000});
  }
});