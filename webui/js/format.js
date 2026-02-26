/* ---- Time formatting ---- */
function nowTime() {
	var d = new Date();
	return pad2(d.getHours()) + ':' + pad2(d.getMinutes()) + ':' + pad2(d.getSeconds())
		+ '.' + pad3(d.getMilliseconds());
}
function pad2(n) { return n < 10 ? '0' + n : '' + n; }
function pad3(n) { return n < 10 ? '00' + n : n < 100 ? '0' + n : '' + n; }

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

/* Special-case fields that cannot be derived from suffix patterns. */
var SPECIAL_FORMATS = {
	/* Dot indicators (non-text display) */
	battery_ok:      function (v) { return {dot: (v === 1 || v === '1') ? 'batt-ok' : 'batt-low'}; },
	/* Fields without unit suffix in name */
	humidity:        function (v) { return {text: v + '%'}; },
	humidity_1:      function (v) { return {text: v + '%'}; },
	humidity_2:      function (v) { return {text: v + '%'}; },
	moisture:        function (v) { return {text: v + '%'}; },
	uvi:             function (v) { return {text: v + ' UV'}; },
	uv_index:        function (v) { return {text: v + ' UV'}; },
	storm_dist:      function (v) { return {text: v + ' km'}; },
	strike_distance: function (v) { return {text: v + ' km'}; }
};

/* Suffix-to-unit mapping, ordered longest suffix first so the first
   match wins.  This makes the webui self-adapting: any new device field
   whose name ends with a recognised unit suffix is formatted automatically. */
var SUFFIX_UNITS = [
	/* Multi-char suffixes first (longest match wins) */
	['_ug_m3', ' \u00b5g/m\u00b3'],   /* µg/m³ */
	['_km_h',  ' km/h'],
	['_mi_h',  ' mph'],
	['_mm_h',  ' mm/h'],
	['_in_h',  ' in/h'],
	['_m_s',   ' m/s'],
	['_kWh',   ' kWh'],
	['_hPa',   ' hPa'],
	['_kPa',   ' kPa'],
	['_PSI',   ' PSI'],
	['_klx',   ' klx'],
	['_ppm',   ' ppm'],
	['_ppb',   ' ppb'],
	['_deg',   '\u00b0'],              /* ° */
	['_pct',   '%'],
	['_bar',   ' bar'],
	['_gal',   ' gal'],
	['_lux',   ' lux'],
	['_mm',    ' mm'],
	['_in',    ' in'],
	['_cm',    ' cm'],
	['_m3',    ' m\u00b3'],            /* m³ */
	['_km',    ' km'],
	['_dB',    ' dB'],
	['_Hz',    ' Hz'],
	['_mV',    ' mV'],
	['_VA',    ' VA'],
	['_C',     '\u00b0C'],             /* °C */
	['_F',     '\u00b0F'],             /* °F */
	['_W',     ' W'],
	['_V',     ' V'],
	['_A',     ' A'],
	['_s',     ' s']
];

/* Smart format: special-case lookup then suffix-based pattern match.
   Returns {text} or {dot} or null if no special formatting. */
function smartFormatValue(key, val) {
	var fn = SPECIAL_FORMATS[key];
	if (fn) return fn(val);
	for (var i = 0; i < SUFFIX_UNITS.length; i++) {
		var sfx = SUFFIX_UNITS[i];
		if (key.length > sfx[0].length &&
		    key.indexOf(sfx[0], key.length - sfx[0].length) !== -1) {
			return {text: val + sfx[1]};
		}
	}
	return null;
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
	if (v < 0) return Math.max(0, Math.min(100, ((v + SIG_DB_OFFSET) / SIG_DB_RANGE) * 100));
	return Math.max(0, Math.min(100, (v / SIG_DB_OFFSET) * 100));
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
	bar.className = 'sig-bar ' + (pct > SIG_THRESH_HI ? 'sig-hi' : pct > SIG_THRESH_MID ? 'sig-mid' : 'sig-lo');
	bar.style.width = Math.max(SIG_BAR_MIN_PX, (pct * 0.6) | 0) + 'px';
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

/* ---- JSON parsing helper ---- */
function tryParse(s) {
	try { return JSON.parse(s); } catch (e) { return null; }
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
	var _t0 = performance.now();
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
	perfMetrics.fillDataCellsMs += performance.now() - _t0;
}
