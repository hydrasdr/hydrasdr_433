/* ---- JSON parsing helper ---- */
function tryParse(s) {
	try { return JSON.parse(s); } catch (e) { return null; }
}

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
	if (m.samp_rate) parts.push((m.samp_rate / 1e3).toFixed(0) + ' kS/s');
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

/* ---- System meta display ---- */
/* Map meta keys to human-friendly labels and formatters */
var META_LABELS = {
	samp_rate:               ['Sample rate',      function (v) { return fmtSrate(v) + 'S/s'; }],
	conversion_mode:         ['Conversion',       function (v) { return ['Native', 'SI', 'Customary'][v] || v; }],
	fsk_pulse_detect_mode:   ['FSK pulse detect', null],
	report_time:             ['Report time',      function (v) { return v ? 'Yes' : 'No'; }],
	report_time_hires:       ['Hi-res time',      function (v) { return v ? 'On' : 'Off'; }],
	report_time_utc:         ['UTC time',         function (v) { return v ? 'On' : 'Off'; }],
	report_meta:             ['Signal level',     function (v) { return v ? 'On' : 'Off'; }],
	report_protocol:         ['Protocol #',       function (v) { return v ? 'On' : 'Off'; }],
	report_description:      ['Description',      function (v) { return v ? 'On' : 'Off'; }],
	stats_interval:          ['Stats interval',   function (v) { return v ? v + 's' : 'Off'; }],
	duration:                ['Duration',         function (v) { return v ? v + 's' : 'Continuous'; }]
};

function updateSystemMeta(m) {
	var lines = [];
	for (var key in META_LABELS) {
		if (m[key] === undefined) continue;
		var label = META_LABELS[key][0];
		var fmt = META_LABELS[key][1];
		var val = fmt ? fmt(m[key]) : m[key];
		lines.push(label + ': ' + val);
	}
	elSysMeta.textContent = lines.join('\n') || '--';

	/* Channelizer / wideband section */
	var wbSection = $('sys-wb-section');
	if (m.wideband_mode) {
		var wbLines = [];
		wbLines.push('Mode: Wideband (-B)');
		wbLines.push('Center: ' + fmtFreq(m.wideband_center));
		wbLines.push('Bandwidth: ' + fmtBW(m.wideband_bandwidth));
		wbLines.push('Channels: ' + m.wideband_channels);
		wbLines.push('Channel spacing: ' + fmtBW(m.wideband_bandwidth / m.wideband_channels));
		wbLines.push('Channel rate: ' + fmtBW(2 * m.samp_rate / m.wideband_channels) + 'ps');
		$('sys-wb').textContent = wbLines.join('\n');
		wbSection.style.display = '';
	} else {
		wbSection.style.display = 'none';
	}

	if (m.frequencies) {
		var frag = document.createDocumentFragment();
		for (var f = 0; f < m.frequencies.length; f++) {
			var chip = mkEl('span', 'freq-chip' + (m.frequencies[f] === m.center_frequency ? ' active' : ''), fmtFreq(m.frequencies[f]));
			frag.appendChild(chip);
		}
		elSysFreqs.innerHTML = '';
		elSysFreqs.appendChild(frag);
	}
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

function selectGainMode(mode) {
	gainMode = mode;
	var btns = document.querySelectorAll('.btn-gain');
	for (var i = 0; i < btns.length; i++)
		toggleClass(btns[i], 'active', btns[i].getAttribute('data-gain-mode') === mode);
	$('s-gain').disabled = (mode === 'auto');
	if (mode === 'auto') $('s-gain').value = '';
}

/* Mode button click */
bindAll('.btn-gain', 'click', function (e) {
	selectGainMode(e.currentTarget.getAttribute('data-gain-mode'));
});

/* ---- Apply buttons, presets, enter-key ---- */

var applyHandlers = {
	freq: function () {
		var raw = $('s-freq').value.trim();
		var hz;
		/* Try unit-aware parse first (handles 868.5M, 433k, etc.) */
		var parsed = parseValueWithUnit(raw);
		if (!isNaN(parsed) && parsed > 0) {
			hz = Math.round(parsed);
			/* If bare number < 10000, assume MHz (e.g. "433.92" → 433920000) */
			if (hz < 10000) hz = Math.round(parseFloat(raw) * 1e6);
		} else {
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
		rpc('center_frequency', {val: hz}, function () { refreshMeta(); refreshStats(); });
	},
	gain: function () {
		var arg;
		if (gainMode === 'auto') {
			arg = '0';
		} else {
			var v = $('s-gain').value.trim();
			if (v === '') return;
			arg = gainMode + '=' + v;
		}
		rpc('gain', {arg: arg}, function () { refreshMeta(); });
	},
	srate: function () {
		var raw = $('s-srate').value.trim();
		var hz = parseValueWithUnit(raw);
		/* If bare number without suffix and small, assume it's meant as-is (Hz) */
		if (isNaN(hz) || hz <= 0) return;
		hz = Math.round(hz);
		/* Guard: don't let refreshMeta overwrite this field */
		guardSetting('srate');
		if (metaCache) metaCache.samp_rate = hz;
		updateHeaderFromMeta(metaCache);
		rpc('sample_rate', {val: hz}, function () { refreshMeta(); refreshStats(); });
	},
	ppm: function () {
		var v = parseInt($('s-ppm').value, 10);
		rpc('ppm_error', {val: v});
	},
	hop: function () {
		var v = parseInt($('s-hop').value, 10);
		if (v >= 0) rpc('hop_interval', {val: v});
	},
	convert: function () {
		var v = parseInt($('s-convert').value, 10);
		rpc('convert', {val: v});
	},
	verbosity: function () {
		var v = parseInt($('s-verbosity').value, 10);
		rpc('verbosity', {val: v});
	},
	wideband: function () {
		var v = $('s-wideband').value.trim();
		if (v !== '') rpc('wideband', {arg: v}, function () { refreshMeta(); });
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
		rpc('wideband', {arg: wb}, function () { refreshMeta(); });
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
			/* Mark all groups dirty for full re-render */
			var gm;
			for (gm in monGroups) {
				if (monGroups.hasOwnProperty(gm)) monGroups[gm].needsRebuild = true;
			}
			for (gm in devGroups) {
				if (devGroups.hasOwnProperty(gm)) devGroups[gm].needsRebuild = true;
			}
			reRenderMonitorRows();
			reRenderDeviceRows();
			reRenderSyslogRows();
		});
	})(metaToggles[mi][0], metaToggles[mi][1]);
}

/* Bias-T checkbox */
$('s-biastee').addEventListener('change', function () {
	rpc('biastee', {val: this.checked ? 1 : 0});
});

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
