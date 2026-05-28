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
  isReady = true;
  var callback;
  while (callbacks.length > 0) {
    callback = callbacks.shift();
    callback(event);
  window.navigator.geolocation.getCurrentPosition(locationSuccess, locationError, locationOptions);
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

// Takes a string containing serialized JSON as input.  This is the
// format that is sent back from the configuration web UI.  Produces
// a JSON message to send to the watch face.
function prepareConfiguration(serialized_settings) {
  var settings = JSON.parse(serialized_settings);
  // appKey 8 = CONF_SHOWSTEPS. Defaults on so Wristquake's step display is
  // visible out of the box when running against the original upstream config
  // page (which has no show_steps checkbox). Override by setting
  // `show_steps: false` in localStorage or via a future Clay config UI.
  var showSteps = (typeof settings.show_steps === 'undefined') ? 1
                : (settings.show_steps ? 1 : 0);
  return {
    "0": alignment[settings.text_align],
    "1": settings.bluetooth ? 1 : 0,
    "2": weather[settings.weather],
    "6": temp_unit[settings.temp_unit],
    "7": style[settings.text_style],
    "8": showSteps
  };
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
    var opts = getOptions();
    // Wristquake config page lives in this repo under /docs and is served
    // via GitHub Pages over HTTPS. Moved off the upstream zecoj.github.io
    // page so we can add new appKeys (showSteps) and not depend on an
    // HTTP-only host the Pebble app may block.
    var url  = "https://gloompirate.github.io/pebble-wristquake/";
    console.log(opts);
    Pebble.openURL(url + "#options=" + encodeURIComponent(opts));
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
  if(!isFetching)window.navigator.geolocation.getCurrentPosition(locationSuccess, locationError, locationOptions);
  console.log("message!");
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