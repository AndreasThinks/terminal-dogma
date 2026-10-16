/* Terminal Dogma phone logic. Everything platform-specific is injected through `env`
 * so the host tests (tests/js) can drive it with fake timers, storage and transport. */

var SETTINGS_KEY = 'nerv-watchface-settings-v1'; /* unchanged since 1.0.x */
var WEATHER_KEY = 'terminal-dogma-weather-v1';
var OPEN_METEO_URL = 'https://api.open-meteo.com/v1/forecast';

var SETTINGS = {
  CLOCK_FORMAT: {max: 2, def: 0},
  BOTTOM_LAYOUT: {max: 2, def: 0},
  ACCENT_COLOR: {max: 3, def: 0},
  TEMP_UNIT: {max: 1, def: 0},
  SYNC_SOURCE: {max: 3, def: 0},
  ANIMATION: {max: 2, def: 2}, /* 0 off, 1 subtle, 2 full */
  SHAKE_SCREEN: {max: 2, def: 0}, /* 0 off, 1 weather forecast, 2 heart rate */
  SHAKE_FX: {max: 1, def: 0} /* 0 Sync Graph, 1 Emergency Hex */
};
var FORECAST_HOURS = 12;
var SETTING_NAMES = Object.keys(SETTINGS);

var T = {
  READY_SKIP_S: 30 * 60,  /* weather delivered this recently: the watch has it persisted */
  CACHE_REUSE_S: 10 * 60, /* resend a fetch this recent instead of GPS + HTTP */
  GEO_TIMEOUT_MS: 15000,
  HTTP_TIMEOUT_MS: 15000,
  FETCH_GUARD_MS: 40000,  /* whole fetch abandoned after this, whatever the callbacks do */
  SEND_TIMEOUT_MS: 10000, /* an AppMessage with no ack/nack after this counts as failed */
  RETRY_DELAYS_MS: [1000, 3000, 9000]
};

function clampSetting(name, value) {
  var spec = SETTINGS[name];
  var v = parseInt(value, 10);
  return (isNaN(v) || v < 0 || v > spec.max) ? spec.def : v;
}

/* Only keys that are actually present are returned, so nothing is filled from defaults. */
function sanitizeSettings(obj) {
  var out = {};
  if (!obj || typeof obj !== 'object') return out;
  SETTING_NAMES.forEach(function(name) {
    if (obj[name] !== undefined && obj[name] !== null && obj[name] !== '') {
      out[name] = clampSetting(name, obj[name]);
    }
  });
  return out;
}

function withDefaults(partial) {
  var out = {};
  SETTING_NAMES.forEach(function(name) {
    out[name] = partial[name] !== undefined ? partial[name] : SETTINGS[name].def;
  });
  return out;
}

/* Serialised AppMessage sender: one message in flight, bounded retries with backoff, a
 * watchdog for lost callbacks, and same-kind coalescing (a newer payload replaces an
 * older queued one). */
function createQueue(env) {
  var queue = [];
  var inflight = null;
  var retryTimer = null;

  function pump() {
    if (inflight || retryTimer || !queue.length) return;
    var item = queue.shift();
    inflight = item;
    var settled = false;
    var watchdog = env.setTimeout(function() { settle(false, 'timeout'); }, T.SEND_TIMEOUT_MS);

    function settle(ok, why) {
      if (settled) return;
      settled = true;
      env.clearTimeout(watchdog);
      inflight = null;
      if (ok) {
        if (item.onSuccess) item.onSuccess();
        pump();
        return;
      }
      item.tries++;
      if (item.tries > T.RETRY_DELAYS_MS.length) {
        env.log('send ' + item.kind + ' gave up: ' + why);
        if (item.onFail) item.onFail();
        pump();
        return;
      }
      retryTimer = env.setTimeout(function() {
        retryTimer = null;
        /* a newer payload of the same kind supersedes this retry */
        if (!queue.some(function(q) { return q.kind === item.kind; })) queue.unshift(item);
        pump();
      }, T.RETRY_DELAYS_MS[item.tries - 1]);
    }

    env.sendAppMessage(item.msg, function() { settle(true); }, function() { settle(false, 'nack'); });
  }

  function enqueue(kind, msg, onSuccess, onFail) {
    for (var i = 0; i < queue.length; i++) {
      if (queue[i].kind === kind) {
        queue[i] = {kind: kind, msg: msg, tries: 0, onSuccess: onSuccess, onFail: onFail};
        pump();
        return;
      }
    }
    queue.push({kind: kind, msg: msg, tries: 0, onSuccess: onSuccess, onFail: onFail});
    pump();
  }

  return {
    enqueue: enqueue,
    pending: function() { return queue.length + (inflight ? 1 : 0) + (retryTimer ? 1 : 0); }
  };
}

