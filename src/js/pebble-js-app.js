var isReady = false;
var isFetching = false;
var callbacks = [];

var temp_unit = {
  f: 0,
  c: 1
};

var style = {
  fuzzy: 0,
  human:   1,
  machine:  2
};
var alignment = {
  center: 0,
  left:   1,
  right:  2
};
var weather = {
  off: 0,
  on_15: 15,
  on_30: 30,
  on_60: 59
};


var locationOptions = { "timeout": 150000, "maximumAge": 600000 };

// WMO weather codes \u2014 https://open-meteo.com/en/docs \u2014 collapsed to short
// labels that fit the watchface's topbar string.
function wmoToIcon(code) {
  if (code === 0)                       return "Clear";
  if (code >= 1  && code <= 3)          return "Cloudy";
  if (code === 45 || code === 48)       return "Fog";
  if (code >= 51 && code <= 57)         return "Drizzle";
  if (code >= 61 && code <= 67)         return "Rain";
  if (code >= 71 && code <= 77)         return "Snow";
  if (code >= 80 && code <= 82)         return "Rain";
  if (code === 85 || code === 86)       return "Snow";
  if (code >= 95)                       return "Storm";
  return "Unknown";
}

function fetchWeather(latitude, longitude) {
  var curTime = Math.floor((new Date).getTime() / 1000);
  var lastFetch = parseInt(localStorage.getItem("lastFetch") || "0", 10);

  if (isFetching) {
    console.log("fetchWeather: already fetching, quit");
    return;
  }
  if (curTime - lastFetch < 900) {
    console.log("fetchWeather: cached, last fetch " + (curTime - lastFetch) + "s ago");
    return;
  }

  isFetching = true;
  console.log("fetchWeather: requesting Open-Meteo");

  // Open-Meteo: free, no API key, HTTPS. Returns current temperature in C
  // and a WMO weather code; we map the code to a short label and compute F
  // locally so the watch can pick based on temp_unit.
  var url = "https://api.open-meteo.com/v1/forecast"
          + "?latitude=" + latitude
          + "&longitude=" + longitude
          + "&current=temperature_2m,weather_code";

  var req = new XMLHttpRequest();
  req.open('GET', url, true);
  req.onload = function() {
    isFetching = false;
    if (req.readyState !== 4) return;
    if (req.status !== 200) {
      console.log("fetchWeather: HTTP " + req.status);
      return;
    }
    try {
      var data = JSON.parse(req.responseText);
      var tempC = Math.round(data.current.temperature_2m);
      var tempF = Math.round(tempC * 9 / 5 + 32);
      var icon  = wmoToIcon(data.current.weather_code);
      console.log("fetchWeather: " + icon + " " + tempC + "C / " + tempF + "F");
      localStorage.setItem("lastFetch", curTime);
      transmitConfiguration({
        "icon": icon,
        "temperatureC": tempC + "\u00B0C",
        "temperatureF": tempF + "\u00B0F"
      });
    } catch (e) {
      console.log("fetchWeather: parse error " + e);
    }
  };
  req.onerror = function() {
    isFetching = false;
    console.log("fetchWeather: network error");
  };
  req.send(null);
}

function locationSuccess(pos) {
  var coordinates = pos.coords;
  var datetime = "======= lastsync: " + new Date();
  console.log(datetime);
  if(!isFetching)fetchWeather(coordinates.latitude, coordinates.longitude);
}

function locationError(err) {
  console.warn('location error (' + err.code + '): ' + err.message);
  transmitConfiguration({
    "icon":"no data",
    "temperatureC":"01234",
    "temperatureF":"01234"
    });
}

function readyCallback(event) {
  console.log("Wristquake JS ready");
  isReady = true;
  while (callbacks.length > 0) {
    try { callbacks.shift()(event); } catch (e) { console.log("ready callback error: " + e); }
  }
  // Kick off an initial weather fetch once JS is up. Without this, the
  // first reveal-on-shake stays weather-less until the C side ticks the
  // 15-minute weather interval.
  if (window.navigator && window.navigator.geolocation) {
    try {
      window.navigator.geolocation.getCurrentPosition(locationSuccess, locationError, locationOptions);
    } catch (e) {
      console.log("geolocation throw: " + e);
    }
  } else {
    console.log("no geolocation API available");
  }
}

