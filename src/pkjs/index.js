var SETTINGS_KEY = 'nerv-watchface-settings-v1';

var DEFAULTS = {
  CLOCK_FORMAT: 0,     // 0 system, 1 24h, 2 12h
  CHEVRON_SOURCE: 0,   // 0 battery, 1 steps
  BOTTOM_LAYOUT: 0,    // 0 date|steps|weather, 1 date|battery|weather, 2 date|steps|battery
  ACCENT_COLOR: 0      // 0 red, 1 amber, 2 green, 3 cyan
};

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
    '<h1>NERV WATCHFACE</h1><p>Configure display format and MAGI-style data modules.</p>' +
    '<label>Clock format</label><select id="CLOCK_FORMAT">' +
      option(0, 'System default', settings.CLOCK_FORMAT) + option(1, '24 hour', settings.CLOCK_FORMAT) + option(2, '12 hour', settings.CLOCK_FORMAT) +
    '</select>' +
    '<label>Chevron source</label><select id="CHEVRON_SOURCE">' +
      option(0, 'Battery level', settings.CHEVRON_SOURCE) + option(1, 'Steps progress', settings.CHEVRON_SOURCE) +
    '</select>' +
    '<label>Bottom modules</label><select id="BOTTOM_LAYOUT">' +
      option(0, 'Date | Steps | Weather', settings.BOTTOM_LAYOUT) + option(1, 'Date | Battery | Weather', settings.BOTTOM_LAYOUT) + option(2, 'Date | Steps | Battery', settings.BOTTOM_LAYOUT) +
    '</select>' +
    '<label>HUD accent</label><select id="ACCENT_COLOR">' +
      option(0, 'NERV red', settings.ACCENT_COLOR) + option(1, 'Amber', settings.ACCENT_COLOR) + option(2, 'Green', settings.ACCENT_COLOR) + option(3, 'Cyan', settings.ACCENT_COLOR) +
    '</select>' +
    '<button id="save">Save</button>' +
    '<div class="note">Logo remains red for now. HUD accents change color.</div>' +
    '</div><script>' +
    'document.getElementById("save").onclick=function(){var s={};["CLOCK_FORMAT","CHEVRON_SOURCE","BOTTOM_LAYOUT","ACCENT_COLOR"].forEach(function(k){s[k]=parseInt(document.getElementById(k).value,10);});location.href="pebblejs://close#"+encodeURIComponent(JSON.stringify(s));};' +
    '</script></body></html>';
}

Pebble.addEventListener('ready', function() {
  sendSettings(loadSettings());
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
