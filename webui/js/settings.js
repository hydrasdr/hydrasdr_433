/* ---- Header info ---- */
function updateHeaderFromMeta(m) {
	if (!m) return;
	var parts = [];
	if (m.wideband_mode) {
		parts.push('Wideband ' + fmtFreq(m.wideband_center) +
			' \u00b1' + fmtBW(m.wideband_bandwidth / 2) +
			' (' + m.wideband_channels + 'ch)');
	} else if (m.center_frequency) {
		parts.push(fmtFreq(m.center_frequency));
	}
	if (m.samp_rate) parts.push(fmtSrate(m.samp_rate) + 'S/s');
	if (hdrGainStr) parts.push(hdrGainStr);
	parts.push('Bias-T ' + (hdrBiasTee ? 'ON' : 'OFF'));
	if (hdrHopInterval > 0) parts.push('Hop ' + hdrHopInterval + 's');
	elInfo.textContent = parts.join('  |  ');
}

/* ---- Settings guard ---- */
/* Guard: after user applies freq or srate, skip overwriting that field
   briefly — the server's center_frequency / samp_rate may lag behind
   because the SDR hardware hasn't tuned yet. */
function guardSetting(key) {
	settingsGuard[key] = Date.now() + SETTINGS_GUARD_MS;
}

function isGuarded(key) {
	if (!settingsGuard[key]) return false;
	if (Date.now() < settingsGuard[key]) return true;
	delete settingsGuard[key];
	return false;
}

/* ---- Populate settings from meta ---- */
function populateSettingsFromMeta(m) {
	/* Use frequencies[0] if available (updates immediately),
	   fall back to center_frequency (may lag on hardware) */
	if (!isGuarded('freq')) {
		var freq = (m.frequencies && m.frequencies.length > 0) ? m.frequencies[0] : m.center_frequency;
		if (freq) $('s-freq').value = (freq / 1e6).toFixed(3);
	}
	if (!isGuarded('srate')) {
		if (m.samp_rate) $('s-srate').value = fmtSrate(m.samp_rate);
	}
	if (m.conversion_mode !== undefined) $('s-convert').value = m.conversion_mode;
	/* Populate wideband field from meta */
	if (m.wideband_mode && m.wideband_center && m.wideband_bandwidth && m.wideband_channels) {
		$('s-wideband').value = fmtFreq(m.wideband_center) + ':' +
			fmtBW(m.wideband_bandwidth) + ':' + m.wideband_channels;
	} else {
		$('s-wideband').value = '';
	}
	/* Report option checkboxes are display-only — do NOT sync from server.
	   Server always has all options ON; checkboxes control client visibility.
	   Display flags default to false (simplified view). */
}

/* ---- Refresh header + settings from server after a setting change ---- */
function refreshMeta() {
	rpc('get_meta', null, function (r) {
		if (r) {
			var m = (typeof r === 'string') ? tryParse(r) : r;
			if (!m) return;
			/* Preserve guarded fields: the server may not yet reflect
			   the value we just applied (SDR retune is async). */
			if (isGuarded('freq') && metaCache) {
				m.center_frequency = metaCache.center_frequency;
				m.frequencies = metaCache.frequencies;
			}
			if (isGuarded('srate') && metaCache) {
				m.samp_rate = metaCache.samp_rate;
			}
			metaCache = m;
			updateHeaderFromMeta(metaCache);
			populateSettingsFromMeta(metaCache);
			updateSystemMeta(metaCache);
		}
	});
}

/* ---- Gain mode state ---- */
var gainMode = 'auto'; /* 'auto' | 'linearity' | 'sensitivity' */

