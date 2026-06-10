// Citi Bike Pebble App — phone-side logic
// Fetches GBFS station data, finds nearby stations, responds to watch requests.

var GBFS_BASE = 'https://gbfs.citibikenyc.com/gbfs/en/';
var RADIUS_METERS = 250;
var WALK_METERS_PER_MIN = 80;

var STATION_DB = [];        // [{id, name, lat, lon}]
var STATION_STATUS = null;  // {station_id: status}
var dataReady = false;

var nearbyList = [];        // [{station, distance}] sorted by distance asc
var currentIndex = 0;
var outOfRange = false;
var pendingNearest = false;
var userLat = null, userLon = null;

function haversine(lat1, lon1, lat2, lon2) {
    var R = 6371000, toRad = Math.PI / 180;
    var dLat = (lat2 - lat1) * toRad, dLon = (lon2 - lon1) * toRad;
    var a = Math.sin(dLat / 2) * Math.sin(dLat / 2) +
        Math.cos(lat1 * toRad) * Math.cos(lat2 * toRad) *
        Math.sin(dLon / 2) * Math.sin(dLon / 2);
    return 2 * R * Math.asin(Math.sqrt(a));
}

function fetchJSON(url, cb) {
    var xhr = new XMLHttpRequest();
    xhr.open('GET', url, true);
    xhr.onload = function() {
        if (xhr.status === 200) {
            try { cb(null, JSON.parse(xhr.responseText)); }
            catch (e) { cb(e, null); }
        } else {
            cb(new Error('HTTP ' + xhr.status), null);
        }
    };
    xhr.onerror = function() { cb(new Error('network'), null); };
    xhr.send();
}

function sendToWatch(msg) {
    Pebble.sendAppMessage(msg,
        function() {},
        function(e) { console.log('[citibike] send failed: ' + JSON.stringify(e)); });
}

function sendError(code) {
    sendToWatch({ ErrorCode: code });
}

function buildNearbyList(lat, lon) {
    nearbyList = [];
    outOfRange = false;
    var nearest = null, nearestDist = Infinity;

    for (var i = 0; i < STATION_DB.length; i++) {
        var d = haversine(lat, lon, STATION_DB[i].lat, STATION_DB[i].lon);
        if (d <= RADIUS_METERS) {
            nearbyList.push({ station: STATION_DB[i], distance: d });
        }
        if (d < nearestDist) {
            nearestDist = d;
            nearest = STATION_DB[i];
        }
    }

    nearbyList.sort(function(a, b) { return a.distance - b.distance; });

    // Nothing in range: fall back to the single nearest station with a warning
    if (nearbyList.length === 0 && nearest) {
        nearbyList.push({ station: nearest, distance: nearestDist });
        outOfRange = true;
    }
    currentIndex = 0;
}

function sendCurrentStation() {
    if (nearbyList.length === 0) {
        sendError(2);
        return;
    }
    var entry = nearbyList[currentIndex];
    var status = STATION_STATUS ? STATION_STATUS[entry.station.id] : null;
    if (!status) {
        sendError(3);
        return;
    }

    var bikes = status.num_bikes_available || 0;
    var ebikes = status.num_ebikes_available || 0;
    var docks = status.num_docks_available || 0;

    // Keep total bundle string under ~79 chars; trim long station names
    var name = entry.station.name;
    if (name.length > 40) name = name.substring(0, 39);

    var msg = {
        Bundle: name + '|' + bikes + '|' + ebikes + '|' + docks,
        StationIndex: currentIndex + 1,
        StationCount: nearbyList.length,
        ErrorCode: 0,
        WarningMsg: ''
    };

    if (outOfRange) {
        var meters = Math.round(entry.distance);
        var mins = Math.max(1, Math.round(entry.distance / WALK_METERS_PER_MIN));
        msg.WarningMsg = 'Far: ' + meters + 'm / ' + mins + ' min walk';
    }

    sendToWatch(msg);
}