function createPhone(env) {
  var queue = createQueue(env);
  var fetchId = 0;
  var fetching = false;
  var guardTimer = null;

  function nowS() { return Math.floor(env.now() / 1000); }

  function readJson(key) {
    try {
      var raw = env.storage.getItem(key);
      return raw ? JSON.parse(raw) : null;
    } catch (e) {
      return null;
    }
  }

  function loadSaved() {
    var saved = sanitizeSettings(readJson(SETTINGS_KEY));
    return Object.keys(saved).length ? saved : null;
  }

  function saveSettings(partial) {
    var merged = sanitizeSettings(readJson(SETTINGS_KEY));
    Object.keys(partial).forEach(function(k) { merged[k] = partial[k]; });
    env.storage.setItem(SETTINGS_KEY, JSON.stringify(merged));
    return merged;
  }

  function sendSettings(settings) {
    if (Object.keys(settings).length) queue.enqueue('settings', settings);
  }

  function sendWeather(temp, ts, fc) {
    var msg = {TEMPERATURE: temp, WEATHER_TIME: ts};
    if (fc) {
      msg.FORECAST_START = fc.start;
      /* byte arrays: temperatures as signed bytes */
      msg.FORECAST_TEMPS = fc.temps.map(function(t) { return t & 0xff; });
      msg.FORECAST_RAIN = fc.rain;
      msg.FORECAST_CODES = fc.codes;
    }
    queue.enqueue('weather', msg, function() {
      var cache = readJson(WEATHER_KEY) || {};
      if (cache.ts === ts) {
        cache.deliveredTs = nowS();
        env.storage.setItem(WEATHER_KEY, JSON.stringify(cache));
      }
    });
  }

  function finishFetch(id, why) {
    if (id !== fetchId || !fetching) return;
    fetching = false;
    env.clearTimeout(guardTimer);
    guardTimer = null;
    env.log('weather fetch ' + why);
  }

  function fetchWeather(id, lat, lon) {
    var url = OPEN_METEO_URL + '?latitude=' + lat + '&longitude=' + lon + '&current=temperature_2m' +
      '&hourly=temperature_2m,precipitation_probability,weather_code&forecast_hours=' + FORECAST_HOURS +
      '&timeformat=unixtime';
    var req = env.createXhr();
    req.open('GET', url, true);
    req.timeout = T.HTTP_TIMEOUT_MS;
    req.ontimeout = function() { finishFetch(id, 'http timeout'); };
    req.onerror = function() { finishFetch(id, 'network error'); };
    req.onload = function() {
      if (id !== fetchId || !fetching) return;
      if (req.status !== 200) return finishFetch(id, 'http ' + req.status);
      var temp, data;
      try {
        data = JSON.parse(req.responseText);
        temp = Math.round(data.current.temperature_2m);
      } catch (e) {
        return finishFetch(id, 'parse error');
      }
      if (typeof temp !== 'number' || isNaN(temp)) return finishFetch(id, 'no temperature');
      var ts = nowS();
      var fc = parseForecast(data);
      var cache = {temp: temp, ts: ts};
      if (fc) cache.fc = fc;
      env.storage.setItem(WEATHER_KEY, JSON.stringify(cache));
      sendWeather(temp, ts, fc);
      finishFetch(id, 'ok');
    };
    req.send();
  }

  /* reason: 'ready' (JS started with the watchface) or 'watch' (REQUEST_WEATHER). */
  function requestWeather(reason) {
    var now = nowS();
    var cache = readJson(WEATHER_KEY);
    if (reason === 'ready' && cache && cache.deliveredTs && now - cache.deliveredTs >= 0 &&
        now - cache.deliveredTs < T.READY_SKIP_S) {
      return 'skip-delivered';
    }
    if (cache && typeof cache.temp === 'number' && now - cache.ts >= 0 && now - cache.ts < T.CACHE_REUSE_S) {
      sendWeather(cache.temp, cache.ts, cache.fc || null);
      return 'cache';
    }
    if (fetching) return 'busy';
    fetching = true;
    var id = ++fetchId;
    guardTimer = env.setTimeout(function() { finishFetch(id, 'guard timeout'); }, T.FETCH_GUARD_MS);
    env.getPosition(function(pos) {
      if (id !== fetchId || !fetching) return;
      fetchWeather(id, pos.coords.latitude, pos.coords.longitude);
    }, function() {
      finishFetch(id, 'location error');
    }, {timeout: T.GEO_TIMEOUT_MS, maximumAge: 600000});
    return 'fetch';
  }

  function onReady() {
    var saved = loadSaved();
    /* Without phone-side settings, ask the watch for its own instead of pushing defaults. */
    if (saved) sendSettings(saved);
    else queue.enqueue('request-settings', {REQUEST_SETTINGS: 1});
    return requestWeather('ready');
  }

  function onAppMessage(payload) {
    if (!payload) return null;
    var fromWatch = sanitizeSettings(payload);
    if (Object.keys(fromWatch).length) saveSettings(fromWatch);
    if (payload.REQUEST_WEATHER) return requestWeather('watch');
    return null;
  }

  function onWebviewClosed(response) {
    if (!response) return null;
    var parsed;
    try {
      parsed = JSON.parse(decodeURIComponent(response));
    } catch (e) {
      return null;
    }
    var settings = sanitizeSettings(parsed);
    if (!Object.keys(settings).length) return null;
    saveSettings(settings);
    sendSettings(settings);
    return settings;
  }

  function configUrl() {
    return 'data:text/html,' + encodeURIComponent(buildConfigHtml(withDefaults(loadSaved() || {})));
  }

  return {
    onReady: onReady,
    onAppMessage: onAppMessage,
    onWebviewClosed: onWebviewClosed,
    configUrl: configUrl,
    requestWeather: requestWeather,
    queue: queue,
    isFetching: function() { return fetching; }
  };
}

