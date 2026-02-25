/* MONITOR: event table — THE HOT PATH All optimizations concentrate here. */

/* Batch flush: called once per animation frame.
   Processes ALL queued events partitioned by model.
   When the browser tab is hidden, defers DOM work to save CPU. */
function flushEvents() {
	rafId = 0;
	var batch = pendingEvents;
	pendingEvents = [];
	if (batch.length === 0) return;

	/* Defer DOM rendering while tab is hidden — accumulate for later */
	if (document.hidden) {
		for (var hi = 0; hi < batch.length; hi++) hiddenBatch.push(batch[hi]);
		eventCount += batch.length;
		rowCount += batch.length;
		return;
	}

	var t0 = performance.now();

	/* Partition batch by model */
	var byModel = {};
	var modelsInBatch = [];
	for (var i = 0; i < batch.length; i++) {
		var model = batch[i].model || '';
		if (!byModel[model]) {
			byModel[model] = [];
			modelsInBatch.push(model);
		}
		byModel[model].push(batch[i]);
	}

	/* Process each model group */
	for (var mi = 0; mi < modelsInBatch.length; mi++) {
		var m = modelsInBatch[mi];
		var msgs = byModel[m];
		var group = getOrCreateMonGroup(m);

		/* Update data columns for this group */
		if (updateGroupDataKeys(group, msgs, _identity)) {
			rebuildGroupColumns(group);
			/* Backfill existing rows with new column cells */
			var totalCols = group.fixedCols + group.dataKeys.length;
			var existingRows = group.tbody.childNodes;
			for (var er = 0; er < existingRows.length; er++) {
				var etr = existingRows[er];
				if (!etr._eventData) continue;
				if (hasClass(etr, 'detail-row')) continue;
				ensureCells(etr, totalCols);
				fillDataCells(etr, etr._eventData, group.dataKeys, 2);
			}
		}

		/* Build rows in reverse so newest is on top */
		var frag = document.createDocumentFragment();
		for (var j = msgs.length - 1; j >= 0; j--) {
			frag.appendChild(buildGroupRow(msgs[j], group));
		}
		group.rowCount += msgs.length;

		/* Prepend to group's tbody */
		prependChild(group.tbody, frag);

		/* Trim excess rows in this group */
		while (group.rowCount > MAX_EVENTS) {
			var last = group.tbody.lastChild;
			if (!last) break;
			if (hasClass(last, 'detail-row')) {
				group.tbody.removeChild(last);
				continue;
			}
			if (last._detailRow && last._detailRow.parentNode) {
				group.tbody.removeChild(last._detailRow);
			}
			group.tbody.removeChild(last);
			freeRow(last);
			group.rowCount--;
		}

		/* Update group header count */
		group.hdrCount.textContent = '(' + group.rowCount + ' events)';
		group.needsRebuild = true;
	}

	eventCount += batch.length;
	rowCount += batch.length;

	/* Reorder groups: move groups with new events to top */
	for (var ri = modelsInBatch.length - 1; ri >= 0; ri--) {
		var rg = monGroups[modelsInBatch[ri]];
		if (rg && rg.el.parentNode) {
			prependChild(elMonGroups, rg.el);
		}
	}
	devGroupsDirty = true;

	/* Update global counter */
	elMonCount.textContent = eventCount + ' events';

	/* Re-sort if sorting is active */
	if (monSortCol) sortAllMonGroups();

	/* Auto-scroll to top */
	if (elAutoScrl.checked) elMonWrap.scrollTop = 0;

	var elapsed = performance.now() - t0;
	perfMetrics.eventsReceived += batch.length;
	perfMetrics.lastFlushMs = elapsed;
	perfMetrics.lastFlushBatch = batch.length;
	perfMetrics.flushCount++;
	if (elapsed > perfMetrics.peakFlushMs) perfMetrics.peakFlushMs = elapsed;
	if (batch.length > perfMetrics.peakBatchSize) perfMetrics.peakBatchSize = batch.length;
}

