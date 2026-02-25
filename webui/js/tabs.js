/* PROTOCOLS — DocumentFragment, no innerHTML */

function renderProtocols(list) {
	var filter = elProtoSearch.value.toLowerCase();
	var frag = document.createDocumentFragment();
	var shown = 0;
	var ookCount = 0;
	var fskCount = 0;
	var totalCount = list.length;

	for (var i = 0; i < list.length; i++) {
		var p = list[i];
		var name = p.name || '';
		var num = p.num !== undefined ? p.num : '';
		var modIdx = p.mod || 0;

		/* Count by modulation */
		if (modIdx === 1) ookCount++;
		else if (modIdx >= 2) fskCount++;

		if (filter && name.toLowerCase().indexOf(filter) === -1 &&
				('' + num).indexOf(filter) === -1) continue;
		shown++;

		var tr = document.createElement('tr');
		tr.className = 'proto-clickable';
		if (!p.en) addClass(tr, 'proto-disabled');
		tr._protoData = p;
		tr._protoDetailRow = null;

		/* # */
		var td1 = document.createElement('td');
		td1.className = 'col-num';
		td1.textContent = num;
		tr.appendChild(td1);

		/* Name */
		var td2 = document.createElement('td');
		td2.textContent = name;
		tr.appendChild(td2);

		/* Modulation tag */
		var td3 = document.createElement('td');
		var tag = document.createElement('span');
		tag.className = 'mod-tag ' + (modIdx === 1 ? 'mod-ook' : modIdx >= 2 ? 'mod-fsk' : 'mod-other');
		tag.textContent = MOD_NAMES[modIdx] || 'Other';
		td3.appendChild(tag);
		tr.appendChild(td3);

		/* Enabled dot */
		var td4 = document.createElement('td');
		td4.className = 'col-en';
		var dot = document.createElement('span');
		dot.className = 'en-dot ' + (p.en ? 'on' : 'off');
		td4.appendChild(dot);
		tr.appendChild(td4);

		frag.appendChild(tr);
	}

	/* Single DOM swap */
	elProtoBody.innerHTML = '';
	elProtoBody.appendChild(frag);
	elProtoCount.textContent = shown + ' / ' + totalCount + ' protocols';

	/* Modulation count breakdown */
	updateProtoModCounts(ookCount, fskCount, totalCount);
}

function updateProtoModCounts(ook, fsk, total) {
	elProtoModCounts.innerHTML = '';
	elProtoModCounts.appendChild(mkEl('span', 'mod-cnt-ook', ook + ' OOK'));
	elProtoModCounts.appendChild(document.createTextNode(' / '));
	elProtoModCounts.appendChild(mkEl('span', 'mod-cnt-fsk', fsk + ' FSK'));
	elProtoModCounts.appendChild(document.createTextNode(' / '));
	elProtoModCounts.appendChild(document.createTextNode(total + ' total'));
}

/* ---- Protocol: Row click to expand detail ---- */
elProtoBody.addEventListener('click', function (e) {
	var tr = e.target;
	while (tr && tr.tagName !== 'TR') {
		tr = tr.parentNode;
	}
	if (!tr || !tr._protoData) return;
	/* Don't expand detail rows */
	if (hasClass(tr, 'proto-detail-row')) return;

	toggleProtoDetail(tr);
});

function buildDetailPairs(items, keyFn, valFn) {
	var wrap = mkEl('div', 'proto-detail-pairs');
	for (var i = 0; i < items.length; i++) {
		var pair = mkEl('span', 'proto-detail-pair');
		var k = keyFn ? keyFn(items[i]) : null;
		if (k) pair.appendChild(mkEl('span', 'proto-detail-key', k + ': '));
		pair.appendChild(mkEl('span', 'proto-detail-val', valFn(items[i])));
		wrap.appendChild(pair);
	}
	return wrap;
}
function buildProtoSection(title, items, keyFn, valFn) {
	var sec = mkEl('div', 'proto-detail-section');
	sec.appendChild(mkEl('div', 'proto-detail-label', title));
	sec.appendChild(buildDetailPairs(items, keyFn, valFn));
	return sec;
}

