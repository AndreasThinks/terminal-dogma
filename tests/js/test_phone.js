/* Host tests for src/pkjs/phone.js. Run: node tests/js/test_phone.js (or tests/run.sh) */
'use strict';
var path = require('path');
var phoneLib = require(path.join(__dirname, '..', '..', 'src', 'pkjs', 'phone.js'));
var T = phoneLib.TIMING;

var checks = 0, failures = 0;
function check(cond, msg) {
  checks++;
  if (!cond) { failures++; console.log('  FAIL ' + msg); }
}
function eq(got, want, msg) {
  check(JSON.stringify(got) === JSON.stringify(want),
        msg + ': got ' + JSON.stringify(got) + ' want ' + JSON.stringify(want));
}
function test(name, fn) {
  var before = failures;
  fn();
  console.log((failures === before ? 'ok   ' : 'FAIL ') + name);
}

/* ---------- fake platform ---------- */

function makeEnv(opts) {
  opts = opts || {};
  var now = opts.now || 1760000000000;
  var timers = [];
  var nextId = 1;
  var store = {};
  var env = {
    sent: [],       /* every sendAppMessage call: {msg, ok, fail} */
    inflightMax: 0, /* most messages handed to the transport without an ack/nack yet */
    geoCalls: 0,
    xhrs: [],
    logs: [],
    geo: opts.geo || 'ok',
    storage: {
      getItem: function(k) { return Object.prototype.hasOwnProperty.call(store, k) ? store[k] : null; },
      setItem: function(k, v) { store[k] = String(v); },
      removeItem: function(k) { delete store[k]; },
      clear: function() { store = {}; }
    },
    now: function() { return now; },
    setTimeout: function(fn, ms) { var id = nextId++; timers.push({id: id, at: now + ms, fn: fn}); return id; },
    clearTimeout: function(id) { timers = timers.filter(function(t) { return t.id !== id; }); },
    log: function(m) { env.logs.push(m); },
    sendAppMessage: function(msg, ok, fail) {
      var rec = {msg: msg, settled: false};
      rec.ok = function() { rec.settled = true; ok(); };
      rec.fail = function() { rec.settled = true; fail(); };
      env.sent.push(rec);
      var open = env.sent.filter(function(r) { return !r.settled; }).length;
      if (open > env.inflightMax) env.inflightMax = open;
    },
    getPosition: function(ok, err) {
      env.geoCalls++;
      if (env.geo === 'ok') ok({coords: {latitude: 51.5, longitude: -0.1}});
      else if (env.geo === 'error') err({code: 1});
      /* 'hang': neither callback fires */
    },
    createXhr: function() {
      var x = {open: function(m, u) { x.url = u; }, send: function() { x.sent = true; }};
      env.xhrs.push(x);
      return x;
    },
    advance: function(ms) {
      var end = now + ms;
      for (;;) {
        var due = timers.filter(function(t) { return t.at <= end; }).sort(function(a, b) { return a.at - b.at; });
        if (!due.length) break;
        var t = due[0];
        timers = timers.filter(function(x) { return x !== t; });
        now = t.at;
        t.fn();
      }
      now = end;
    },
    pendingTimers: function() { return timers.length; },
    last: function() { return env.sent[env.sent.length - 1]; },
    open: function() { return env.sent.filter(function(r) { return !r.settled; }); }
  };
  return env;
}

function respond(x, status, body) {
  x.status = status;
  x.responseText = body;
  x.onload();
}
var GOOD = JSON.stringify({
  current: {temperature_2m: 17.6},
  hourly: {
    time: [1760000400, 1760004000],
    temperature_2m: [17.4, 18.1],
    precipitation_probability: [0, 20],
    weather_code: [1, 2]
  }
});

/* ---------- settings ---------- */