// Sends nearby stations as offsets (metres) from the user for the map view.
// Format: "dlat,dlon,color;..." color: 0 red, 1 yellow, 2 green, 3 = selected
function sendMapData() {
    if (userLat === null || nearbyList.length === 0) return;
    var cosLat = Math.cos(userLat * Math.PI / 180);
    var parts = [];
    var limit = Math.min(nearbyList.length, 8);
    for (var i = 0; i < limit; i++) {
        var s = nearbyList[i].station;
        var dlat = Math.round((s.lat - userLat) * 111111);
        var dlon = Math.round((s.lon - userLon) * 111111 * cosLat);
        var st = STATION_STATUS ? STATION_STATUS[s.id] : null;
        var bikes = st ? (st.num_bikes_available || 0) : 0;
        var c = (i === currentIndex) ? 3 : (bikes === 0 ? 0 : (bikes < 5 ? 1 : 2));
        parts.push(dlat + ',' + dlon + ',' + c);
    }
    sendToWatch({ MapData: parts.join(';') });
}

// Maps an Open-Meteo WMO weather code + temperature to a short alert banner.
// Empty string means "no adverse conditions".
function weatherAlert(code, tempC) {
    if (code >= 95) return '! Storm';
    if (code >= 85) return '! Snow Showers';
    if (code >= 80) return '! Rain Showers';
    if (code >= 71 && code <= 77) return '! Snow';
    if (code >= 61 && code <= 67) return '! Rain';
    if (code >= 51 && code <= 57) return '! Drizzle';
    if (code >= 45 && code <= 48) return '! Fog';
    if (tempC > 35) return '! Heat Advisory';
    if (tempC < -10) return '! Cold Alert';
    return '';
}

function fetchWeather(lat, lon) {
    var url = 'https://api.open-meteo.com/v1/forecast?latitude=' + lat +
        '&longitude=' + lon + '&current_weather=true';
    fetchJSON(url, function(err, data) {
        if (err || !data || !data.current_weather) return;
        var cw = data.current_weather;
        var alert = weatherAlert(cw.weathercode, cw.temperature);
        sendToWatch({ WeatherAlert: alert });
    });
}

function locateAndSend() {
    navigator.geolocation.getCurrentPosition(
        function(pos) {
            userLat = pos.coords.latitude;
            userLon = pos.coords.longitude;
            buildNearbyList(userLat, userLon);
            sendCurrentStation();
            sendMapData();
            fetchWeather(userLat, userLon);
        },
        function(err) {
            console.log('[citibike] geolocation failed: ' + (err && err.message));
            sendError(1);
        },
        { timeout: 15000, maximumAge: 60000, enableHighAccuracy: true }
    );
}

function handleNearest() {
    if (!dataReady) {
        pendingNearest = true;
        return;
    }
    locateAndSend();
}

function loadData() {
    fetchJSON(GBFS_BASE + 'station_information.json', function(err, info) {
        if (err) {
            console.log('[citibike] station_information failed: ' + err);
            sendError(3);
            return;
        }
        STATION_DB = info.data.stations.map(function(s) {
            return {
                id: s.station_id,
                name: s.name,
                lat: parseFloat(s.lat),
                lon: parseFloat(s.lon)
            };
        });

        fetchJSON(GBFS_BASE + 'station_status.json', function(err2, statusData) {
            if (err2) {
                console.log('[citibike] station_status failed: ' + err2);
                sendError(3);
                return;
            }
            STATION_STATUS = statusData.data.stations.reduce(function(m, s) {
                m[s.station_id] = s;
                return m;
            }, {});
            dataReady = true;

            if (pendingNearest) {
                pendingNearest = false;
                locateAndSend();
            }
        });
    });
}

Pebble.addEventListener('ready', function() {
    console.log('[citibike] JS ready');
    // The watch requests on launch, but it may have fired before JS was up —
    // treat 'ready' as an implicit nearest request so the app always loads.
    pendingNearest = true;
    loadData();
});

Pebble.addEventListener('appmessage', function(e) {
    var msg = e.payload || {};

    if (msg.UseNearest) {
        handleNearest();
    } else if (msg.RequestNext) {
        if (!dataReady || nearbyList.length === 0) return;
        currentIndex = (currentIndex + 1) % nearbyList.length;
        sendCurrentStation();
        sendMapData();
    } else if (msg.RequestPrev) {
        if (!dataReady || nearbyList.length === 0) return;
        currentIndex = (currentIndex - 1 + nearbyList.length) % nearbyList.length;
        sendCurrentStation();
        sendMapData();
    }
});