function toggleProtoDetail(tr) {
	/* If already expanded, collapse */
	if (tr._protoDetailRow) {
		if (tr._protoDetailRow.parentNode) {
			tr._protoDetailRow.parentNode.removeChild(tr._protoDetailRow);
		}
		tr._protoDetailRow = null;
		removeClass(tr, 'proto-expanded');
		return;
	}

	/* Expand: create detail sub-row */
	addClass(tr, 'proto-expanded');
	var detailTr = mkEl('tr', 'proto-detail-row');
	var detailTd = document.createElement('td');
	detailTd.setAttribute('colspan', '4');

	var content = mkEl('div', 'proto-detail-content');
	var p = tr._protoData;

	/* Fields section */
	if (p.fields && p.fields.length > 0) {
		content.appendChild(buildProtoSection('Fields', p.fields, null, function (f) { return f; }));
	}

	/* Signal parameters section */
	var sigKeys = ['short', 'long', 'reset', 'gap', 'sync', 'tolerance',
		's_short', 's_long', 's_reset', 's_gap', 's_sync', 's_tolerance'];
	var sigItems = [];
	for (var si = 0; si < sigKeys.length; si++) {
		if (p[sigKeys[si]] !== undefined) sigItems.push(sigKeys[si]);
	}
	if (sigItems.length > 0) {
		content.appendChild(buildProtoSection('Signal Parameters', sigItems,
			function (k) { return k; }, function (k) { return String(p[k]); }));
	}

	/* All other properties section */
	var knownKeys = {name:1, num:1, mod:1, en:1, fields:1};
	for (var oi = 0; oi < sigKeys.length; oi++) knownKeys[sigKeys[oi]] = 1;
	var otherKeys = [];
	for (var ok in p) {
		if (p.hasOwnProperty(ok) && !knownKeys[ok]) otherKeys.push(ok);
	}
	if (otherKeys.length > 0) {
		content.appendChild(buildProtoSection('Other', otherKeys,
			function (k) { return k; },
			function (k) { var v = p[k]; return String(typeof v === 'object' ? JSON.stringify(v) : v); }));
	}

	/* If no detail sections at all, show basic info */
	if (content.childNodes.length === 0) {
		var basicKeys = ['name', 'num', 'mod', 'en'];
		var bk = [];
		for (var bi = 0; bi < basicKeys.length; bi++) {
			if (p[basicKeys[bi]] !== undefined) bk.push(basicKeys[bi]);
		}
		if (bk.length > 0) {
			var sec = mkEl('div', 'proto-detail-section');
			sec.appendChild(buildDetailPairs(bk,
				function (k) { return k; }, function (k) { return String(p[k]); }));
			content.appendChild(sec);
		}
	}

	detailTd.appendChild(content);
	detailTr.appendChild(detailTd);

	/* Insert detail row right after the clicked row */
	var nextSib = tr.nextSibling;
	if (nextSib) {
		tr.parentNode.insertBefore(detailTr, nextSib);
	} else {
		tr.parentNode.appendChild(detailTr);
	}
	tr._protoDetailRow = detailTr;
}

elProtoSearch.addEventListener('input', function () {
	renderProtocols(protoList);
});

/* SETTINGS: gain mode state */
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

/* SETTINGS: apply buttons, presets, enter-key */

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

/* Report meta checkboxes — display-only (server always sends full data) */
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

/* Debug enable checkbox in Settings → Advanced */
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

/* STATS */

function refreshStats() {
	rpc('get_stats', null, function (r) {
		if (!r) { elStatsContent.textContent = 'No data'; return; }
		var data = (typeof r === 'string') ? tryParse(r) : r;
		if (!data) { elStatsContent.textContent = String(r); return; }
		renderStats(data);
	});
}

/* Build stats with DocumentFragment, not innerHTML.
   Handles: primitives (grouped), objects (key-value table),
   arrays of objects (columnar table with header row). */
function renderStats(obj) {
	var frag = document.createDocumentFragment();
	renderStatsLevel(frag, obj, '');
	renderClientStats(frag);
	elStatsContent.innerHTML = '';
	elStatsContent.appendChild(frag);
}

function renderClientStats(frag) {
	var tbl = makeStatsCaption('Client Performance');

	/* Memory (Chrome/Edge only) */
	if (performance.memory) {
		var mb = (performance.memory.usedJSHeapSize / 1048576).toFixed(1);
		var total = (performance.memory.totalJSHeapSize / 1048576).toFixed(1);
		var limit = (performance.memory.jsHeapSizeLimit / 1048576).toFixed(0);
		appendStatRow(tbl, 'JS heap used', mb + ' MB');
		appendStatRow(tbl, 'JS heap total', total + ' MB');
		appendStatRow(tbl, 'JS heap limit', limit + ' MB');
	}

	/* Counts */
	appendStatRow(tbl, 'Events received', perfMetrics.eventsReceived);
	appendStatRow(tbl, 'Flush cycles', perfMetrics.flushCount);
	appendStatRow(tbl, 'Monitor groups', Object.keys(monGroups).length);
	appendStatRow(tbl, 'Devices tracked', deviceCount);
	appendStatRow(tbl, 'Chart events', chartEvents.length + ' / ' + CHART_MAX_EVENTS);
	appendStatRow(tbl, 'Syslog entries', syslogEntries.length + ' / ' + SYSLOG_MAX);

	/* Estimate stored device events */
	var devEvtTotal = 0;
	for (var i = 0; i < deviceKeys.length; i++) {
		var d = deviceRegistry[deviceKeys[i]];
		if (d && d.events) devEvtTotal += d.events.length;
	}
	appendStatRow(tbl, 'Device events stored', devEvtTotal);

	/* Timing */
	appendStatRow(tbl, 'Last flush', perfMetrics.lastFlushMs.toFixed(1) + ' ms'
		+ ' (' + perfMetrics.lastFlushBatch + ' events)');
	appendStatRow(tbl, 'Peak flush', perfMetrics.peakFlushMs.toFixed(1) + ' ms'
		+ ' (' + perfMetrics.peakBatchSize + ' events)');
	appendStatRow(tbl, 'Last re-render (Monitor)', perfMetrics.lastReRenderMonMs.toFixed(1) + ' ms');
	appendStatRow(tbl, 'Last re-render (Devices)', perfMetrics.lastReRenderDevMs.toFixed(1) + ' ms');
	appendStatRow(tbl, 'Last chart render', perfMetrics.lastChartMs.toFixed(1) + ' ms');

	/* Monitor row limits per group */
	appendStatRow(tbl, 'MAX_EVENTS per group', MAX_EVENTS);
	appendStatRow(tbl, 'MAX_DEVICE_EVENTS', MAX_DEVICE_EVENTS);

	frag.appendChild(tbl);
}

