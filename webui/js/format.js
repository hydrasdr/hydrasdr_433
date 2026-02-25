/* ---- Time formatting ---- */
function nowTime() {
	var d = new Date();
	return pad2(d.getHours()) + ':' + pad2(d.getMinutes()) + ':' + pad2(d.getSeconds())
		+ '.' + pad6(d.getMilliseconds() * 1000);
}
function pad2(n) { return n < 10 ? '0' + n : '' + n; }
function pad3(n) { return n < 10 ? '00' + n : n < 100 ? '0' + n : '' + n; }
function pad6(n) { var s = '' + n; while (s.length < 6) s = '0' + s; return s; }

/* Format a time string for display.
   Handles all server formats consistently:
   - "2026-02-24 12:34:56.789" (full datetime + hi-res)
   - "2026-02-24 12:34:56"     (full datetime)
   - "12:34:56.789"            (time only + hi-res)
   - "12:34:56"                (time only)
   Extracts just the time portion, shows/hides fractional seconds. */
function formatTime(t) {
	if (!t) return '';
	/* Extract time portion (after last space, if full datetime) */
	var sp = t.lastIndexOf(' ');
	var tp = sp !== -1 ? t.substring(sp + 1) : t;
	if (showMetaHires) return tp;
	/* Remove fractional seconds */
	var dot = tp.lastIndexOf('.');
	return dot !== -1 ? tp.substring(0, dot) : tp;
}

/* ---- Value formatting ---- */

/* Smart format: O(1) lookup table dispatch.
   Returns {text} or {dot} or null if no special formatting. */
var VALUE_FORMATS = {
	/* Temperature */
	temperature_C:   function (v) { return {text: v + '\u00b0C'}; },
	temperature_F:   function (v) { return {text: v + '\u00b0F'}; },
	temperature_1_C: function (v) { return {text: v + '\u00b0C'}; },
	temperature_2_C: function (v) { return {text: v + '\u00b0C'}; },
	temperature_3_C: function (v) { return {text: v + '\u00b0C'}; },
	temperature_4_C: function (v) { return {text: v + '\u00b0C'}; },
	temperature_1_F: function (v) { return {text: v + '\u00b0F'}; },
	temperature_2_F: function (v) { return {text: v + '\u00b0F'}; },
	setpoint_C:      function (v) { return {text: v + '\u00b0C'}; },
	/* Humidity & moisture */
	humidity:        function (v) { return {text: v + '%'}; },
	humidity_1:      function (v) { return {text: v + '%'}; },
	humidity_2:      function (v) { return {text: v + '%'}; },
	moisture:        function (v) { return {text: v + '%'}; },
	/* Battery */
	battery_ok:      function (v) { return {dot: (v === 1 || v === '1') ? 'batt-ok' : 'batt-low'}; },
	/* Wind */
	wind_avg_km_h:   function (v) { return {text: v + ' km/h'}; },
	wind_max_km_h:   function (v) { return {text: v + ' km/h'}; },
	wind_avg_m_s:    function (v) { return {text: v + ' m/s'}; },
	wind_max_m_s:    function (v) { return {text: v + ' m/s'}; },
	wind_avg_mi_h:   function (v) { return {text: v + ' mph'}; },
	wind_max_mi_h:   function (v) { return {text: v + ' mph'}; },
	wind_dir_deg:    function (v) { return {text: v + '\u00b0'}; },
	/* Rain */
	rain_mm:         function (v) { return {text: v + ' mm'}; },
	rain_in:         function (v) { return {text: v + ' in'}; },
	rain_rate_mm_h:  function (v) { return {text: v + ' mm/h'}; },
	rain_rate_in_h:  function (v) { return {text: v + ' in/h'}; },
	/* Pressure */
	pressure_hPa:    function (v) { return {text: v + ' hPa'}; },
	pressure_kPa:    function (v) { return {text: v + ' kPa'}; },
	pressure_PSI:    function (v) { return {text: v + ' PSI'}; },
	pressure_bar:    function (v) { return {text: v + ' bar'}; },
	/* Light & UV */
	light_lux:       function (v) { return {text: v + ' lux'}; },
	light_klx:       function (v) { return {text: v + ' klx'}; },
	uvi:             function (v) { return {text: v + ' UV'}; },
	uv_index:        function (v) { return {text: v + ' UV'}; },
	/* Energy & power */
	power_W:         function (v) { return {text: v + ' W'}; },
	energy_kWh:      function (v) { return {text: v + ' kWh'}; },
	current_A:       function (v) { return {text: v + ' A'}; },
	voltage_V:       function (v) { return {text: v + ' V'}; },
	/* Air quality */
	co2_ppm:         function (v) { return {text: v + ' ppm'}; },
	hcho_ppb:        function (v) { return {text: v + ' ppb'}; },
	pm1_ug_m3:       function (v) { return {text: v + ' \u00b5g/m\u00b3'}; },
	pm2_5_ug_m3:     function (v) { return {text: v + ' \u00b5g/m\u00b3'}; },
	pm4_ug_m3:       function (v) { return {text: v + ' \u00b5g/m\u00b3'}; },
	pm10_ug_m3:      function (v) { return {text: v + ' \u00b5g/m\u00b3'}; },
	pm10_0_ug_m3:    function (v) { return {text: v + ' \u00b5g/m\u00b3'}; },
	/* Volume & depth */
	depth_cm:        function (v) { return {text: v + ' cm'}; },
	volume_m3:       function (v) { return {text: v + ' m\u00b3'}; },
	/* Distance (lightning/storm) */
	storm_dist:      function (v) { return {text: v + ' km'}; },
	strike_distance: function (v) { return {text: v + ' km'}; }
};
function smartFormatValue(key, val) {
	var fn = VALUE_FORMATS[key];
	return fn ? fn(val) : null;
}