test('lost phone storage asks the watch instead of pushing defaults', function() {
  var env = makeEnv();
  var p = phoneLib.createPhone(env);
  p.onReady();
  eq(env.sent.length, 1, 'one message');
  eq(env.sent[0].msg, {REQUEST_SETTINGS: 1}, 'settings request, no default values');
  env.sent[0].ok();
  /* the watch replies with what it has persisted (including ANIMATION) */
  p.onAppMessage({CLOCK_FORMAT: 2, BOTTOM_LAYOUT: 1, ACCENT_COLOR: 3, TEMP_UNIT: 1, SYNC_SOURCE: 2, ANIMATION: 0});
  var stored = JSON.parse(env.storage.getItem(phoneLib.SETTINGS_KEY));
  eq(stored, {CLOCK_FORMAT: 2, BOTTOM_LAYOUT: 1, ACCENT_COLOR: 3, TEMP_UNIT: 1, SYNC_SOURCE: 2, ANIMATION: 0},
     'watch settings stored on phone');
  var html = decodeURIComponent(p.configUrl());
  check(html.indexOf('<select id="ANIMATION"><option value="0" selected>Off') >= 0, 'config page shows Off');
  check(html.indexOf('<option value="3" selected>Cyan') >= 0, 'config page shows Cyan');
});

test('saved 1.0.16 settings are resent without inventing an ANIMATION value', function() {
  var env = makeEnv();
  env.storage.setItem(phoneLib.SETTINGS_KEY, JSON.stringify(
    {CLOCK_FORMAT: 1, BOTTOM_LAYOUT: 0, ACCENT_COLOR: 1, TEMP_UNIT: 0, SYNC_SOURCE: 3}));
  var p = phoneLib.createPhone(env);
  p.onReady();
  eq(env.sent[0].msg, {CLOCK_FORMAT: 1, BOTTOM_LAYOUT: 0, ACCENT_COLOR: 1, TEMP_UNIT: 0, SYNC_SOURCE: 3},
     'only stored keys');
});

test('legacy shake default becomes heart rate while preserving other saved choices', function() {
  var env = makeEnv();
  env.storage.setItem('nerv-watchface-settings-v1', JSON.stringify(
    {ACCENT_COLOR: 2, SHAKE_SCREEN: 0, SHAKE_FX: 0}));
  var p = phoneLib.createPhone(env);
  p.onReady();
  eq(env.sent[0].msg, {ACCENT_COLOR: 2, SHAKE_SCREEN: 2, SHAKE_FX: 0}, 'migrated settings sent');
  eq(JSON.parse(env.storage.getItem(phoneLib.SETTINGS_KEY)),
     {ACCENT_COLOR: 2, SHAKE_SCREEN: 2, SHAKE_FX: 0}, 'migrated settings saved under current key');
});

test('legacy non-default shake choices survive phone settings migration', function() {
  var env = makeEnv();
  env.storage.setItem('nerv-watchface-settings-v1', JSON.stringify(
    {SHAKE_SCREEN: 1, SHAKE_FX: 1}));
  phoneLib.createPhone(env).onReady();
  eq(env.sent[0].msg, {SHAKE_SCREEN: 1, SHAKE_FX: 1}, 'explicit non-default values preserved');
});

test('fresh config defaults to heart rate and Sync Graph', function() {
  var env = makeEnv();
  var html = decodeURIComponent(phoneLib.createPhone(env).configUrl());
  check(html.indexOf('<option value="2" selected>Heart rate, last 3 hours') >= 0, 'heart rate selected');
  check(html.indexOf('<option value="0" selected>Sync Graph') >= 0, 'Sync Graph selected');
});