function num(v) {
  return typeof v === 'number' && !isNaN(v);
}

/* Open-Meteo hourly block -> {start, temps, rain, codes}, or null. Slots stop at the first
 * hour without a temperature or out of order, so the watch never shows invented hours. */
function parseForecast(data) {
  var h = data && data.hourly;
  if (!h || !h.time || !h.temperature_2m || !num(h.time[0])) return null;
  var fc = {start: h.time[0], temps: [], rain: [], codes: []};
  for (var i = 0; i < h.time.length && i < FORECAST_HOURS; i++) {
    var t = h.temperature_2m[i];
    if (!num(t) || h.time[i] !== fc.start + i * 3600) break;
    var p = h.precipitation_probability ? h.precipitation_probability[i] : null;
    var c = h.weather_code ? h.weather_code[i] : null;
    fc.temps.push(Math.max(-128, Math.min(127, Math.round(t))));
    fc.rain.push(num(p) ? Math.max(0, Math.min(100, Math.round(p))) : 0);
    fc.codes.push(num(c) && c >= 0 && c < 255 ? c : 255);
  }
  return fc.temps.length ? fc : null;
}

function option(value, label, selected) {
  return '<option value="' + value + '"' + (selected === value ? ' selected' : '') + '>' + label + '</option>';
}

function select(id, label, settings, options) {
  return '<label>' + label + '</label><select id="' + id + '">' +
    options.map(function(o, i) { return option(i, o, settings[id]); }).join('') + '</select>';
}

