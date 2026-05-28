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

function fetchWeather(latitude, longitude) {
  var curTime = Math.floor((new Date).getTime()/1000);
  var lastFetch = localStorage.getItem("lastFetch");

  if (isFetching) {
    console.log("fetchWeather: already fetching, quit");
    return;
  }
  else if (curTime - lastFetch < 900) {
    console.log("fetchWeather: already fetched recently, quit");
    return;
  }
  else {
    isFetching = true;
    console.log("fetchWeather: fetching now");

    var response;
    var req = new XMLHttpRequest();
    req.open('GET', "http://api.openweathermap.org/data/2.5/find?" +
               "lat=" + latitude + "&lon=" + longitude + "&cnt=1", true);
    req.onload = function(e) {
      if (req.readyState == 4) {
        if(req.status == 200) {
          console.log(req.responseText);
          response = JSON.parse(req.responseText);
          var temperatureC, temperatureF, icon, city;
          if (response && response.list && response.list.length > 0) {
            var weatherResult = response.list[0];
            temperatureC = Math.round(weatherResult.main.temp - 273.15);
            temperatureF = Math.round((weatherResult.main.temp*1.8) - 459.67);
            icon = weatherResult.weather[0].main;
            city = weatherResult.name;
            console.log(temperatureC);
            console.log(temperatureF);
            console.log(icon);
            console.log(city);
            localStorage.setItem("lastFetch", curTime);
            transmitConfiguration({
              "icon":icon,
              "temperatureC":"" + temperatureC+"\u00B0C",
              "temperatureF":"" + temperatureF+"\u00B0F"
              });
          }
        } else {
          console.log("Error");
        }
      }
    };
    req.send(null);
    isFetching = false;
    console.log("fetchWeather: finished fetching, quit");
  }
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