test('corrupt storage and out-of-range values are handled', function() {
  var env = makeEnv();
  env.storage.setItem(phoneLib.SETTINGS_KEY, '{not json');
  var p = phoneLib.createPhone(env);
  p.onReady();
  eq(env.sent[0].msg, {REQUEST_SETTINGS: 1}, 'corrupt storage treated as absent');
  eq(phoneLib.sanitizeSettings({CLOCK_FORMAT: 9, ANIMATION: '1', TEMP_UNIT: null, BOGUS: 1}),
     {CLOCK_FORMAT: 0, ANIMATION: 1}, 'clamped, absent keys omitted');
  eq(p.onWebviewClosed(undefined), null, 'cancelled config page');
  eq(p.onWebviewClosed('%7Bbad'), null, 'malformed response');
});

test('config page save stores and sends all settings', function() {
  var env = makeEnv();
  var p = phoneLib.createPhone(env);
  var resp = encodeURIComponent(JSON.stringify(
    {CLOCK_FORMAT: 2, BOTTOM_LAYOUT: 2, ACCENT_COLOR: 0, TEMP_UNIT: 1, SYNC_SOURCE: 1, ANIMATION: 1}));
  p.onWebviewClosed(resp);
  eq(env.last().msg.ANIMATION, 1, 'ANIMATION sent');
  eq(JSON.parse(env.storage.getItem(phoneLib.SETTINGS_KEY)).SYNC_SOURCE, 1, 'stored');
});

/* ---------- AppMessage queue ---------- */

test('settings and weather never overlap on the AppMessage channel', function() {
  var env = makeEnv();
  env.storage.setItem(phoneLib.SETTINGS_KEY, JSON.stringify({ACCENT_COLOR: 2}));
  var p = phoneLib.createPhone(env);
  p.onReady(); /* settings send + weather fetch, as on every launch of 1.0.16 */
  respond(env.xhrs[0], 200, GOOD);
  eq(env.sent.length, 1, 'weather waits for the settings ack');
  env.sent[0].ok();
  eq(env.sent.length, 2, 'weather sent after ack');
  eq(env.sent[1].msg.TEMPERATURE, 18, 'rounded temperature');
  check(typeof env.sent[1].msg.WEATHER_TIME === 'number', 'observation time included');
  eq(env.inflightMax, 1, 'never more than one message in flight');
});

test('nacked messages retry with backoff then give up', function() {
  var env = makeEnv();
  var q = phoneLib.createQueue(env);
  var failed = 0;
  q.enqueue('weather', {TEMPERATURE: 1}, null, function() { failed++; });
  env.sent[0].fail();
  eq(env.sent.length, 1, 'no immediate resend');
  env.advance(T.RETRY_DELAYS_MS[0] - 1);
  eq(env.sent.length, 1, 'waits for first backoff');
  env.advance(1);
  eq(env.sent.length, 2, 'first retry');
  env.sent[1].fail();
  env.advance(T.RETRY_DELAYS_MS[1]);
  env.sent[2].fail();
  env.advance(T.RETRY_DELAYS_MS[2]);
  eq(env.sent.length, 4, 'three retries');
  env.sent[3].fail();
  env.advance(60000);
  eq(env.sent.length, 4, 'gave up');
  eq(failed, 1, 'onFail once');
  eq(q.pending(), 0, 'queue drained');
  eq(env.pendingTimers(), 0, 'no timers left');
});

test('lost ack/nack callbacks are caught by the send watchdog', function() {
  var env = makeEnv();
  var q = phoneLib.createQueue(env);
  q.enqueue('settings', {ACCENT_COLOR: 1});
  q.enqueue('weather', {TEMPERATURE: 3});
  env.advance(T.SEND_TIMEOUT_MS);
  eq(env.sent.length, 1, 'still one message after timeout (retry scheduled)');
  env.advance(T.RETRY_DELAYS_MS[0]);
  eq(env.sent.length, 2, 'settings retried after watchdog');
  eq(env.sent[1].msg, {ACCENT_COLOR: 1}, 'retry keeps order');
  env.sent[1].ok();
  eq(env.last().msg, {TEMPERATURE: 3}, 'weather follows');
  env.sent[0].ok(); /* a very late ack of the first attempt changes nothing */
  eq(env.sent.length, 3, 'late ack ignored');
});