/* Apply min/max/step/placeholder from device gain ranges to the input */
function applyGainInputRange() {
	var el = $('s-gain');
	var gr = gainRanges[gainMode];
	if (gr && gr.max > 0) {
		el.type = 'number';
		el.min = gr.min;
		el.max = gr.max;
		el.step = gr.step || 1;
		el.placeholder = gr.def || gr.min;
		el.title = gainMode.charAt(0).toUpperCase() + gainMode.slice(1) +
			' gain: ' + gr.min + '\u2013' + gr.max +
			(gr.step > 1 ? ' (step ' + gr.step + ')' : '');
	} else {
		el.type = (gainMode === 'auto') ? 'text' : 'number';
		el.removeAttribute('min');
		el.removeAttribute('max');
		el.step = '1';
		el.placeholder = (gainMode === 'auto') ? '' : '10';
		el.title = '';
	}
}

function selectGainMode(mode) {
	gainMode = mode;
	var btns = document.querySelectorAll('.btn-gain');
	for (var i = 0; i < btns.length; i++)
		toggleClass(btns[i], 'active', btns[i].getAttribute('data-gain-mode') === mode);
	$('s-gain').disabled = (mode === 'auto');
	if (mode === 'auto') $('s-gain').value = '';
	applyGainInputRange();
}

/* Mode button click */
bindAll('.btn-gain', 'click', function (e) {
	selectGainMode(e.currentTarget.getAttribute('data-gain-mode'));
});

/* ---- Apply buttons, presets, enter-key ---- */

/* ---- RPC result handler: toast + optional follow-up ---- */
function rpcToast(label, afterOk) {
	return function (result, error) {
		if (error) {
			showToast(label + ': ' + (error.message || 'Error'), 'err');
		} else {
			showToast(label, 'ok');
			if (afterOk) afterOk();
		}
	};
}

var applyHandlers = {
	freq: function () {
		var raw = $('s-freq').value.trim();
		var hz;
		/* Try unit-aware parse first (handles 868.5M, 433k, etc.) */
		var parsed = parseValueWithUnit(raw);
		if (!isNaN(parsed) && parsed > 0) {
			hz = Math.round(parsed);
			/* If bare number < 10000, assume MHz (e.g. "433.92" → 433920000) */
			if (hz < FREQ_MHZ_THRESH) hz = Math.round(parseFloat(raw) * 1e6);
		} else {
			showToast('Invalid frequency', 'warn');
			return;
		}
		/* Guard: don't let refreshMeta overwrite this field */
		guardSetting('freq');
		/* Immediate local update for instant feedback */
		if (metaCache) {
			metaCache.center_frequency = hz;
			if (metaCache.frequencies) metaCache.frequencies[0] = hz;
		}
		updateHeaderFromMeta(metaCache);
		rpc('center_frequency', {val: hz}, rpcToast('Frequency set to ' + fmtFreq(hz), function () {
			refreshMeta(); refreshStats();
		}));
	},
	gain: function () {
		var arg;
		if (gainMode === 'auto') {
			arg = '0';
			hdrGainStr = 'AGC Auto';
		} else {
			var v = $('s-gain').value.trim();
			if (v === '') {
				showToast('Enter a gain value', 'warn');
				return;
			}
			var gv = parseInt(v, 10);
			var gr = gainRanges[gainMode];
			if (isNaN(gv) || (gr && gr.max > 0 && (gv < gr.min || gv > gr.max))) {
				showToast('Gain must be ' + (gr && gr.max > 0 ? gr.min + '\u2013' + gr.max : 'a valid number'), 'warn');
				return;
			}
			arg = gainMode + '=' + gv;
			hdrGainStr = gainMode.charAt(0).toUpperCase() + gainMode.slice(1) + ' ' + gv;
		}
		var label = gainMode === 'auto' ? 'Gain set to Auto' : 'Gain set to ' + arg;
		updateHeaderFromMeta(metaCache);
		rpc('gain', {arg: arg}, rpcToast(label, refreshMeta));
	},
	srate: function () {
		var raw = $('s-srate').value.trim();
		var hz = parseValueWithUnit(raw);
		/* If bare number without suffix and small, assume it's meant as-is (Hz) */
		if (isNaN(hz) || hz <= 0) {
			showToast('Invalid sample rate', 'warn');
			return;
		}
		hz = Math.round(hz);
		/* Guard: don't let refreshMeta overwrite this field */
		guardSetting('srate');
		if (metaCache) metaCache.samp_rate = hz;
		updateHeaderFromMeta(metaCache);
		rpc('sample_rate', {val: hz}, rpcToast('Sample rate set to ' + fmtSrate(hz) + 'S/s', function () {
			refreshMeta(); refreshStats();
		}));
	},
	ppm: function () {
		var v = parseInt($('s-ppm').value, 10);
		if (isNaN(v)) { showToast('Invalid PPM value', 'warn'); return; }
		rpc('ppm_error', {val: v}, rpcToast('PPM set to ' + v));
	},
	hop: function () {
		var v = parseInt($('s-hop').value, 10);
		if (isNaN(v) || v < 0) { showToast('Invalid hop interval', 'warn'); return; }
		hdrHopInterval = v;
		updateHeaderFromMeta(metaCache);
		rpc('hop_interval', {val: v}, rpcToast('Hop interval set to ' + v + 's'));
	},
	convert: function () {
		var v = parseInt($('s-convert').value, 10);
		var labels = ['Native', 'SI', 'Customary'];
		rpc('convert', {val: v}, rpcToast('Conversion: ' + (labels[v] || v)));
	},
	verbosity: function () {
		var v = parseInt($('s-verbosity').value, 10);
		rpc('verbosity', {val: v}, rpcToast('Verbosity set to ' + v));
	},
	wideband: function () {
		var v = $('s-wideband').value.trim();
		if (v === '') { showToast('Enter a wideband spec', 'warn'); return; }
		rpc('wideband', {arg: v}, rpcToast('Wideband: ' + v, refreshMeta));
	}
};

