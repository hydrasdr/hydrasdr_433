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
	/* Snapshot DOM node count */
	perfMetrics.domNodeCount = document.getElementsByTagName('*').length;

	/* ---- Event Pipeline ---- */
	var tblEvt = makeStatsCaption('Event Pipeline');
	appendStatRow(tblEvt, 'Events received', perfMetrics.eventsReceived);
	appendStatRow(tblEvt, 'Peak event rate', perfMetrics.peakEventRate.toFixed(1) + ' evt/s');
	appendStatRow(tblEvt, 'Flush cycles', perfMetrics.flushCount);
	appendStatRow(tblEvt, 'Last flush', perfMetrics.lastFlushMs.toFixed(1) + ' ms'
		+ ' (' + perfMetrics.lastFlushBatch + ' events)');
	appendStatRow(tblEvt, 'Peak flush', perfMetrics.peakFlushMs.toFixed(1) + ' ms'
		+ ' (' + perfMetrics.peakBatchSize + ' events)');
	appendStatRow(tblEvt, 'fillDataCells (last flush)', perfMetrics.fillDataCellsMs.toFixed(1) + ' ms');
	appendStatRow(tblEvt, 'Hidden batch (peak)', perfMetrics.hiddenBatchMax);
	appendStatRow(tblEvt, 'Hidden flushes', perfMetrics.hiddenFlushes);
	frag.appendChild(tblEvt);

	/* ---- Render Timing ---- */
	var tblRnd = makeStatsCaption('Render Timing');
	appendStatRow(tblRnd, 'Re-render Monitor', perfMetrics.lastReRenderMonMs.toFixed(1) + ' ms');
	appendStatRow(tblRnd, 'Re-render Devices', perfMetrics.lastReRenderDevMs.toFixed(1) + ' ms');
	appendStatRow(tblRnd, 'Chart render', perfMetrics.lastChartMs.toFixed(1) + ' ms');
	appendStatRow(tblRnd, 'Protocols render', perfMetrics.lastProtocolsMs.toFixed(1) + ' ms');
	appendStatRow(tblRnd, 'Syslog rebuild', perfMetrics.lastSyslogMs.toFixed(1) + ' ms');
	appendStatRow(tblRnd, 'Device registry update', perfMetrics.lastDevRegistryMs.toFixed(3) + ' ms');
	frag.appendChild(tblRnd);

	/* ---- WebSocket / RPC ---- */
	var tblWs = makeStatsCaption('WebSocket / Rpc');
	appendStatRow(tblWs, 'JSON.parse (last)', perfMetrics.lastMsgParseMs.toFixed(3) + ' ms');
	appendStatRow(tblWs, 'JSON.parse (peak)', perfMetrics.peakMsgParseMs.toFixed(3) + ' ms');
	appendStatRow(tblWs, 'RPC queue depth', perfMetrics.rpcQueueDepth);
	appendStatRow(tblWs, 'RPC queue (peak)', perfMetrics.peakRpcQueueDepth);
	appendStatRow(tblWs, 'WS reconnects', perfMetrics.wsReconnects);
	frag.appendChild(tblWs);

	/* ---- Counts & Capacity ---- */
	var tblCnt = makeStatsCaption('Counts & Capacity');
	appendStatRow(tblCnt, 'Monitor groups', Object.keys(monGroups).length);
	appendStatRow(tblCnt, 'Devices tracked', deviceCount);
	appendStatRow(tblCnt, 'Chart events', chartEvents.length + ' / ' + CHART_MAX_EVENTS);
	appendStatRow(tblCnt, 'Syslog entries', syslogEntries.length + ' / ' + SYSLOG_MAX);
	var devEvtTotal = 0;
	for (var i = 0; i < deviceKeys.length; i++) {
		var d = deviceRegistry[deviceKeys[i]];
		if (d && d.events) devEvtTotal += d.events.length;
	}
	appendStatRow(tblCnt, 'Device events stored', devEvtTotal);
	appendStatRow(tblCnt, 'MAX_EVENTS per group', MAX_EVENTS);
	appendStatRow(tblCnt, 'MAX_DEVICE_EVENTS', MAX_DEVICE_EVENTS);
	frag.appendChild(tblCnt);

	/* ---- Row Pool & DOM ---- */
	var tblDom = makeStatsCaption('Row Pool & Dom');
	appendStatRow(tblDom, 'Pool size', rowPool.length + ' / ' + POOL_MAX);
	appendStatRow(tblDom, 'Pool hits', perfMetrics.rowPoolHits);
	appendStatRow(tblDom, 'Pool misses', perfMetrics.rowPoolMisses);
	var poolTotal = perfMetrics.rowPoolHits + perfMetrics.rowPoolMisses;
	var hitRate = poolTotal > 0 ? ((perfMetrics.rowPoolHits / poolTotal) * 100).toFixed(1) : '0.0';
	appendStatRow(tblDom, 'Pool hit rate', hitRate + '%');
	appendStatRow(tblDom, 'DOM nodes', perfMetrics.domNodeCount);
	appendStatRow(tblDom, 'Toasts shown', perfMetrics.toastsShown);
	frag.appendChild(tblDom);

	/* ---- Memory (Chrome/Edge only) ---- */
	if (performance.memory) {
		var tblMem = makeStatsCaption('Memory');
		var mb = (performance.memory.usedJSHeapSize / 1048576).toFixed(1);
		var total = (performance.memory.totalJSHeapSize / 1048576).toFixed(1);
		var limit = (performance.memory.jsHeapSizeLimit / 1048576).toFixed(0);
		appendStatRow(tblMem, 'JS heap used', mb + ' MB');
		appendStatRow(tblMem, 'JS heap total', total + ' MB');
		appendStatRow(tblMem, 'JS heap limit', limit + ' MB');
		frag.appendChild(tblMem);
	}
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