test('newer payload of the same kind replaces a queued or retrying one', function() {
  var env = makeEnv();
  var q = phoneLib.createQueue(env);
  q.enqueue('settings', {ACCENT_COLOR: 1});
  q.enqueue('weather', {TEMPERATURE: 1});
  q.enqueue('weather', {TEMPERATURE: 2});
  env.sent[0].ok();
  eq(env.sent.length, 2, 'one weather send');
  eq(env.sent[1].msg, {TEMPERATURE: 2}, 'latest weather wins');
  env.sent[1].fail();
  q.enqueue('weather', {TEMPERATURE: 3});
  env.advance(T.RETRY_DELAYS_MS[0]);
  env.last().ok();
  env.advance(30000);
  eq(env.sent.map(function(r) { return r.msg.TEMPERATURE; }), [undefined, 2, 3], 'stale retry dropped');
});

/* ---------- weather ---------- */

test('relaunch with recently delivered weather does no GPS or HTTP', function() {
  var env = makeEnv();
  var p = phoneLib.createPhone(env);
  p.requestWeather('watch');
  respond(env.xhrs[0], 200, GOOD);
  env.sent[0].ok();
  check(JSON.parse(env.storage.getItem(phoneLib.WEATHER_KEY)).deliveredTs > 0, 'delivery recorded');
  env.advance(5 * 60 * 1000);
  /* notification closes, the watchface and its JS restart */
  var p2 = phoneLib.createPhone(env);
  eq(p2.requestWeather('ready'), 'skip-delivered', 'ready skips');
  eq(env.geoCalls, 1, 'no extra GPS');
  eq(env.xhrs.length, 1, 'no extra HTTP');
});

test('watch request with a recent fetch resends the cache with its original time', function() {
  var env = makeEnv();
  var p = phoneLib.createPhone(env);
  p.requestWeather('watch');
  respond(env.xhrs[0], 200, GOOD);
  var ts = env.sent[0].msg.WEATHER_TIME;
  env.sent[0].fail(); /* undelivered: watch asks again later */
  env.advance(60000);
  env.sent[1].fail();
  env.advance(60000);
  eq(p.requestWeather('watch'), 'cache', 'cache reused');
  eq(env.last().msg.WEATHER_TIME, ts, 'original observation time, not now');
  eq(env.geoCalls, 1, 'no GPS');
});

test('old cache triggers a real fetch; overlapping requests share it', function() {
  var env = makeEnv();
  var p = phoneLib.createPhone(env);
  env.storage.setItem(phoneLib.WEATHER_KEY, JSON.stringify({temp: 5, ts: Math.floor(env.now() / 1000) - 3600}));
  eq(p.requestWeather('ready'), 'fetch', 'fetches');
  eq(p.requestWeather('watch'), 'busy', 'second request does not start another GPS/HTTP');
  eq(env.geoCalls, 1, 'one GPS');
  respond(env.xhrs[0], 200, GOOD);
  check(!p.isFetching(), 'done');
  eq(p.requestWeather('watch'), 'cache', 'next request uses fresh cache');
});

test('HTTP failure, timeout and parse errors send nothing and free the fetch slot', function() {
  ['500', 'timeout', 'error', 'parse', 'missing'].forEach(function(kind) {
    var env = makeEnv();
    var p = phoneLib.createPhone(env);
    p.requestWeather('watch');
    var x = env.xhrs[0];
    eq(x.timeout, T.HTTP_TIMEOUT_MS, kind + ': XHR timeout set');
    if (kind === '500') respond(x, 500, '');
    else if (kind === 'timeout') x.ontimeout();
    else if (kind === 'error') x.onerror();
    else if (kind === 'parse') respond(x, 200, '<html>');
    else respond(x, 200, JSON.stringify({current: {}}));
    eq(env.sent.length, 0, kind + ': nothing sent');
    check(!p.isFetching(), kind + ': slot freed');
    eq(env.pendingTimers(), 0, kind + ': guard cleared');
  });
});

