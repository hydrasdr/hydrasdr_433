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

/* Smart format keys: returns {text, html} or null if no special formatting */
function smartFormatValue(key, val) {
	/* Temperature */
	if (key === 'temperature_C') {
		return {text: val + '\u00b0C'};
	}
	if (key === 'temperature_F') {
		return {text: val + '\u00b0F'};
	}
	/* Humidity */
	if (key === 'humidity') {
		return {text: val + '%'};
	}
	/* Battery */
	if (key === 'battery_ok') {
		return {dot: (val === 1 || val === '1') ? 'batt-ok' : 'batt-low'};
	}
	/* Wind speed */
	if (key === 'wind_avg_km_h' || key === 'wind_max_km_h') {
		return {text: val + ' km/h'};
	}
	if (key === 'wind_avg_m_s' || key === 'wind_max_m_s') {
		return {text: val + ' m/s'};
	}
	if (key === 'wind_avg_mi_h' || key === 'wind_max_mi_h') {
		return {text: val + ' mph'};
	}
	/* Wind direction */
	if (key === 'wind_dir_deg') {
		return {text: val + '\u00b0'};
	}
	/* Rain */
	if (key === 'rain_mm') {
		return {text: val + ' mm'};
	}
	if (key === 'rain_in') {
		return {text: val + ' in'};
	}
	/* Pressure */
	if (key === 'pressure_hPa') {
		return {text: val + ' hPa'};
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

/* ---- Model color coding ---- */
function modelColor(name) {
	var hash = 0;
	for (var i = 0; i < name.length; i++) {
		hash = name.charCodeAt(i) + ((hash << 5) - hash);
	}
	var hue = ((hash % 360) + 360) % 360;
	return 'hsl(' + hue + ', 55%, 65%)';
}

/* ---- Signal bar ---- */
function sigPct(db) {
	var v = +db;
	if (v !== v) return 0; /* NaN check */
	if (v < 0) return Math.max(0, Math.min(100, ((v + 30) / 29) * 100));
	return Math.max(0, Math.min(100, (v / 30) * 100));
}

/* Build a signal-strength bar element from a message's rssi/snr.
   Returns a span element or null if level display is off / no signal data. */
function buildSigBar(msg) {
	if (!showMetaLevel) return null;
	var sigVal = msg.rssi !== undefined ? msg.rssi : msg.snr;
	if (sigVal === undefined) return null;
	var pct = sigPct(sigVal);
	var bar = document.createElement('span');
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
