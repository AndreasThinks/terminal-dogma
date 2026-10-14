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
var GOOD = JSON.stringify({current: {temperature_2m: 17.6}});

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

console.log('JS phone: ' + checks + ' checks, ' + failures + ' failures');
process.exit(failures ? 1 : 0);