test('hung geolocation is abandoned by the guard and a late reply is ignored', function() {
  var env = makeEnv({geo: 'hang'});
  var p = phoneLib.createPhone(env);
  p.requestWeather('watch');
  env.advance(T.FETCH_GUARD_MS);
  check(!p.isFetching(), 'guard released the fetch');
  env.geo = 'ok';
  p.requestWeather('watch');
  eq(env.geoCalls, 2, 'a new fetch can start');
  /* a stale XHR from an abandoned attempt must not deliver */
  var env2 = makeEnv();
  var p2 = phoneLib.createPhone(env2);
  p2.requestWeather('watch');
  env2.advance(T.FETCH_GUARD_MS);
  respond(env2.xhrs[0], 200, GOOD);
  eq(env2.sent.length, 0, 'late reply after guard ignored');
});

test('location error ends the fetch without a message', function() {
  var env = makeEnv({geo: 'error'});
  var p = phoneLib.createPhone(env);
  eq(p.requestWeather('watch'), 'fetch', 'attempted');
  check(!p.isFetching(), 'finished');
  eq(env.sent.length, 0, 'nothing sent');
});

test('no permanent phone-side timers after activity settles', function() {
  var env = makeEnv();
  env.storage.setItem(phoneLib.SETTINGS_KEY, JSON.stringify({ACCENT_COLOR: 1}));
  var p = phoneLib.createPhone(env);
  p.onReady();
  respond(env.xhrs[0], 200, GOOD);
  env.sent[0].ok();
  env.sent[1].ok();
  eq(env.pendingTimers(), 0, 'idle');
});

/* ---------- hourly forecast ---------- */

var HOUR0 = 1760000400;
function hourly(n, over) {
  var h = {time: [], temperature_2m: [], precipitation_probability: [], weather_code: []};
  for (var i = 0; i < n; i++) {
    h.time.push(HOUR0 + i * 3600);
    h.temperature_2m.push(12.4 - i);
    h.precipitation_probability.push(i * 10);
    h.weather_code.push(i < 3 ? 3 : 61);
  }
  Object.keys(over || {}).forEach(function(k) { h[k] = over[k]; });
  return h;
}

test('ready refetches recently delivered legacy weather when forecast is missing', function() {
  var env = makeEnv();
  var now = Math.floor(env.now() / 1000);
  env.storage.setItem(phoneLib.WEATHER_KEY, JSON.stringify({temp: 18, ts: now, deliveredTs: now}));
  var p = phoneLib.createPhone(env);
  eq(p.onReady(), 'fetch', 'legacy cache must not suppress forecast fetch');
  eq(env.geoCalls, 1, 'refetch obtains fresh location and forecast');
});

test('weather fetch asks Open-Meteo for 12 hourly slots in unix time', function() {
  var env = makeEnv();
  var p = phoneLib.createPhone(env);
  p.requestWeather('watch');
  var url = env.xhrs[0].url;
  check(url.indexOf('hourly=temperature_2m,precipitation_probability,weather_code') > 0, 'hourly fields: ' + url);
  check(url.indexOf('forecast_hours=12') > 0, 'twelve hours');
  check(url.indexOf('timeformat=unixtime') > 0, 'unix times');
});