function buildConfigHtml(settings) {
  return '<!doctype html><html><head><meta name="viewport" content="width=device-width,initial-scale=1">' +
    '<title>Terminal Dogma</title>' +
    '<style>' +
    'body{margin:0;background:#070707;color:#f4f4f4;font-family:-apple-system,BlinkMacSystemFont,Segoe UI,sans-serif;}' +
    '.wrap{padding:18px;max-width:520px;margin:0 auto;}' +
    'h1{font-size:22px;margin:0 0 4px;color:#ff2424;letter-spacing:.04em;}' +
    'p{color:#aaa;margin:0 0 18px;font-size:13px;line-height:1.35;}' +
    'label{display:block;margin:16px 0 6px;color:#ffd400;font-size:12px;text-transform:uppercase;font-weight:700;letter-spacing:.06em;}' +
    'select{width:100%;box-sizing:border-box;background:#111;color:#fff;border:1px solid #444;border-radius:8px;padding:12px;font-size:16px;}' +
    'button{width:100%;margin-top:22px;background:#ff2424;color:#000;border:0;border-radius:8px;padding:13px;font-weight:800;font-size:16px;text-transform:uppercase;}' +
    '.note{margin-top:12px;color:#777;font-size:12px;line-height:1.4;}' +
    '</style></head><body><div class="wrap">' +
    '<h1>TERMINAL DOGMA</h1><p>Configure display format, MAGI-style modules, weather units, the SYNC rail and animations.</p>' +
    select('CLOCK_FORMAT', 'Clock format', settings, ['System default', '24 hour', '12 hour (AM/PM)']) +
    select('BOTTOM_LAYOUT', 'Bottom modules', settings, ['Date | Steps | Weather', 'Date | Battery | Weather', 'Date | Steps | Battery']) +
    select('ACCENT_COLOR', 'HUD accent', settings, ['NERV red', 'Amber', 'Green', 'Cyan']) +
    select('TEMP_UNIT', 'Temperature unit', settings, ['Celsius', 'Fahrenheit']) +
    select('SYNC_SOURCE', 'SYNC rail source', settings, ['Data freshness', 'Steps progress', 'Battery level', 'Aesthetic diagnostic']) +
    select('ANIMATION', 'Animations', settings, ['Off', 'Subtle (MAGI check only)', 'Full (system boot + MAGI check)']) +
    select('SHAKE_SCREEN', 'Shake to show', settings, ['Off', 'Weather, next 6 hours', 'Heart rate, last 3 hours']) +
    select('SHAKE_FX', 'Shake transition', settings, ['Sync Graph', 'Emergency Hex']) +
    '<button id="save">Save</button>' +
    '<div class="note">Weather comes from Open-Meteo using your phone location, refreshed about every 30 minutes. ' +
    'It turns grey and reads STALE after an hour without an update, and -- after six hours. ' +
    'BIO shows the watch\'s most recent heart-rate reading (at most 15 minutes old), or -- when there is none. ' +
    'Animations play when the face is first shown (at most once every 30 minutes) and briefly when the phone link or weather recovers. ' +
    'Off uses no animation timers at all. ' +
    'Shake to show opens the forecast or a heart-rate graph for 10 seconds; shake again to close it. ' +
    'With animations Off or a low battery it opens without the transition.</div>' +
    '</div><script>' +
    'document.getElementById("save").onclick=function(){var s={};' + JSON.stringify(SETTING_NAMES) +
    '.forEach(function(k){s[k]=parseInt(document.getElementById(k).value,10);});' +
    'location.href="pebblejs://close#"+encodeURIComponent(JSON.stringify(s));};' +
    '</script></body></html>';
}

module.exports = {
  createPhone: createPhone,
  createQueue: createQueue,
  sanitizeSettings: sanitizeSettings,
  clampSetting: clampSetting,
  parseForecast: parseForecast,
  buildConfigHtml: buildConfigHtml,
  SETTINGS_KEY: SETTINGS_KEY,
  WEATHER_KEY: WEATHER_KEY,
  TIMING: T
};
