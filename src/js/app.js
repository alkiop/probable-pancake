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

// Serialised message queue: Pebble drops sendAppMessage calls made while a
// previous one is in-flight.  Without this, road or weather data is silently
// lost whenever two async fetches resolve close together.
var _msgQueue = [];
var _msgPending = false;

function _drainQueue() {
    if (_msgPending || _msgQueue.length === 0) return;
    _msgPending = true;
    var msg = _msgQueue.shift();
    Pebble.sendAppMessage(msg,
        function() { _msgPending = false; _drainQueue(); },
        function(e) {
            console.log('[citibike] send failed: ' + JSON.stringify(e));
            _msgPending = false;
            _drainQueue();
        });
}

function sendToWatch(msg) {
    _msgQueue.push(msg);
    _drainQueue();
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
        WarningMsg: '',
        MapData: buildMapData()
    };

    if (outOfRange) {
        var meters = Math.round(entry.distance);
        var mins = Math.max(1, Math.round(entry.distance / WALK_METERS_PER_MIN));
        msg.WarningMsg = 'Far: ' + meters + 'm / ' + mins + ' min walk';
    }

    sendToWatch(msg);
}

// Builds the map payload: nearby stations as metre offsets from the user.
// Format: "dlat,dlon,color;..." color: 0 red, 1 yellow, 2 green, 3 = selected.
// Returned as a string so it can ride inside the station message — AppMessage
// only allows one in-flight message, so separate back-to-back sends get dropped.
function buildMapData() {
    // Out-of-range: the only station is far outside the 250m map radius; don't
    // show a misleading dot clamped to the map edge.
    if (userLat === null || nearbyList.length === 0 || outOfRange) return '';
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
    return parts.join(';');
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

// Fetches nearby road geometry from Overpass (OpenStreetMap) and sends
// line segments in pixel coordinates to the watch for the map overlay.
// Each segment: "x1,y1,x2,y2" in the map layer's pixel space (144x88).
function fetchRoads(lat, lon) {
    var cosLat = Math.cos(lat * Math.PI / 180);
    var query = '[out:json][timeout:8];(way["highway"~"^(primary|secondary|tertiary|residential|unclassified|service)$"](around:300,' +
        lat.toFixed(6) + ',' + lon.toFixed(6) + '););out geom;';
    var url = 'https://overpass-api.de/api/interpreter?data=' + encodeURIComponent(query);

    fetchJSON(url, function(err, data) {
        if (err || !data || !data.elements || data.elements.length === 0) return;

        var segs = [];
        var maxTotal = 80;
        var els = data.elements;

        for (var i = 0; i < els.length && segs.length < maxTotal; i++) {
            var geom = els[i].geometry;
            if (!geom || geom.length < 2) continue;

            // Limit each way to 8 segments; stride-sample if longer.
            var maxPerWay = Math.min(8, geom.length - 1);
            var stride = Math.max(1, Math.floor((geom.length - 1) / maxPerWay));

            for (var j = 0; j + 1 < geom.length && segs.length < maxTotal; j += stride) {
                var k = Math.min(j + stride, geom.length - 1);
                var p1 = geom[j], p2 = geom[k];

                // Convert lat/lon to pixel coords in the map layer (144x88, centre=user).
                // Same scale as station dots: half=44px covers RADIUS_METERS=250m.
                var rx1 = Math.round(72 + (p1.lon - lon) * 111111 * cosLat * 44 / 250);
                var ry1 = Math.round(44 - (p1.lat - lat) * 111111 * 44 / 250);
                var rx2 = Math.round(72 + (p2.lon - lon) * 111111 * cosLat * 44 / 250);
                var ry2 = Math.round(44 - (p2.lat - lat) * 111111 * 44 / 250);

                // Both endpoints off the same edge → clamping would produce a
                // false line hugging the map border; skip the segment entirely.
                if ((rx1 < 0 && rx2 < 0) || (rx1 > 143 && rx2 > 143) ||
                    (ry1 < 0 && ry2 < 0) || (ry1 > 87  && ry2 > 87)) continue;

                var x1 = Math.max(0, Math.min(143, rx1));
                var y1 = Math.max(0, Math.min(87,  ry1));
                var x2 = Math.max(0, Math.min(143, rx2));
                var y2 = Math.max(0, Math.min(87,  ry2));

                if (x1 === x2 && y1 === y2) continue; // zero-length after clamp
                segs.push(x1 + ',' + y1 + ',' + x2 + ',' + y2);
            }
        }

        if (segs.length > 0) {
            sendToWatch({ RoadData: segs.join(';') });
        }
    });
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
            fetchWeather(userLat, userLon);
            fetchRoads(userLat, userLon);
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
    } else if (msg.RequestPrev) {
        if (!dataReady || nearbyList.length === 0) return;
        currentIndex = (currentIndex - 1 + nearbyList.length) % nearbyList.length;
        sendCurrentStation();
    }
});