/* ---- Unit-aware value parser ---- */
/* Accepts numbers with optional k/M/G suffix, e.g. "1M", "250k", "2.5G",
   "433.92", "868.5M". Returns the value in base units (Hz) or NaN. */
function parseValueWithUnit(str) {
	if (!str) return NaN;
	str = str.trim();
	var m = str.match(/^([0-9]*\.?[0-9]+)\s*([kKmMgG])?[Hh]?[Zz]?[Ss]?[/]?[Ss]?$/);
	if (!m) return NaN;
	var val = parseFloat(m[1]);
	if (isNaN(val)) return NaN;
	var suffix = (m[2] || '').toUpperCase();
	if (suffix === 'K') return val * 1e3;
	if (suffix === 'M') return val * 1e6;
	if (suffix === 'G') return val * 1e9;
	return val;
}

/* Format a Hz value to friendly display with unit suffix */
function fmtSrate(hz) {
	if (hz >= 1e6 && hz % 1e6 === 0) return (hz / 1e6) + 'M';
	if (hz >= 1e6) return (hz / 1e6).toFixed(3).replace(/0+$/, '').replace(/\.$/, '') + 'M';
	if (hz >= 1e3 && hz % 1e3 === 0) return (hz / 1e3) + 'k';
	if (hz >= 1e3) return (hz / 1e3).toFixed(1).replace(/0+$/, '').replace(/\.$/, '') + 'k';
	return hz + '';
}

function fmtBW(hz) {
	if (hz >= 1e6) return (hz / 1e6).toFixed(1) + ' MHz';
	if (hz >= 1e3) return (hz / 1e3).toFixed(0) + ' kHz';
	return hz + ' Hz';
}

function fmtFreq(hz) {
	if (hz >= 1e9) return (hz / 1e9).toFixed(6) + ' GHz';
	if (hz >= 1e6) return (hz / 1e6).toFixed(3) + ' MHz';
	if (hz >= 1e3) return (hz / 1e3).toFixed(1) + ' kHz';
	return hz + ' Hz';
}

/* Format stat labels: snake_case → Title Case */
function fmtStatLabel(s) {
	return String(s).replace(/_/g, ' ').replace(/\b\w/g, function (c) { return c.toUpperCase(); });
}

/* Format stat values with context-aware units */
function fmtStatValue(key, value) {
	if (typeof value === 'object') return JSON.stringify(value);
	var v = value;
	/* Large integer formatting */
	if (typeof v === 'number' && v >= 10000 && Number.isInteger(v)) {
		return v.toLocaleString();
	}
	/* Float formatting: max 3 decimal places */
	if (typeof v === 'number' && !Number.isInteger(v)) {
		return v.toFixed(3);
	}
	return String(v);
}

/* ---- Model color coding (cached) ---- */
var _colorCache = {};
function modelColor(name) {
	if (_colorCache[name]) return _colorCache[name];
	var hash = 0;
	for (var i = 0; i < name.length; i++) {
		hash = name.charCodeAt(i) + ((hash << 5) - hash);
	}
	var hue = ((hash % 360) + 360) % 360;
	var c = 'hsl(' + hue + ', 55%, 65%)';
	_colorCache[name] = c;
	return c;
}

