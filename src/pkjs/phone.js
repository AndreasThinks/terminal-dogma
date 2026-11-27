/* Terminal Dogma phone logic. Everything platform-specific is injected through `env`
 * so the host tests (tests/js) can drive it with fake timers, storage and transport. */

var SETTINGS_KEY = 'terminal-dogma-settings-v2';
var LEGACY_SETTINGS_KEY = 'nerv-watchface-settings-v1';
var WEATHER_KEY = 'terminal-dogma-weather-v1';
var LOCATION_KEY = 'terminal-dogma-location-v1'; /* last fix, for sunrise and sunset */
var OPEN_METEO_URL = 'https://api.open-meteo.com/v1/forecast';
/* Pebble plugin weather source (mobile app 2.0+). Used when it can serve the whole
 * reading; otherwise the face falls back to its own GPS + Open-Meteo fetch. */
var PLUGIN_SOURCE = {category: 'weather', item: 'location'};
var PLUGIN_PROPS = ['temperature', 'hourly_temperature', 'hourly_precipitation', 'hourly_condition_code'];
/* plugin condition_code -> the WMO code the watch already labels (td_wmo_label) */
var PLUGIN_CODES = {
  sun: 0, partly_cloudy: 2, cloudy: 3, light_rain: 61, heavy_rain: 65,
  light_snow: 71, heavy_snow: 75, rain_and_snow: 66
};

var SETTINGS = {
  CLOCK_FORMAT: {max: 2, def: 0},
  BOTTOM_LAYOUT: {max: 2, def: 0},
  ACCENT_COLOR: {max: 3, def: 0},
  TEMP_UNIT: {max: 1, def: 0},
  SYNC_SOURCE: {max: 4, def: 0}, /* 4: steps vs typical day */
  ANIMATION: {max: 2, def: 2}, /* 0 off, 1 subtle, 2 full */
  SHAKE_SCREEN: {max: 3, def: 2}, /* 0 off, 1 weather forecast, 2 heart rate, 3 steps */
  SHAKE_FX: {max: 1, def: 0}, /* 0 Sync Graph, 1 Emergency Hex */
  LINK_VIBE: {max: 1, def: 0}, /* buzz when the phone disconnects */
  TZ2: {max: 17, def: 0} /* second time zone: index into TZ2_CITIES, 0 off */
};
/* Second time zone choices. Same order and codes as s_tz2_labels in src/c/td_logic.c.
 * std is the standard UTC offset in minutes, used only where Intl time zones are missing. */
var TZ2_CITIES = [
  null,
  {code: 'LON', name: 'London', tz: 'Europe/London', std: 0},
  {code: 'PAR', name: 'Paris', tz: 'Europe/Paris', std: 60},
  {code: 'ATH', name: 'Athens', tz: 'Europe/Athens', std: 120},
  {code: 'DXB', name: 'Dubai', tz: 'Asia/Dubai', std: 240},
  {code: 'DEL', name: 'Delhi', tz: 'Asia/Kolkata', std: 330},
  {code: 'SIN', name: 'Singapore', tz: 'Asia/Singapore', std: 480},
  {code: 'HKG', name: 'Hong Kong', tz: 'Asia/Hong_Kong', std: 480},
  {code: 'TYO', name: 'Tokyo', tz: 'Asia/Tokyo', std: 540},
  {code: 'SYD', name: 'Sydney', tz: 'Australia/Sydney', std: 600},
  {code: 'AKL', name: 'Auckland', tz: 'Pacific/Auckland', std: 720},
  {code: 'HNL', name: 'Honolulu', tz: 'Pacific/Honolulu', std: -600},
  {code: 'LAX', name: 'Los Angeles', tz: 'America/Los_Angeles', std: -480},
  {code: 'DEN', name: 'Denver', tz: 'America/Denver', std: -420},
  {code: 'CHI', name: 'Chicago', tz: 'America/Chicago', std: -360},
  {code: 'NYC', name: 'New York', tz: 'America/New_York', std: -300},
  {code: 'SAO', name: 'Sao Paulo', tz: 'America/Sao_Paulo', std: -180},
  {code: 'UTC', name: 'UTC', tz: 'UTC', std: 0}
];
var FORECAST_HOURS = 12;
var SETTING_NAMES = Object.keys(SETTINGS);