bindAll('.btn-apply', 'click', function (e) {
	var key = e.currentTarget.getAttribute('data-apply');
	if (applyHandlers[key]) applyHandlers[key]();
});

/* ---- Frequency, Sample Rate & Wideband Presets ---- */
bindAll('.btn-preset', 'click', function (e) {
	var btn = e.currentTarget;
	var freq = btn.getAttribute('data-freq');
	if (freq) {
		$('s-freq').value = freq;
		applyHandlers.freq();
		return;
	}
	var sr = btn.getAttribute('data-srate');
	if (sr) {
		$('s-srate').value = fmtSrate(parseInt(sr, 10));
		applyHandlers.srate();
		return;
	}
	var wb = btn.getAttribute('data-wideband');
	if (wb) {
		$('s-wideband').value = (wb === 'off') ? '' : wb;
		var label = (wb === 'off') ? 'Wideband off' : 'Wideband: ' + wb;
		rpc('wideband', {arg: wb}, rpcToast(label, refreshMeta));
	}
});

/* ---- Enter Key for Apply ---- */
bindAll('.field-row input, .field-row select', 'keydown', function (e) {
	if (e.keyCode === 13) {
		e.preventDefault();
		var row = e.currentTarget.parentNode;
		if (!row) return;
		var btn = row.querySelector('.btn-apply');
		if (!btn) return;
		var key = btn.getAttribute('data-apply');
		if (key && applyHandlers[key]) applyHandlers[key]();
	}
});

/* ---- Report meta checkboxes — display-only (server always sends full data) ---- */
var metaToggles = [
	['s-meta-level', 'level'],
	['s-meta-bits', 'bits'],
	['s-meta-proto', 'protocol'],
	['s-meta-desc', 'description'],
	['s-meta-hires', 'hires']
];
for (var mi = 0; mi < metaToggles.length; mi++) {
	(function (id, arg) {
		$(id).addEventListener('change', function () {
			if (arg === 'level')       showMetaLevel = this.checked;
			else if (arg === 'bits')   showMetaBits = this.checked;
			else if (arg === 'protocol') showMetaProto = this.checked;
			else if (arg === 'description') showMetaDesc = this.checked;
			else if (arg === 'hires') showMetaHires = this.checked;
			notifyDisplayChange();
		});
	})(metaToggles[mi][0], metaToggles[mi][1]);
}

