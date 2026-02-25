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