test('forecast travels with the temperature as byte arrays', function() {
  var env = makeEnv();
  var p = phoneLib.createPhone(env);
  p.requestWeather('watch');
  respond(env.xhrs[0], 200, JSON.stringify({current: {temperature_2m: 12.4}, hourly: hourly(14)}));
  var msg = env.sent[0].msg;
  eq(msg.TEMPERATURE, 12, 'current temperature');
  eq(msg.FORECAST_START, HOUR0, 'start hour');
  eq(msg.FORECAST_TEMPS.length, 12, 'capped at 12 hours');
  eq(msg.FORECAST_TEMPS.slice(0, 3), [12, 11, 10], 'rounded temperatures');
  eq(msg.FORECAST_RAIN.slice(0, 3), [0, 10, 20], 'rain chance');
  eq(msg.FORECAST_CODES.slice(2, 4), [3, 61], 'weather codes');
  var cached = JSON.parse(env.storage.getItem(phoneLib.WEATHER_KEY));
  eq(cached.fc.start, HOUR0, 'forecast cached with the reading');
  env.sent[0].fail();
  env.advance(60000);
  eq(p.requestWeather('watch'), 'cache', 'cache reused');
  eq(env.last().msg.FORECAST_TEMPS, msg.FORECAST_TEMPS, 'cache resend keeps the forecast');
});

test('below-zero forecast temperatures are sent as signed bytes', function() {
  var fc = phoneLib.parseForecast({hourly: hourly(2, {temperature_2m: [-3.2, -128.9]})});
  eq(fc.temps, [-3, -128], 'parsed and clamped');
  var env = makeEnv();
  var p = phoneLib.createPhone(env);
  p.requestWeather('watch');
  respond(env.xhrs[0], 200, JSON.stringify({current: {temperature_2m: -3}, hourly: hourly(2, {temperature_2m: [-3.2, 200]})}));
  eq(env.sent[0].msg.FORECAST_TEMPS, [253, 127], 'two\'s complement bytes');
});

test('forecast stops at missing or out-of-order hours and never invents data', function() {
  eq(phoneLib.parseForecast({}), null, 'no hourly block');
  eq(phoneLib.parseForecast({hourly: hourly(0)}), null, 'empty');
  eq(phoneLib.parseForecast({hourly: hourly(5, {temperature_2m: [10, 9, null, 7, 6]})}).temps, [10, 9], 'stops at a gap');
  var t = hourly(4).time; t[2] += 60;
  eq(phoneLib.parseForecast({hourly: hourly(4, {time: t})}).temps.length, 2, 'stops at a time jump');
  var fc = phoneLib.parseForecast({hourly: hourly(3, {precipitation_probability: [null, 140, -5], weather_code: [null, 2, 999]})});
  eq(fc.rain, [0, 100, 0], 'rain clamped, missing as 0');
  eq(fc.codes, [255, 2, 255], 'unknown code as 255');
  /* the current reading still goes out when the forecast is unusable */
  var env = makeEnv();
  var p = phoneLib.createPhone(env);
  p.requestWeather('watch');
  respond(env.xhrs[0], 200, JSON.stringify({current: {temperature_2m: 17.6}}));
  eq(env.sent[0].msg, {TEMPERATURE: 18, WEATHER_TIME: env.sent[0].msg.WEATHER_TIME}, 'temperature only');
});

test('shake settings are stored, clamped and on the config page', function() {
  eq(phoneLib.sanitizeSettings({SHAKE_SCREEN: 2, SHAKE_FX: 1}), {SHAKE_SCREEN: 2, SHAKE_FX: 1}, 'kept');
  eq(phoneLib.sanitizeSettings({SHAKE_SCREEN: 3, SHAKE_FX: 7}), {SHAKE_SCREEN: 2, SHAKE_FX: 0}, 'out of range');
  var html = phoneLib.buildConfigHtml({SHAKE_SCREEN: 1, SHAKE_FX: 1});
  check(html.indexOf('id="SHAKE_SCREEN"') > 0 && html.indexOf('id="SHAKE_FX"') > 0, 'selects present');
  check(html.indexOf('<option value="1" selected>Emergency Hex') > 0, 'current transition selected');
});

console.log('JS phone: ' + checks + ' checks, ' + failures + ' failures');
process.exit(failures ? 1 : 0);