/* Bias-T checkbox */
$('s-biastee').addEventListener('change', function () {
	var on = this.checked;
	hdrBiasTee = on;
	updateHeaderFromMeta(metaCache);
	rpc('biastee', {val: on ? 1 : 0}, rpcToast('Bias-T ' + (on ? 'enabled' : 'disabled')));
});

/* ---- Configurable limits (persisted to localStorage) ---- */
var LIMIT_DEFS = [
	{id: 's-lim-events',     key: 'lim_events',     varName: 'MAX_EVENTS',        min: 50, max: 10000},
	{id: 's-lim-devices',    key: 'lim_devices',     varName: 'MAX_DEVICES',       min: 50, max: 5000},
	{id: 's-lim-dev-events', key: 'lim_dev_events',  varName: 'MAX_DEVICE_EVENTS', min: 20, max: 2000},
	{id: 's-lim-syslog',     key: 'lim_syslog',      varName: 'SYSLOG_MAX',        min: 50, max: 2000},
	{id: 's-lim-chart',      key: 'lim_chart',       varName: 'CHART_MAX_EVENTS',  min: 500, max: 20000}
];

/* Map var names to their global references for assignment */
function setLimitVar(name, val) {
	if (name === 'MAX_EVENTS')        MAX_EVENTS = val;
	else if (name === 'MAX_DEVICES')  MAX_DEVICES = val;
	else if (name === 'MAX_DEVICE_EVENTS') MAX_DEVICE_EVENTS = val;
	else if (name === 'SYSLOG_MAX')   SYSLOG_MAX = val;
	else if (name === 'CHART_MAX_EVENTS') CHART_MAX_EVENTS = val;
}

function getLimitVar(name) {
	if (name === 'MAX_EVENTS')        return MAX_EVENTS;
	if (name === 'MAX_DEVICES')       return MAX_DEVICES;
	if (name === 'MAX_DEVICE_EVENTS') return MAX_DEVICE_EVENTS;
	if (name === 'SYSLOG_MAX')        return SYSLOG_MAX;
	if (name === 'CHART_MAX_EVENTS')  return CHART_MAX_EVENTS;
	return 0;
}

/* Load saved limits from localStorage; populate inputs */
for (var li = 0; li < LIMIT_DEFS.length; li++) {
	(function (def) {
		var saved = null;
		try { saved = localStorage.getItem('hydrasdr_' + def.key); } catch (e) {}
		if (saved !== null) {
			var v = parseInt(saved, 10);
			if (!isNaN(v) && v >= def.min && v <= def.max) setLimitVar(def.varName, v);
		}
		$(def.id).value = getLimitVar(def.varName);
		$(def.id).addEventListener('change', function () {
			var v = parseInt(this.value, 10);
			if (isNaN(v) || v < def.min) v = def.min;
			if (v > def.max) v = def.max;
			this.value = v;
			setLimitVar(def.varName, v);
			try { localStorage.setItem('hydrasdr_' + def.key, v); } catch (e) {}
		});
	})(LIMIT_DEFS[li]);
}

/* ---- Debug enable checkbox in Settings → Advanced ---- */
if (elDbgCheckbox) {
	elDbgCheckbox.addEventListener('change', function () {
		debugEnabled = elDbgCheckbox.checked;
		if (elDbgTabBtn) elDbgTabBtn.style.display = debugEnabled ? '' : 'none';
		/* If disabling and debug panel is open, close it */
		if (!debugEnabled && overlayPanels.debug && overlayPanels.debug.open) {
			toggleOverlay('debug');
		}
	});
}
