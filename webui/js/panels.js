/* PROTOCOLS — DocumentFragment, no innerHTML */

function renderProtocols(list) {
	var filter = elProtoSearch.value.toLowerCase();
	var frag = document.createDocumentFragment();
	var shown = 0;
	var ookCount = 0;
	var fskCount = 0;
	var totalCount = list.length;

	/* Sort copy if user sort is active */
	if (protoSortCol) {
		list = list.slice(0);
		var col = protoSortCol;
		var asc = protoSortAsc;
		list.sort(function (a, b) {
			var va, vb;
			if (col === 'num') { va = a.num || 0; vb = b.num || 0; }
			else if (col === 'name') { va = a.name || ''; vb = b.name || ''; }
			else if (col === 'mod') { va = a.mod || 0; vb = b.mod || 0; }
			else if (col === 'en') { va = a.en ? 1 : 0; vb = b.en ? 1 : 0; }
			else { return 0; }
			return compareValues(va, vb, asc);
		});
	}

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
	updateProtoSortIndicators();
}

function updateProtoModCounts(ook, fsk, total) {
	elProtoModCounts.innerHTML = '';
	elProtoModCounts.appendChild(mkEl('span', 'mod-cnt-ook', ook + ' OOK'));
	elProtoModCounts.appendChild(document.createTextNode(' / '));
	elProtoModCounts.appendChild(mkEl('span', 'mod-cnt-fsk', fsk + ' FSK'));
	elProtoModCounts.appendChild(document.createTextNode(' / '));
	elProtoModCounts.appendChild(document.createTextNode(total + ' total'));
}

/* ---- Protocol: Column sort indicators ---- */
function updateProtoSortIndicators() {
	updateSortIndicators(document.querySelector('#proto-table thead'), protoSortCol, protoSortAsc);
}

/* ---- Protocol: Column sort click handler ---- */
document.querySelector('#proto-table thead tr').addEventListener('click', function (e) {
	var th = e.target;
	while (th && th.tagName !== 'TH') th = th.parentNode;
	if (!th || !hasClass(th, 'sortable')) return;
	var s = cycleSortState(protoSortCol, protoSortAsc, th.getAttribute('data-sort'));
	protoSortCol = s.col;
	protoSortAsc = s.asc;
	renderProtocols(protoList);
});

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

	/* Sort: user sort takes priority, else default to first numeric field asc */
	arr = arr.slice(0); /* copy to avoid mutating original */
	var ss = statsSortState[label];
	if (ss && ss.col) {
		var userCol = ss.col;
		var userAsc = ss.asc;
		arr.sort(function (a, b) {
			return compareValues(a[userCol], b[userCol], userAsc);
		});
	} else {
		/* Default: sort by first numeric field ascending */
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
			arr.sort(function (a, b) { return (a[sortKey] || 0) - (b[sortKey] || 0); });
		}
	}

	var tbl = document.createElement('table');
	tbl.className = 'stats-table stats-col-table';
	var cap = document.createElement('caption');
	cap.textContent = fmtStatLabel(label);
	tbl.appendChild(cap);

	/* Header row with sortable columns */
	var thead = document.createElement('thead');
	var htr = document.createElement('tr');
	for (i = 0; i < cols.length; i++) {
		var th = document.createElement('th');
		th.className = 'sortable';
		th.setAttribute('data-sort', cols[i]);
		th.textContent = fmtStatLabel(cols[i]) + ' ';
		var ind = document.createElement('span');
		ind.className = 'sort-ind';
		if (ss && ss.col === cols[i]) {
			addClass(th, 'sort-active');
			ind.textContent = ss.asc ? SORT_ASC : SORT_DESC;
		}
		th.appendChild(ind);
		htr.appendChild(th);
	}
	thead.appendChild(htr);
	tbl.appendChild(thead);

	/* Click handler for sorting (closed over label) */
	(function (lbl) {
		htr.addEventListener('click', function (e) {
			var target = e.target;
			while (target && target.tagName !== 'TH') target = target.parentNode;
			if (!target || !hasClass(target, 'sortable')) return;
			var col = target.getAttribute('data-sort');
			var cur = statsSortState[lbl];
			if (cur && cur.col === col) {
				if (cur.asc) { statsSortState[lbl] = {col: col, asc: false}; }
				else { delete statsSortState[lbl]; }
			} else {
				statsSortState[lbl] = {col: col, asc: true};
			}
			refreshStats();
		});
	})(label);

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