var T = {
  LOCATION_MAX_AGE_S: 24 * 60 * 60, /* sunrise/sunset location reused for a day */
  READY_SKIP_S: 30 * 60,  /* weather delivered this recently: the watch has it persisted */
  CACHE_REUSE_S: 10 * 60, /* resend a fetch this recent instead of GPS + HTTP */
  GEO_TIMEOUT_MS: 15000,
  HTTP_TIMEOUT_MS: 15000,
  PLUGIN_WAIT_MS: 8000,   /* wait this long for the plugin source before falling back */
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
  var plugin = {sub: null, off: !env.subscribeToSource, latest: null, waiter: null};

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
    if (Object.keys(saved).length) return saved;
    saved = sanitizeSettings(readJson(LEGACY_SETTINGS_KEY));
    if (!Object.keys(saved).length) return null;
    /* 1.0.19 stored Off as the default. Move that old default to heart rate. */
    if (saved.SHAKE_SCREEN === 0) saved.SHAKE_SCREEN = SETTINGS.SHAKE_SCREEN.def;
    env.storage.setItem(SETTINGS_KEY, JSON.stringify(saved));
    return saved;
  }

  function saveSettings(partial) {
    var merged = sanitizeSettings(readJson(SETTINGS_KEY));
    Object.keys(partial).forEach(function(k) { merged[k] = partial[k]; });
    env.storage.setItem(SETTINGS_KEY, JSON.stringify(merged));
    return merged;
  }

  function savedLocation() {
    var loc = readJson(LOCATION_KEY);
    if (!loc || !num(loc.lat) || !num(loc.lon) || !num(loc.at)) return null;
    var age = nowS() - loc.at;
    return age >= 0 && age < T.LOCATION_MAX_AGE_S ? loc : null;
  }

  function rememberLocation(lat, lon) {
    env.storage.setItem(LOCATION_KEY, JSON.stringify({lat: lat, lon: lon, at: nowS()}));
  }

  /* Second-zone offset and the next sunrise/sunset, added to what goes to the watch. Both are
   * refreshed with every weather send, so a DST change is picked up within the refresh period. */
  function addExtras(msg, tz2Index) {
    var idx = tz2Index !== undefined ? tz2Index : (sanitizeSettings(readJson(SETTINGS_KEY)).TZ2 || 0);
    var off = tz2Offset(idx, env.now());
    if (off !== null) msg.TZ2_OFFSET = off;
    var loc = savedLocation();
    if (loc) {
      var sun = nextSunEvents(loc.lat, loc.lon, env.now());
      msg.SUNRISE = sun.rise;
      msg.SUNSET = sun.set;
    }
    return msg;
  }

  /* The plugin weather path has no coordinates: look the location up at most once a day,
   * accepting a fix up to a day old, and send the sun times on their own. */
  function refreshSunLocation() {
    if (savedLocation()) return;
    env.getPosition(function(pos) {
      rememberLocation(pos.coords.latitude, pos.coords.longitude);
      var msg = addExtras({});
      if (msg.SUNRISE !== undefined) queue.enqueue('sun', msg);
    }, function() {
      env.log('sun location error');
    }, {timeout: T.GEO_TIMEOUT_MS, maximumAge: T.LOCATION_MAX_AGE_S * 1000, enableHighAccuracy: false});
  }

  function sendSettings(settings) {
    if (!Object.keys(settings).length) return;
    var msg = {};
    Object.keys(settings).forEach(function(k) { msg[k] = settings[k]; });
    if (settings.TZ2 !== undefined) {
      var off = tz2Offset(settings.TZ2, env.now());
      if (off !== null) msg.TZ2_OFFSET = off;
    }
    queue.enqueue('settings', msg);
  }

  function sendWeather(temp, ts, fc) {
    var msg = addExtras({TEMPERATURE: temp, WEATHER_TIME: ts});
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
      deliver(id, temp, parseForecast(data), 'open-meteo');
    };
    req.send();
  }

  function deliver(id, temp, fc, source) {
    var ts = nowS();
    var cache = {temp: temp, ts: ts, src: source};
    if (fc) cache.fc = fc;
    env.storage.setItem(WEATHER_KEY, JSON.stringify(cache));
    sendWeather(temp, ts, fc);
    finishFetch(id, 'ok ' + source);
  }

  function fetchOpenMeteo(id) {
    env.getPosition(function(pos) {
      if (id !== fetchId || !fetching) return;
      rememberLocation(pos.coords.latitude, pos.coords.longitude);
      fetchWeather(id, pos.coords.latitude, pos.coords.longitude);
    }, function() {
      finishFetch(id, 'location error');
    }, {timeout: T.GEO_TIMEOUT_MS, maximumAge: 600000});
  }

  /* One long-lived subscription; the newest envelope is kept for the next fetch. */
  function ensureSubscription() {
    if (plugin.off || plugin.sub) return;
    try {
      plugin.sub = env.subscribeToSource({
        category: PLUGIN_SOURCE.category,
        item: PLUGIN_SOURCE.item,
        properties: PLUGIN_PROPS,
        onData: function(data) {
          plugin.latest = {data: data, at: env.now()};
          if (plugin.waiter) plugin.waiter('data');
        },
        onError: function(err) {
          var code = err && err.code;
          env.log('weather plugin ' + code);
          /* not installed or not allowed: stop waiting on it for this session */
          if (code === 'PERMISSION_DENIED' || code === 'PLUGIN_UNAVAILABLE') plugin.off = true;
          if (plugin.waiter) plugin.waiter('error');
        }
      }) || true;
    } catch (e) {
      env.log('weather plugin subscribe failed');
      plugin.off = true;
    }
  }

  function freshPluginReading() {
    var l = plugin.latest;
    if (!l || !l.data) return null;
    var nowMs = env.now();
    var valid = num(l.data.validUntilMs) ? nowMs < l.data.validUntilMs : nowMs - l.at < T.CACHE_REUSE_S * 1000;
    return valid ? parsePluginWeather(l.data, nowS()) : null;
  }

  function fetchPlugin(id) {
    var r = freshPluginReading();
    if (r) {
      deliver(id, r.temp, r.fc, 'plugin');
      return refreshSunLocation();
    }
    var waitTimer = null;
    function done(why) {
      if (!plugin.waiter) return;
      plugin.waiter = null;
      env.clearTimeout(waitTimer);
      if (id !== fetchId || !fetching) return;
      var got = why === 'data' ? freshPluginReading() : null;
      if (got) {
        deliver(id, got.temp, got.fc, 'plugin');
        return refreshSunLocation();
      }
      env.log('weather plugin ' + (why === 'data' ? 'incomplete' : why) + ', using open-meteo');
      fetchOpenMeteo(id);
    }
    plugin.waiter = done;
    waitTimer = env.setTimeout(function() { done('timeout'); }, T.PLUGIN_WAIT_MS);
  }

  /* reason: 'ready' (JS started with the watchface) or 'watch' (REQUEST_WEATHER). */
  function requestWeather(reason) {
    var now = nowS();
    var cache = readJson(WEATHER_KEY);
    var hasForecast = cache && cache.fc && num(cache.fc.start) &&
      Array.isArray(cache.fc.temps) && cache.fc.temps.length > 0;
    if (reason === 'ready' && hasForecast && cache.deliveredTs && now - cache.deliveredTs >= 0 &&
        now - cache.deliveredTs < T.READY_SKIP_S) {
      return 'skip-delivered';
    }
    if (hasForecast && typeof cache.temp === 'number' && now - cache.ts >= 0 && now - cache.ts < T.CACHE_REUSE_S) {
      sendWeather(cache.temp, cache.ts, cache.fc);
      return 'cache';
    }
    if (fetching) return 'busy';
    fetching = true;
    var id = ++fetchId;
    guardTimer = env.setTimeout(function() { finishFetch(id, 'guard timeout'); }, T.FETCH_GUARD_MS);
    ensureSubscription();
    if (plugin.off) fetchOpenMeteo(id);
    else fetchPlugin(id);
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

/* Current UTC offset in minutes of a TZ2_CITIES entry (DST included), or null when off. */
function tz2Offset(index, nowMs) {
  var city = TZ2_CITIES[index];
  if (!city) return null;
  try {
    var parts = {};
    new Intl.DateTimeFormat('en-US', {
      timeZone: city.tz, hour12: false, year: 'numeric', month: 'numeric', day: 'numeric',
      hour: 'numeric', minute: 'numeric'
    }).formatToParts(new Date(nowMs)).forEach(function(p) { parts[p.type] = p.value; });
    var asUtc = Date.UTC(+parts.year, +parts.month - 1, +parts.day, +parts.hour % 24, +parts.minute);
    var off = Math.round((asUtc - Math.floor(nowMs / 60000) * 60000) / 60000);
    if (num(off) && off >= -720 && off <= 840) return off;
  } catch (e) {
    /* no Intl time-zone data on this phone: fall through to the standard offset */
  }
  return city.std;
}

var RAD = Math.PI / 180;

/* Sunrise and sunset (ms) for the solar day nearest dayMs, from the standard sunrise equation
 * (accurate to a few minutes). null during polar day or night. */
function sunEventsForDay(lat, lon, dayMs) {
  var jd = dayMs / 86400000 + 2440587.5;
  var n = Math.round(jd - 2451545.0 - 0.0009 + lon / 360);
  var jStar = n + 0.0009 - lon / 360;
  var M = (357.5291 + 0.98560028 * jStar) % 360;
  var C = 1.9148 * Math.sin(M * RAD) + 0.02 * Math.sin(2 * M * RAD) + 0.0003 * Math.sin(3 * M * RAD);
  var L = (M + C + 180 + 102.9372) % 360;
  var jTransit = 2451545.0 + jStar + 0.0053 * Math.sin(M * RAD) - 0.0069 * Math.sin(2 * L * RAD);
  var sinD = Math.sin(L * RAD) * Math.sin(23.4397 * RAD);
  var cosD = Math.cos(Math.asin(sinD));
  var cosW = (Math.sin(-0.833 * RAD) - Math.sin(lat * RAD) * sinD) / (Math.cos(lat * RAD) * cosD);
  if (cosW < -1 || cosW > 1) return null;
  var w = Math.acos(cosW) / RAD;
  function toMs(j) { return (j - 2440587.5) * 86400000; }
  return {rise: toMs(jTransit - w / 360), set: toMs(jTransit + w / 360)};
}

/* Next sunrise and next sunset after nowMs, as unix seconds (0 when there is none in the
 * next two days, as in polar day or night; the watch then shows neither). */
function nextSunEvents(lat, lon, nowMs) {
  var rise = 0, set = 0;
  for (var d = -1; d <= 2; d++) {
    var e = sunEventsForDay(lat, lon, nowMs + d * 86400000);
    if (!e) continue;
    var r = Math.floor(e.rise / 1000), s = Math.floor(e.set / 1000);
    if (e.rise > nowMs && (!rise || r < rise)) rise = r;
    if (e.set > nowMs && (!set || s < set)) set = s;
  }
  return {rise: rise, set: set};
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

/* A series property: {series: {start?, stepSec, <shape>: values}}. `values` may be a bare
 * array or {values: [...], unit}. Returns {start, step, values, unit} or null. */
function seriesOf(prop, shape) {
  var se = prop && prop.series;
  if (!se || !num(se.stepSec) || se.stepSec <= 0 || !num(se.start)) return null;
  var v = se[shape];
  var unit = null;
  if (v && !Array.isArray(v)) { unit = v.unit || null; v = v.values; }
  if (!Array.isArray(v)) return null;
  return {start: se.start, step: se.stepSec, values: v, unit: unit};
}

function isFahrenheit(unit) {
  return typeof unit === 'string' && /F/i.test(unit);
}

function toC(value, unit) {
  return isFahrenheit(unit) ? (value - 32) * 5 / 9 : value;
}

function codeText(v) {
  if (v && typeof v === 'object') v = v.text;
  return typeof v === 'string' ? v : null;
}

/* Plugin envelope -> {temp, fc} in the same form as the Open-Meteo path, or null when the
 * plugin cannot serve the current temperature plus hourly temperature and rain chance.
 * A forecast without rain chances would read as DRY, so that falls back instead. */
function parsePluginWeather(data, nowSec) {
  var inst = data && Array.isArray(data.instances) && data.instances[0];
  var props = inst && inst.properties;
  if (!props) return null;
  var cur = props.temperature && props.temperature.numericValue;
  if (!cur || !num(cur.value)) return null;
  var tempSeries = seriesOf(props.hourly_temperature, 'numericValue');
  var rainSeries = seriesOf(props.hourly_precipitation, 'numericValue');
  if (!tempSeries || !rainSeries) return null;
  var codeSeries = seriesOf(props.hourly_condition_code, 'shortText');
  var unit = tempSeries.unit || cur.unit;
  /* series start at local midnight: skip to the slot holding this hour */
  var first = Math.max(0, Math.floor((nowSec - tempSeries.start) / tempSeries.step));
  var fc = {start: tempSeries.start + first * tempSeries.step, temps: [], rain: [], codes: []};
  if (tempSeries.step !== 3600) return null;
  for (var i = first; i < tempSeries.values.length && fc.temps.length < FORECAST_HOURS; i++) {
    var t = tempSeries.values[i];
    var slot = tempSeries.start + i * 3600;
    var ri = (slot - rainSeries.start) / rainSeries.step;
    var p = rainSeries.step === 3600 && ri >= 0 ? rainSeries.values[ri] : null;
    if (!num(t) || !num(p)) break;
    var c = null;
    if (codeSeries && codeSeries.step === 3600) {
      var ci = (slot - codeSeries.start) / codeSeries.step;
      if (ci >= 0) c = codeText(codeSeries.values[ci]);
    }
    fc.temps.push(Math.max(-128, Math.min(127, Math.round(toC(t, unit)))));
    fc.rain.push(Math.max(0, Math.min(100, Math.round(p))));
    fc.codes.push(c && PLUGIN_CODES.hasOwnProperty(c) ? PLUGIN_CODES[c] : 255);
  }
  if (!fc.temps.length) return null;
  return {temp: Math.round(toC(cur.value, cur.unit)), fc: fc};
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
    select('SYNC_SOURCE', 'SYNC rail source', settings, ['Data freshness', 'Steps progress', 'Battery level', 'Aesthetic diagnostic', 'Steps vs typical day']) +
    select('ANIMATION', 'Animations', settings, ['Off', 'Subtle (MAGI check only)', 'Full (system boot + MAGI check)']) +
    select('SHAKE_SCREEN', 'Shake to show', settings, ['Off', 'Weather, next 6 hours', 'Heart rate, last 3 hours', 'Steps, today']) +
    select('SHAKE_FX', 'Shake transition', settings, ['Sync Graph', 'Emergency Hex']) +
    select('LINK_VIBE', 'Phone disconnect alert', settings, ['Off', 'Buzz and flash LINK LOST']) +
    select('TZ2', 'Second time zone', settings, TZ2_CITIES.map(function(c) { return c ? c.name + ' (' + c.code + ')' : 'Off'; })) +
    '<button id="save">Save</button>' +
    '<div class="note">Weather comes from the Pebble app\'s weather source when it can supply the hourly forecast, ' +
    'otherwise from Open-Meteo using your phone location, refreshed about every 30 minutes. ' +
    'It turns grey and reads STALE after an hour without an update, and -- after six hours. ' +
    'BIO shows the watch\'s most recent heart-rate reading (at most 15 minutes old), or -- when there is none. ' +
    'Animations play when the face is first shown (at most once every 30 minutes) and briefly when the phone link or weather recovers. ' +
    'Off uses no animation timers at all. ' +
    'Shake to show opens the forecast or a heart-rate graph for 10 seconds, then plays the transition back to the face; shake again to return sooner. ' +
    'With animations Off or a low battery it opens without the transition. ' +
    'Steps vs typical day compares today with the watch\'s own average for a weekday or weekend day; the yellow marker is where you usually are by now. ' +
    'The disconnect alert buzzes if the phone stays away for 10 seconds, never during Quiet Time. ' +
    'Sunrise and sunset are worked out on the phone from your location, checked at most once a day.</div>' +
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
  parsePluginWeather: parsePluginWeather,
  tz2Offset: tz2Offset,
  nextSunEvents: nextSunEvents,
  TZ2_CITIES: TZ2_CITIES,
  LOCATION_KEY: LOCATION_KEY,
  buildConfigHtml: buildConfigHtml,
  SETTINGS_KEY: SETTINGS_KEY,
  LEGACY_SETTINGS_KEY: LEGACY_SETTINGS_KEY,
  WEATHER_KEY: WEATHER_KEY,
  TIMING: T
};