function renderStatsLevel(frag, obj, sectionLabel) {
	var key, val;
	/* Separate primitives from sub-objects/arrays */
	var primKeys = [];
	var subKeys = [];
	for (key in obj) {
		if (!obj.hasOwnProperty(key)) continue;
		val = obj[key];
		if (val !== null && val !== undefined && typeof val === 'object') {
			subKeys.push(key);
		} else {
			primKeys.push(key);
		}
	}

	/* Render primitives as a key-value table */
	if (primKeys.length > 0) {
		var caption = sectionLabel || 'Overview';
		var tbl = makeStatsCaption(caption);
		for (var pi = 0; pi < primKeys.length; pi++) {
			appendStatRow(tbl, primKeys[pi], obj[primKeys[pi]]);
		}
		frag.appendChild(tbl);
	}

	/* Render sub-objects and arrays */
	for (var si = 0; si < subKeys.length; si++) {
		key = subKeys[si];
		val = obj[key];
		if (Array.isArray(val)) {
			if (val.length > 0 && typeof val[0] === 'object') {
				/* Array of objects → columnar table */
				renderStatsArrayTable(frag, key, val);
			} else {
				/* Simple array */
				var tbl2 = makeStatsCaption(key);
				for (var ai = 0; ai < val.length; ai++) {
					appendStatRow(tbl2, ai, val[ai]);
				}
				frag.appendChild(tbl2);
			}
		} else {
			/* Nested object → recurse */
			renderStatsLevel(frag, val, key);
		}
	}
}

/* Render array of objects as a columnar table with header row */
function renderStatsArrayTable(frag, label, arr) {
	/* Sort by first numeric field (e.g. device/channel number) */
	var sortKey = null;
	if (arr.length > 0) {
		for (var sk in arr[0]) {
			if (arr[0].hasOwnProperty(sk) && typeof arr[0][sk] === 'number') {
				sortKey = sk;
				break;
			}
		}
	}
	if (sortKey) {
		arr = arr.slice(0); /* copy to avoid mutating original */
		arr.sort(function (a, b) { return (a[sortKey] || 0) - (b[sortKey] || 0); });
	}

	/* Collect all keys across all objects (preserve order) */
	var cols = [];
	var colSet = {};
	var i, k;
	for (i = 0; i < arr.length; i++) {
		for (k in arr[i]) {
			if (!arr[i].hasOwnProperty(k)) continue;
			if (!colSet[k]) {
				colSet[k] = true;
				cols.push(k);
			}
		}
	}
	if (cols.length === 0) return;

	var tbl = document.createElement('table');
	tbl.className = 'stats-table stats-col-table';
	var cap = document.createElement('caption');
	cap.textContent = fmtStatLabel(label);
	tbl.appendChild(cap);

	/* Header row */
	var thead = document.createElement('thead');
	var htr = document.createElement('tr');
	for (i = 0; i < cols.length; i++) {
		var th = document.createElement('th');
		th.textContent = fmtStatLabel(cols[i]);
		htr.appendChild(th);
	}
	thead.appendChild(htr);
	tbl.appendChild(thead);

	/* Data rows */
	var tbody = document.createElement('tbody');
	for (i = 0; i < arr.length; i++) {
		var tr = document.createElement('tr');
		for (var ci = 0; ci < cols.length; ci++) {
			var td = document.createElement('td');
			var v = arr[i][cols[ci]];
			if (v !== undefined && v !== null) {
				td.textContent = fmtStatValue(cols[ci], v);
			}
			tr.appendChild(td);
		}
		tbody.appendChild(tr);
	}
	tbl.appendChild(tbody);
	frag.appendChild(tbl);
}