/* Build a single table row for a group. 2 fixed cols (Time, ID) + data. */
function buildGroupRow(msg, group) {
	var totalCols = group.fixedCols + group.dataKeys.length;
	var tr = allocRow();
	ensureCells(tr, totalCols);
	tr.className = 'row-clickable';
	tr._eventData = msg;
	tr._detailRow = null;
	var cells = tr.childNodes;

	/* td[0]: Time — with optional signal bar prefix */
	cells[0].textContent = '';
	var sigBar = buildSigBar(msg);
	if (sigBar) cells[0].appendChild(sigBar);
	cells[0].appendChild(document.createTextNode(formatTime(msg.time || nowTime())));

	/* td[1]: ID */
	cells[1].textContent = (msg.id !== undefined && msg.id !== null) ? msg.id : '';

	/* td[2..N]: One cell per data key */
	fillDataCells(tr, msg, group.dataKeys, 2);

	return tr;
}

/* ---- Monitor: Event Filter ---- */
elMonFilter.addEventListener('input', function () {
	monFilterStr = elMonFilter.value.toLowerCase().trim();
	toggleClass(elMonFilter, 'filter-active', !!monFilterStr);

	/* Apply filter to all existing rows in the table */
	applyFilterToExistingRows();
});

function applyFilterToExistingRows() {
	/* Filter shows/hides entire groups by model name match */
	for (var model in monGroups) {
		if (!monGroups.hasOwnProperty(model)) continue;
		var group = monGroups[model];
		var visible = matchesFilter(model);
		group.el.style.display = visible ? '' : 'none';
	}
}

/* ---- Monitor: Column Sorting ---- */
function updateMonSortIndicators() {
	for (var model in monGroups) {
		if (!monGroups.hasOwnProperty(model)) continue;
		updateSortIndicators(monGroups[model].theadTr, monSortCol, monSortAsc);
	}
}

function getSortValue(tr, col) {
	if (!tr._eventData) return '';
	var msg = tr._eventData;
	if (col === 'time') return msg.time || '';
	if (col === 'model') return (msg.model || '').toLowerCase();
	if (col === 'id') {
		var id = msg.id;
		if (id === undefined || id === null) return '';
		/* Try numeric comparison for IDs that are numbers */
		var n = parseInt(id, 10);
		if (!isNaN(n)) return n;
		return ('' + id).toLowerCase();
	}
	return '';
}

/* Sort rows within a single Monitor group's tbody */
function sortMonGroupTable(group) {
	if (!monSortCol) return;
	var col = monSortCol;
	var asc = monSortAsc;
	var tbody = group.tbody;

	var rows = [];
	var child = tbody.firstChild;
	while (child) {
		if (child.tagName === 'TR' && !(hasClass(child, 'detail-row'))) {
			rows.push(child);
		}
		child = child.nextSibling;
	}

	rows.sort(function (a, b) {
		return compareValues(getSortValue(a, col), getSortValue(b, col), asc);
	});

	var frag = document.createDocumentFragment();
	for (var i = 0; i < rows.length; i++) {
		frag.appendChild(rows[i]);
		if (rows[i]._detailRow && rows[i]._detailRow.parentNode) {
			frag.appendChild(rows[i]._detailRow);
		}
	}
	tbody.appendChild(frag);
}

/* Sort all Monitor groups */
function sortAllMonGroups() {
	for (var model in monGroups) {
		if (!monGroups.hasOwnProperty(model)) continue;
		sortMonGroupTable(monGroups[model]);
	}
}