// Retrieves stored configuration from localStorage.
function getOptions() {
  return localStorage.getItem("options") || ("{}");
}

// Stores options in localStorage.
function setOptions(options) {
  localStorage.setItem("options", options);
}

// Takes a string containing serialized JSON as input.  Returns an
// appmessage payload for the watch — only includes keys whose source
// values resolve to a valid int/string. Undefined values are skipped
// because Pebble.sendAppMessage can wedge the JS engine on them, which
// then breaks settings page loading, weather fetches, and the
// double-tap refresh round-trip.
function prepareConfiguration(serialized_settings) {
  var settings = {};
  try { settings = JSON.parse(serialized_settings) || {}; } catch (_) { settings = {}; }

  var msg = {
    // Always-defined booleans.
    "1": settings.bluetooth ? 1 : 0,
    "8": (typeof settings.show_steps === 'undefined') ? 1 : (settings.show_steps ? 1 : 0)
  };
  if (alignment.hasOwnProperty(settings.text_align))   msg["0"] = alignment[settings.text_align];
  if (weather.hasOwnProperty(settings.weather))        msg["2"] = weather[settings.weather];
  if (temp_unit.hasOwnProperty(settings.temp_unit))    msg["6"] = temp_unit[settings.temp_unit];
  if (style.hasOwnProperty(settings.text_style))       msg["7"] = style[settings.text_style];
  return msg;
}

// Takes a JSON message as input.  Sends the message to the watch.
function transmitConfiguration(settings) {
  Pebble.sendAppMessage(settings, function(event) {
  }, logError);
}

function logError(event) {
  console.log('Unable to deliver message with transactionId='+
              event.data.transactionId +' ; Error is'+ event.error.message);
}


function showConfiguration(event) {
    // Self-contained — does not depend on any other JS init succeeding.
    // The rePebble app fires "showConfiguration" when the user taps the
    // gear icon; if our handler doesn't call Pebble.openURL the app sits
    // forever on "loading watchface".
    var url = "https://gloompirate.github.io/pebble-wristquake/";
    var opts = "{}";
    try { opts = localStorage.getItem("options") || "{}"; } catch (_) {}
    try {
      console.log("Wristquake showConfiguration -> " + url);
      Pebble.openURL(url + "#options=" + encodeURIComponent(opts));
    } catch (e) {
      console.log("openURL threw: " + e);
    }
}

function webviewclosed(event) {
  var resp = event.response;
  console.log('configuration response: '+ resp + ' ('+ typeof resp +')');
  if (!resp) return; // user hit Cancel; response is empty

  var options;
  try { options = JSON.parse(resp); } catch (_) { return; }

  if (typeof options.bluetooth === 'undefined' &&
      typeof options.text_style === 'undefined' &&
      typeof options.text_align === 'undefined' &&
      typeof options.temp_unit === 'undefined' &&
      typeof options.weather === 'undefined' &&
      typeof options.show_steps === 'undefined') {
    return;
  }

  onReady(function() {
    setOptions(resp);

    var message = prepareConfiguration(resp);
    transmitConfiguration(message);
  });
}

function appmessage(event) {
  // The watch fires app_message_outbox_send on the minute-tick weather
  // interval and on the second tap of a double-tap reveal. Re-fetch
  // weather here so those actions actually do something.
  console.log("Wristquake appmessage");
  if (isFetching) return;
  if (!window.navigator || !window.navigator.geolocation) {
    console.log("appmessage: no geolocation API");
    return;
  }
  try {
    window.navigator.geolocation.getCurrentPosition(locationSuccess, locationError, locationOptions);
  } catch (e) {
    console.log("appmessage: geolocation throw " + e);
  }
}

function onReady(callback) {
  if (isReady) {
    callback();
  }
  else {
    callbacks.push(callback);
  }
}

Pebble.addEventListener("ready", readyCallback);
Pebble.addEventListener("showConfiguration", showConfiguration);
Pebble.addEventListener("webviewclosed", webviewclosed);
Pebble.addEventListener("appmessage", appmessage);

onReady(function(event) {
  var message = prepareConfiguration(getOptions());
  transmitConfiguration(message);
});