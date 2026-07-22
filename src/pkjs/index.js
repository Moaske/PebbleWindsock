// Wind Forecast - PebbleKit JS companion
// On each REQUEST from the watch: get GPS location, reverse-geocode it to a
// friendly place name, fetch Open-Meteo (KNMI Harmonie) forecast, send both
// back to the watch in one AppMessage.

function pad(n) {
  return n < 10 ? '0' + n : '' + n;
}

function sendError(code) {
  Pebble.sendAppMessage(
    { 'ERROR': code },
    function() {},
    function(e) { console.log('Failed to send error: ' + JSON.stringify(e)); }
  );
}

function buildRowsPayload(json) {
  var times = json.hourly.time;             // e.g. "2026-07-22T17:00"
  var wind10 = json.hourly.wind_speed_10m;
  var wind100 = json.hourly.wind_speed_100m;
  var gusts = json.hourly.wind_gusts_10m;
  var dir10 = json.hourly.wind_direction_10m;

  var now = new Date();
  var nowIso = now.getFullYear() + '-' + pad(now.getMonth() + 1) + '-' +
               pad(now.getDate()) + 'T' + pad(now.getHours()) + ':00';

  var startIdx = times.indexOf(nowIso);
  if (startIdx === -1) {
    startIdx = 0;
    for (var i = 0; i < times.length; i++) {
      if (new Date(times[i]) >= now) { startIdx = i; break; }
    }
  }

  var endIdx = Math.min(startIdx + 24, times.length);
  var rows = [];
  for (var j = startIdx; j < endIdx; j++) {
    var hour = parseInt(times[j].substring(11, 13), 10);
    rows.push(hour + ',' + Math.round(wind10[j]) + ',' +
              Math.round(gusts[j]) + ',' + Math.round(wind100[j]) + ',' +
              Math.round(dir10[j]));
  }
  return rows.join('|');
}

function fetchLocationName(lat, lon, callback) {
  var url = 'https://api.bigdatacloud.net/data/reverse-geocode-client' +
            '?latitude=' + lat + '&longitude=' + lon + '&localityLanguage=en';
  var xhr = new XMLHttpRequest();
  xhr.onload = function() {
    if (xhr.status === 200) {
      try {
        var data = JSON.parse(xhr.responseText);
        var name = data.city || data.locality || data.principalSubdivision || 'Unknown location';
        callback(name);
      } catch (e) {
        callback('Unknown location');
      }
    } else {
      callback('Unknown location');
    }
  };
  xhr.onerror = function() { callback('Unknown location'); };
  xhr.timeout = 10000;
  xhr.ontimeout = function() { callback('Unknown location'); };
  xhr.open('GET', url);
  xhr.send();
}

function fetchForecast(lat, lon, callback) {
  var url = 'https://api.open-meteo.com/v1/forecast' +
             '?latitude=' + lat + '&longitude=' + lon +
             '&hourly=wind_speed_10m,wind_speed_100m,wind_gusts_10m,wind_direction_10m' +
             '&models=knmi_seamless' +
             '&wind_speed_unit=kmh' +
             '&timezone=auto' +
             '&forecast_days=2';

  var xhr = new XMLHttpRequest();
  xhr.onload = function() {
    if (xhr.status === 200) {
      try {
        var json = JSON.parse(xhr.responseText);
        callback(null, buildRowsPayload(json));
      } catch (e) {
        callback('PARSE_ERR');
      }
    } else {
      callback('HTTP_' + xhr.status);
    }
  };
  xhr.onerror = function() { callback('NET_ERR'); };
  xhr.timeout = 15000;
  xhr.ontimeout = function() { callback('TIMEOUT'); };
  xhr.open('GET', url);
  xhr.send();
}

function fetchAndSend() {
  navigator.geolocation.getCurrentPosition(
    function(pos) {
      var lat = pos.coords.latitude.toFixed(4);
      var lon = pos.coords.longitude.toFixed(4);

      var locationName = null;
      var forecastPayload = null;
      var forecastErr = null;
      var pending = 2;

      function maybeSend() {
        pending--;
        if (pending > 0) { return; }
        if (forecastErr) {
          sendError(forecastErr);
          return;
        }
        Pebble.sendAppMessage(
          {
            'FORECAST_DATA': forecastPayload,
            'LOCATION_NAME': locationName || 'Unknown location'
          },
          function() { console.log('Forecast + location sent'); },
          function(e) { console.log('Send failed: ' + JSON.stringify(e)); }
        );
      }

      fetchLocationName(lat, lon, function(name) {
        locationName = name;
        maybeSend();
      });

      fetchForecast(lat, lon, function(err, payload) {
        if (err) { forecastErr = err; } else { forecastPayload = payload; }
        maybeSend();
      });
    },
    function() { sendError('GPS_ERR'); },
    { timeout: 15000, maximumAge: 60000, enableHighAccuracy: false }
  );
}

Pebble.addEventListener('ready', function() {
  console.log('Wind Forecast JS ready');
});

Pebble.addEventListener('appmessage', function(e) {
  if (e.payload && e.payload.REQUEST) {
    fetchAndSend();
  }
});