/* ---- Monitor: Row Click to Expand ---- */
function toggleMonitorDetail(tr, colspan) {
	colspan = colspan || 10; /* fallback */
	/* If already expanded, collapse */
	if (tr._detailRow) {
		if (tr._detailRow.parentNode) {
			tr._detailRow.parentNode.removeChild(tr._detailRow);
		}
		tr._detailRow = null;
		removeClass(tr, 'row-expanded');
		return;
	}

	/* Expand: create detail sub-row */
	tr.className = (tr.className ? tr.className + ' ' : '') + 'row-expanded';
	var detailTr = document.createElement('tr');
	detailTr.className = 'detail-row';
	var detailTd = document.createElement('td');
	detailTd.setAttribute('colspan', '' + colspan);

	var content = document.createElement('div');
	content.className = 'detail-content';

	var msg = tr._eventData;
	for (var k in msg) {
		if (!msg.hasOwnProperty(k)) continue;
		var v = msg[k];
		if (typeof v === 'object') v = JSON.stringify(v);

		var pair = document.createElement('span');
		pair.className = 'detail-pair';

		var keySpan = document.createElement('span');
		keySpan.className = 'detail-key';
		keySpan.textContent = k + ': ';
		pair.appendChild(keySpan);

		/* Smart format the value in detail view too */
		var fmt = smartFormatValue(k, v);
		if (fmt && fmt.dot) {
			var dot = document.createElement('span');
			dot.className = 'batt-dot ' + fmt.dot;
			pair.appendChild(dot);
		} else {
			var valSpan = document.createElement('span');
			valSpan.className = 'detail-val';
			valSpan.textContent = (fmt && fmt.text) ? fmt.text : String(v);
			pair.appendChild(valSpan);
		}

		content.appendChild(pair);
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
	tr._detailRow = detailTr;
}

/* Re-render all Monitor rows with current data columns.
   Called when report meta display toggles change. */
function reRenderMonitorRows() {
	var t0 = performance.now();
	for (var model in monGroups) {
		if (!monGroups.hasOwnProperty(model)) continue;
		var group = monGroups[model];
		if (!group.needsRebuild) continue;  /* skip unchanged groups */
		group.needsRebuild = false;
		/* Rebuild column set from scratch */
		group.dataKeys = [];
		group.dataKeysSet = {};
		var rows = group.tbody.childNodes;
		for (var i = 0; i < rows.length; i++) {
			var tr = rows[i];
			if (!tr._eventData) continue;
			if (hasClass(tr, 'detail-row')) continue;
			var keys = getVisibleKeys(tr._eventData);
			for (var ki = 0; ki < keys.length; ki++) {
				var k = keys[ki];
				if (!group.dataKeysSet[k]) {
					group.dataKeysSet[k] = 1;
					group.dataKeys.push(k);
				}
			}
		}
		rebuildGroupColumns(group);
		var totalCols = group.fixedCols + group.dataKeys.length;
		for (var j = 0; j < rows.length; j++) {
			var tr2 = rows[j];
			if (!tr2._eventData) continue;
			if (hasClass(tr2, 'detail-row')) continue;
			var msg2 = tr2._eventData;
			ensureCells(tr2, totalCols);
			/* Refresh Time cell with signal bar */
			tr2.childNodes[0].textContent = '';
			var sigBar = buildSigBar(msg2);
			if (sigBar) tr2.childNodes[0].appendChild(sigBar);
			tr2.childNodes[0].appendChild(document.createTextNode(formatTime(msg2.time || '')));
			/* Refresh data cells */
			fillDataCells(tr2, msg2, group.dataKeys, 2);

			/* Re-render expanded detail row */
			if (tr2._detailRow && tr2._detailRow.parentNode) {
				tr2._detailRow.parentNode.removeChild(tr2._detailRow);
				tr2._detailRow = null;
				removeClass(tr2, 'row-expanded');
				toggleMonitorDetail(tr2, totalCols);
			}
		}
	}
	perfMetrics.lastReRenderMonMs = performance.now() - t0;
}

/* ---- Pause / Resume (global — freezes Monitor + chart) ---- */
function togglePause() {
	paused = !paused;
	elMonPause.textContent = paused ? 'Resume' : 'Pause';
	elMonPause.className = paused ? 'btn-sm active' : 'btn-sm';
	if (paused) {
		/* Freeze chart — stop periodic redraw */
		if (chartTimerId) { clearInterval(chartTimerId); chartTimerId = null; }
	} else {
		/* Resume — replay all buffered events, then restart chart */
		var buf = pausedEvents;
		pausedEvents = [];
		for (var pi = 0; pi < buf.length; pi++) {
			updateDeviceRegistry(buf[pi]);
			pendingEvents.push(buf[pi]);
		}
		if (pendingEvents.length && !rafId)
			rafId = requestAnimationFrame(flushEvents);
		startChartTimer();
		chartDirty = true;
		renderAllCharts();
	}
}
elMonPause.addEventListener('click', togglePause);

/* ---- CSV Export (Monitor) ---- */
function exportMonitorCSV() {
	var allKeys = {};
	var dataRows = [];
	var i, k, model;
	/* Collect all data keys across all groups */
	for (model in monGroups) {
		if (!monGroups.hasOwnProperty(model)) continue;
		var group = monGroups[model];
		var rows = group.tbody.childNodes;
		for (i = 0; i < rows.length; i++) {
			var tr = rows[i];
			if (!tr._eventData) continue;
			if (hasClass(tr, 'detail-row')) continue;
			var msg = tr._eventData;
			dataRows.push(msg);
			for (k in msg) {
				if (msg.hasOwnProperty(k) && k !== 'time' && k !== 'model' && k !== 'id') {
					allKeys[k] = 1;
				}
			}
		}
	}
	if (dataRows.length === 0) return;
	var dynKeys = [];
	for (k in allKeys) {
		if (allKeys.hasOwnProperty(k)) dynKeys.push(k);
	}
	/* Header */
	var header = ['Time', 'Model', 'ID'];
	for (i = 0; i < dynKeys.length; i++) header.push(dynKeys[i]);
	var lines = [header.join(',')];
	/* Data rows — include Model from the event */
	for (i = 0; i < dataRows.length; i++) {
		var m = dataRows[i];
		var cols = [csvEscape(formatTime(m.time || '')), csvEscape(m.model || ''), csvEscape(m.id !== undefined ? m.id : '')];
		for (var di = 0; di < dynKeys.length; di++) {
			var v = m[dynKeys[di]];
			if (typeof v === 'object') v = JSON.stringify(v);
			cols.push(csvEscape(v));
		}
		lines.push(cols.join(','));
	}
	downloadCSV('hydrasdr_433_monitor.csv', lines.join('\n'));
}
$('mon-export').addEventListener('click', exportMonitorCSV);

/* Clear button */
$('mon-clear').addEventListener('click', function () {
	/* Remove all monitor group elements */
	while (elMonGroups.firstChild) {
		elMonGroups.removeChild(elMonGroups.firstChild);
	}
	monGroups = {};
	monGroupOrder = [];
	eventCount = 0;
	rowCount = 0;
	for (var ri = 0; ri < RATE_BUCKET_COUNT; ri++) rateBuckets[ri] = 0;
	rateBucketIdx = 0;
	rateBucketTs = 0;
	elMonCount.textContent = '0 events';
	elMonRate.textContent = '0.0 evt/s';
	/* Also clear device registry, device groups, and chart */
	deviceRegistry = {};
	deviceKeys = [];
	deviceCount = 0;
	devDetailKey = null;
	chartEvents = [];
	chartWindowSec = CHART_WINDOW_SEC;
	chartPanOffsetMs = 0;
	chartDirty = true;
	devActiveKeys = {};
	devActiveCount = 0;
	devPrevKeys = null;
	while (elDevGroups.firstChild) {
		elDevGroups.removeChild(elDevGroups.firstChild);
	}
	devGroups = {};
	devGroupOrder = [];
	if (devicesTabActive) scheduleDevicesRender();
});