/* ---- Signal bar ---- */
function sigPct(db) {
	var v = +db;
	if (v !== v) return 0; /* NaN check */
	if (v < 0) return Math.max(0, Math.min(100, ((v + 30) / 29) * 100));
	return Math.max(0, Math.min(100, (v / 30) * 100));
}

/* Build a signal-strength bar element from a message's rssi/snr.
   Returns a span element or null if level display is off / no signal data.
   Uses cloneNode from a cached template for faster hot-path creation. */
var _sigBarTpl = null;
function buildSigBar(msg) {
	if (!showMetaLevel) return null;
	var sigVal = msg.rssi !== undefined ? msg.rssi : msg.snr;
	if (sigVal === undefined) return null;
	if (!_sigBarTpl) _sigBarTpl = document.createElement('span');
	var pct = sigPct(sigVal);
	var bar = _sigBarTpl.cloneNode(false);
	bar.className = 'sig-bar ' + (pct > 60 ? 'sig-hi' : pct > 30 ? 'sig-mid' : 'sig-lo');
	bar.style.width = Math.max(4, (pct * 0.6) | 0) + 'px';
	bar.title = sigVal + ' dB';
	return bar;
}

/* ---- CSV helpers ---- */
function csvEscape(val) {
	var s = String(val === null || val === undefined ? '' : val);
	if (s.indexOf('"') !== -1 || s.indexOf(',') !== -1 || s.indexOf('\n') !== -1 || s.indexOf('\r') !== -1) {
		return '"' + s.replace(/"/g, '""') + '"';
	}
	return s;
}

function downloadCSV(filename, content) {
	var blob = new Blob([content], {type: 'text/csv;charset=utf-8;'});
	var url = URL.createObjectURL(blob);
	var a = document.createElement('a');
	a.href = url;
	a.download = filename;
	a.style.display = 'none';
	document.body.appendChild(a);
	a.click();
	document.body.removeChild(a);
	URL.revokeObjectURL(url);
}

/* ---- Consolidated visible key filter ---- */
/* Collect visible data keys from a message, respecting all meta toggles.
   Replaces getVisibleMsgKeys + getVisibleEventKeys (identical logic). */
function getVisibleKeys(msg) {
	var keys = [];
	for (var k in msg) {
		if (!msg.hasOwnProperty(k) || SKIP_KEYS[k]) continue;
		if (!showMetaLevel && META_SIGNAL_KEYS[k]) continue;
		if (!showMetaBits && k === META_BITS_KEY) continue;
		if (!showMetaProto && k === META_PROTO_KEY) continue;
		if (!showMetaDesc && k === META_DESC_KEY) continue;
		keys.push(k);
	}
	return keys;
}

/* Collect the union of visible data keys across an array of events.
   Returns an ordered array of key names (order of first appearance). */
function getVisibleEventKeys(events) {
	var seen = {};
	var keys = [];
	for (var i = 0; i < events.length; i++) {
		var vk = getVisibleKeys(events[i]);
		for (var j = 0; j < vk.length; j++) {
			if (!seen[vk[j]]) {
				seen[vk[j]] = 1;
				keys.push(vk[j]);
			}
		}
	}
	return keys;
}

/* ---- Shared cell filling ---- */

/* Ensure a TR has exactly n cells (add/remove TDs as needed) */
function ensureCells(tr, n) {
	while (tr.childNodes.length < n) tr.appendChild(document.createElement('td'));
	while (tr.childNodes.length > n) tr.removeChild(tr.lastChild);
}

/* Fill data columns of a Monitor/Device row from a message and key list.
   startIdx = index of first data cell in the TR. */
function fillDataCells(tr, msg, dataKeys, startIdx) {
	for (var c = 0; c < dataKeys.length; c++) {
		var td = tr.childNodes[startIdx + c];
		if (!td) break;
		var k = dataKeys[c];
		var v = msg[k];
		td.textContent = '';
		td.style.color = '';
		if (v === undefined) continue;
		if (typeof v === 'object') v = JSON.stringify(v);
		var fmt = smartFormatValue(k, v);
		if (fmt && fmt.dot) {
			var dot = document.createElement('span');
			dot.className = 'batt-dot ' + fmt.dot;
			td.appendChild(dot);
		} else {
			td.textContent = (fmt && fmt.text) ? fmt.text : String(v);
		}
	}
}