function makeStatsCaption(label) {
	var tbl = document.createElement('table');
	tbl.className = 'stats-table';
	var cap = document.createElement('caption');
	cap.textContent = fmtStatLabel(label);
	tbl.appendChild(cap);
	return tbl;
}

function appendStatRow(tbl, label, value) {
	var tr = document.createElement('tr');
	var td1 = document.createElement('td');
	td1.textContent = fmtStatLabel(label);
	var td2 = document.createElement('td');
	td2.textContent = fmtStatValue(label, value);
	tr.appendChild(td1);
	tr.appendChild(td2);
	tbl.appendChild(tr);
}

$('stats-refresh').addEventListener('click', refreshStats);

function startStatsTimer() {
	stopStatsTimer();
	if ($('stats-auto').checked) statsTimer = setInterval(refreshStats, STATS_INTERVAL_MS);
}
function stopStatsTimer() {
	if (statsTimer) { clearInterval(statsTimer); statsTimer = null; }
}
$('stats-auto').addEventListener('change', startStatsTimer);
startStatsTimer();

/* SYSLOG TAB — server log messages (lvl + msg) */

function appendSyslogEntry(msg) {
	var t = msg.time || nowTime();
	if (t.indexOf('.') === -1) t = t + '.' + pad3(new Date().getMilliseconds());
	var entry = {
		time: t,
		src: msg.src || '',
		lvl: msg.lvl || 6,
		msg: msg.msg || ''
	};
	syslogEntries.unshift(entry);
	if (syslogEntries.length > SYSLOG_MAX) {
		syslogEntries.length = SYSLOG_MAX;
	}
	renderSyslogRow(entry);
}

/* Append a single row at the top of the syslog table */
function renderSyslogRow(entry) {
	var tr = document.createElement('tr');
	var lvl = entry.lvl;
	var cls = LOG_CLASSES[lvl] || 'log-info';
	tr._syslogData = entry;

	/* Time */
	var td1 = document.createElement('td');
	td1.textContent = formatTime(entry.time);
	tr.appendChild(td1);

	/* Source */
	var td2 = document.createElement('td');
	td2.textContent = entry.src;
	tr.appendChild(td2);

	/* Level */
	var td3 = document.createElement('td');
	td3.className = cls;
	td3.textContent = LOG_LEVELS[lvl] || 'INFO';
	tr.appendChild(td3);

	/* Message */
	var td4 = document.createElement('td');
	td4.textContent = entry.msg;
	tr.appendChild(td4);

	/* Apply severity class to full row for fatal/critical */
	if (lvl <= 2) tr.className = cls;

	/* Apply filter */
	if (syslogFilterStr && !matchesSyslogFilter(entry)) {
		tr.className = (tr.className ? tr.className + ' ' : '') + 'row-hidden';
	}

	/* Insert at top */
	prependChild(elSyslogBody, tr);

	/* Trim excess rows */
	while (elSyslogBody.childNodes.length > SYSLOG_MAX) {
		elSyslogBody.removeChild(elSyslogBody.lastChild);
	}

	elSyslogCount.textContent = pluralize(syslogEntries.length, 'entry', 'entries');
}

function matchesSyslogFilter(entry) {
	if (!syslogFilterStr) return true;
	var hay = (entry.src + ' ' + entry.msg + ' ' + (LOG_LEVELS[entry.lvl] || '')).toLowerCase();
	return hay.indexOf(syslogFilterStr) !== -1;
}

function applySyslogFilter() {
	var rows = elSyslogBody.childNodes;
	for (var i = 0; i < rows.length; i++) {
		var tr = rows[i];
		if (!tr._syslogData) continue;
		var visible = matchesSyslogFilter(tr._syslogData);
		var cls = tr.className || '';
		cls = cls.replace(/\s*row-hidden/g, '');
		if (!visible) cls = (cls ? cls + ' ' : '') + 'row-hidden';
		tr.className = cls;
	}
}

/* Re-render syslog time cells (called when hi-res toggle changes) */
function reRenderSyslogRows() {
	var rows = elSyslogBody.childNodes;
	for (var i = 0; i < rows.length; i++) {
		var tr = rows[i];
		if (!tr._syslogData) continue;
		if (tr.childNodes[0]) tr.childNodes[0].textContent = formatTime(tr._syslogData.time);
	}
}

/* Syslog search filter */
elSyslogSearch.addEventListener('input', function () {
	syslogFilterStr = elSyslogSearch.value.toLowerCase().trim();
	applySyslogFilter();
});

/* Syslog clear */
$('syslog-clear').addEventListener('click', function () {
	syslogEntries = [];
	elSyslogBody.innerHTML = '';
	elSyslogCount.textContent = '0 entries';
});