/* Build a single syslog TR from an entry (pure function, returns TR) */
function buildSyslogTr(entry) {
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
	return tr;
}

/* Append a single row at the top of the syslog table */
function renderSyslogRow(entry) {
	/* If sort is active, rebuild entire table to maintain order */
	if (syslogSortCol) {
		rebuildSyslogTable();
		return;
	}

	var tr = buildSyslogTr(entry);

	/* Insert at top */
	prependChild(elSyslogBody, tr);

	/* Trim excess rows */
	while (elSyslogBody.childNodes.length > SYSLOG_MAX) {
		elSyslogBody.removeChild(elSyslogBody.lastChild);
	}

	elSyslogCount.textContent = pluralize(syslogEntries.length, 'entry', 'entries');
}

/* Rebuild entire syslog table (used when sort is active) */
function rebuildSyslogTable() {
	var sorted = syslogEntries.slice(0);
	if (syslogSortCol) {
		var col = syslogSortCol;
		var asc = syslogSortAsc;
		sorted.sort(function (a, b) {
			var va, vb;
			if (col === 'time') { va = a.time; vb = b.time; }
			else if (col === 'src') { va = a.src; vb = b.src; }
			else if (col === 'lvl') { va = a.lvl; vb = b.lvl; }
			else if (col === 'msg') { va = a.msg; vb = b.msg; }
			else { return 0; }
			return compareValues(va, vb, asc);
		});
	}
	var frag = document.createDocumentFragment();
	for (var i = 0; i < sorted.length; i++) {
		frag.appendChild(buildSyslogTr(sorted[i]));
	}
	elSyslogBody.innerHTML = '';
	elSyslogBody.appendChild(frag);
	elSyslogCount.textContent = pluralize(syslogEntries.length, 'entry', 'entries');
	updateSyslogSortIndicators();
}

/* Syslog sort indicators */
function updateSyslogSortIndicators() {
	updateSortIndicators(document.querySelector('#syslog-table thead'), syslogSortCol, syslogSortAsc);
}

/* Syslog sort click handler */
document.querySelector('#syslog-table thead tr').addEventListener('click', function (e) {
	var th = e.target;
	while (th && th.tagName !== 'TH') th = th.parentNode;
	if (!th || !hasClass(th, 'sortable')) return;
	var s = cycleSortState(syslogSortCol, syslogSortAsc, th.getAttribute('data-sort'));
	syslogSortCol = s.col;
	syslogSortAsc = s.asc;
	rebuildSyslogTable();
});

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
	if (syslogSortCol) { rebuildSyslogTable(); return; }
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
	if (syslogSortCol) { rebuildSyslogTable(); return; }
	applySyslogFilter();
});

/* Syslog CSV export */
$('syslog-export').addEventListener('click', function () {
	if (syslogEntries.length === 0) return;
	var lines = ['Time,Source,Level,Message'];
	for (var i = 0; i < syslogEntries.length; i++) {
		var e = syslogEntries[i];
		var lvl = LOG_LEVELS[e.lvl] || 'INFO';
		lines.push(csvEscape(e.time) + ',' + csvEscape(e.src) + ',' + csvEscape(lvl) + ',' + csvEscape(e.msg));
	}
	downloadCSV('hydrasdr_433_syslog.csv', lines.join('\n'));
});

/* Syslog clear */
$('syslog-clear').addEventListener('click', function () {
	syslogEntries = [];
	syslogSortCol = null;
	syslogSortAsc = true;
	elSyslogBody.innerHTML = '';
	elSyslogCount.textContent = '0 entries';
	updateSyslogSortIndicators();
});
