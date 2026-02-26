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
