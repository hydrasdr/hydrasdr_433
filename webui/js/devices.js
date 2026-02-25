/* DEVICES TAB — device registry, rendering, sort, filter */

/* Update or create a device record from an incoming event */
function updateDeviceRegistry(msg) {
	var model = msg.model || '';
	var id = (msg.id !== undefined && msg.id !== null) ? String(msg.id) : '';
	var key = model + '::' + id;
	var now = nowTime();
	var nowTs = Date.now();

	var dev = deviceRegistry[key];
	if (!dev) {
		dev = {
			model: model,
			id: id,
			key: key,
			firstSeen: now,
			firstSeenTs: nowTs,
			lastSeen: now,
			lastSeenTs: nowTs,
			count: 0,
			lastEvent: msg,
			events: []
		};
		deviceRegistry[key] = dev;
		deviceKeys.push(key);
		deviceCount++;
	}

	dev.lastSeen = now;
	dev.lastSeenTs = nowTs;
	dev.count++;
	dev.lastEvent = msg;
	dev.events.push(msg);  /* O(1) amortized; newest at end */
	if (dev.events.length > MAX_DEVICE_EVENTS) {
		dev.events.splice(0, dev.events.length - MAX_DEVICE_EVENTS);
	}

	/* Mark as recently active for green flash.
	   Purge stale entries if map grows too large (Devices tab not active). */
	devActiveKeys[key] = nowTs;
	if (++devActiveCount > 500) {
		var cutoff = nowTs - DEV_FLASH_MS;
		for (var ak in devActiveKeys) {
			if (devActiveKeys[ak] < cutoff) {
				delete devActiveKeys[ak];
				devActiveCount--;
			}
		}
	}

	/* Record event in activity chart */
	recordChartEvent(nowTs, msg);

	if (devicesTabActive) scheduleDevicesRender();
}

/* Schedule a rAF-throttled render for the Devices tab */
function scheduleDevicesRender() {
	if (devRafId) return;
	devRafId = requestAnimationFrame(function () {
		devRafId = 0;
		renderDeviceList();
		renderAllCharts();
		refreshInlineDetail();
	});
}

/* Get sorted+filtered device keys */
function getSortedDeviceKeys() {
	var keys = [];
	var i, key, dev;

	/* Apply filter */
	for (i = 0; i < deviceKeys.length; i++) {
		key = deviceKeys[i];
		dev = deviceRegistry[key];
		if (!dev) continue;
		if (devFilterStr) {
			var haystack = (dev.model + ' ' + dev.id).toLowerCase();
			if (haystack.indexOf(devFilterStr) === -1) continue;
		}
		keys.push(key);
	}

	/* Sort */
	var col = devSortCol;
	var asc = devSortAsc;
	keys.sort(function (a, b) {
		var da = deviceRegistry[a];
		var db = deviceRegistry[b];
		var va, vb;
		if (col === 'seen') {
			va = da.lastSeenTs; vb = db.lastSeenTs;
		} else if (col === 'model') {
			va = da.model; vb = db.model;
		} else if (col === 'id') {
			var na = parseInt(da.id, 10);
			var nb = parseInt(db.id, 10);
			va = isNaN(na) ? da.id : na;
			vb = isNaN(nb) ? db.id : nb;
		} else if (col === 'count') {
			va = da.count; vb = db.count;
		} else {
			return 0;
		}
		return compareValues(va, vb, asc);
	});

	return keys;
}

/* Create a device row TR element for a group (no Model col, 3 fixed + data) */
function createDevGroupRow(dev, group) {
	var totalCols = group.fixedCols + group.dataKeys.length;
	var tr = document.createElement('tr');
	tr._devKey = dev.key;

	/* ID */
	var td1 = document.createElement('td');
	td1.className = 'col-dev-id';
	td1.textContent = dev.id;
	tr.appendChild(td1);

	/* Last Seen — with optional signal bar prefix */
	var td2 = document.createElement('td');
	td2.className = 'col-dev-seen';
	var sb = buildSigBar(dev.lastEvent);
	if (sb) td2.appendChild(sb);
	td2.appendChild(document.createTextNode(formatTime(dev.lastSeen)));
	tr.appendChild(td2);

	/* Event count */
	var td3 = document.createElement('td');
	td3.className = 'col-dev-count';
	td3.textContent = dev.count;
	tr.appendChild(td3);

	/* Data columns */
	for (var c = 0; c < group.dataKeys.length; c++) {
		tr.appendChild(document.createElement('td'));
	}
	fillDataCells(tr, dev.lastEvent, group.dataKeys, 3);

	return tr;
}

/* Update an existing device row's cells in-place */
function updateDevGroupRow(tr, dev, group) {
	var totalCols = group.fixedCols + group.dataKeys.length;
	ensureCells(tr, totalCols);
	var cells = tr.childNodes;
	/* Last Seen (cell 1) — with signal bar */
	if (cells[1]) {
		cells[1].textContent = '';
		var sb = buildSigBar(dev.lastEvent);
		if (sb) cells[1].appendChild(sb);
		cells[1].appendChild(document.createTextNode(formatTime(dev.lastSeen)));
	}
	/* Event count (cell 2) */
	if (cells[2]) cells[2].textContent = dev.count;
	/* Data columns (cell 3..N) */
	fillDataCells(tr, dev.lastEvent, group.dataKeys, 3);
}

/* Render the device list — per-model grouped tables */
function renderDeviceList() {
	var keys = getSortedDeviceKeys();

	/* Partition keys by model */
	var byModel = {};
	var modelOrder = [];
	for (var i = 0; i < keys.length; i++) {
		var dev = deviceRegistry[keys[i]];
		if (!dev) continue;
		var m = dev.model;
		if (!byModel[m]) {
			byModel[m] = [];
			modelOrder.push(m);
		}
		byModel[m].push(dev);
	}

	var keysStr = keys.join(',');
	var needFullRebuild = (keysStr !== devPrevKeys);
	var scrollTop = elDevListWrap.scrollTop;

	/* Remove groups for models no longer present */
	for (var gm in devGroups) {
		if (!devGroups.hasOwnProperty(gm)) continue;
		if (!byModel[gm]) {
			if (devGroups[gm].el.parentNode) {
				devGroups[gm].el.parentNode.removeChild(devGroups[gm].el);
			}
			delete devGroups[gm];
		}
	}

	/* Build/update each model group */
	var latestTs = {};
	for (var mi = 0; mi < modelOrder.length; mi++) {
		var model = modelOrder[mi];
		var devs = byModel[model];
		var group = getOrCreateDevGroup(model);

		/* Track latest seen timestamp for this model */
		var maxTs = 0;
		for (var ti = 0; ti < devs.length; ti++) {
			if (devs[ti].lastSeenTs > maxTs) maxTs = devs[ti].lastSeenTs;
		}
		latestTs[model] = maxTs;

		/* Update data columns */
		if (updateGroupDataKeys(group, devs, _lastEvent)) {
			rebuildGroupColumns(group);
			needFullRebuild = true; /* columns changed, force rebuild */
		}

		if (needFullRebuild) {
			var frag = document.createDocumentFragment();
			for (var j = 0; j < devs.length; j++) {
				frag.appendChild(createDevGroupRow(devs[j], group));
			}
			group.tbody.innerHTML = '';
			group.tbody.appendChild(frag);
		} else {
			/* In-place update */
			var rows = group.tbody.childNodes;
			for (var ri = 0; ri < rows.length; ri++) {
				var row = rows[ri];
				if (hasClass(row, 'dev-detail-row')) continue;
				if (!row._devKey) continue;
				var dv = deviceRegistry[row._devKey];
				if (dv) updateDevGroupRow(row, dv, group);
			}
		}

		/* Update group header count */
		group.hdrCount.textContent = '(' + pluralize(devs.length, 'device') + ')';

		/* Apply filter: hide group if not matching */
		if (devFilterStr) {
			var anyMatch = false;
			for (var fi = 0; fi < devs.length; fi++) {
				var hay = (devs[fi].model + ' ' + devs[fi].id).toLowerCase();
				if (hay.indexOf(devFilterStr) !== -1) { anyMatch = true; break; }
			}
			group.el.style.display = anyMatch ? '' : 'none';
		} else {
			group.el.style.display = '';
		}
	}

	/* Reorder groups: most recently seen model first (only when changed) */
	if (devGroupsDirty) {
		var sortedModels = modelOrder.slice(0);
		sortedModels.sort(function (a, b) { return (latestTs[b] || 0) - (latestTs[a] || 0); });
		var frag2 = document.createDocumentFragment();
		for (var si = 0; si < sortedModels.length; si++) {
			var sg = devGroups[sortedModels[si]];
			if (sg && sg.el) frag2.appendChild(sg.el);
		}
		elDevGroups.appendChild(frag2);
		devGroupsDirty = false;
	}

	devPrevKeys = keysStr;
	elDevListWrap.scrollTop = scrollTop;

	elDevCount.textContent = pluralize(deviceCount, 'device') +
		(devFilterStr ? ' (' + keys.length + ' shown)' : '');

	updateDevSortIndicators();

	/* Apply green flash to recently-active rows across all groups */
	var nowMs = Date.now();
	for (var gm2 in devGroups) {
		if (!devGroups.hasOwnProperty(gm2)) continue;
		var allRows = devGroups[gm2].tbody.childNodes;
		for (var ri2 = 0; ri2 < allRows.length; ri2++) {
			var rr = allRows[ri2];
			if (hasClass(rr, 'dev-detail-row')) continue;
			var rKey = rr._devKey;
			if (rKey && devActiveKeys[rKey]) {
				var age = nowMs - devActiveKeys[rKey];
				if (age < DEV_FLASH_MS) {
					if (rr.className.indexOf('dev-active') === -1 && rr.className.indexOf('dev-fade') === -1) {
						rr.className = 'dev-active';
						(function (r, k) {
							setTimeout(function () {
								if (r.parentNode) r.className = 'dev-fade';
							}, 50);
							setTimeout(function () {
								delete devActiveKeys[k];
								if (r.parentNode) r.className = '';
							}, DEV_FLASH_MS);
						})(rr, rKey);
					}
				} else {
					delete devActiveKeys[rKey];
				}
			}
		}
	}
}

/* Build a columnar event row: Time | val1 | val2 | ... */
function buildEvtColumnarRow(evt, dataKeys) {
	var tr = document.createElement('tr');
	tr.className = 'row-clickable';
	tr._eventData = evt;
	tr._detailRow = null;

	/* Time cell — with optional signal bar prefix */
	var tdTime = document.createElement('td');
	var esb = buildSigBar(evt);
	if (esb) tdTime.appendChild(esb);
	tdTime.appendChild(document.createTextNode(formatTime(evt.time || '')));
	tr.appendChild(tdTime);

	/* One cell per data key */
	for (var c = 0; c < dataKeys.length; c++) {
		var td = document.createElement('td');
		var k = dataKeys[c];
		var v = evt[k];
		if (v === undefined) {
			/* Event doesn't have this key — empty cell */
			tr.appendChild(td);
			continue;
		}
		if (typeof v === 'object') v = JSON.stringify(v);
		var fmt = smartFormatValue(k, v);
		if (fmt && fmt.dot) {
			var dot = document.createElement('span');
			dot.className = 'batt-dot ' + fmt.dot;
			td.appendChild(dot);
		} else {
			td.textContent = (fmt && fmt.text) ? fmt.text : String(v);
		}
		tr.appendChild(td);
	}
	return tr;
}

/* Toggle inline detail expansion for a device row (accordion style) */
function toggleDeviceDetail(key, parentTr) {
	/* If already expanded, collapse */
	if (devDetailKey === key && parentTr._devDetailRow) {
		if (parentTr._devDetailRow.parentNode) {
			parentTr._devDetailRow.parentNode.removeChild(parentTr._devDetailRow);
		}
		parentTr._devDetailRow = null;
		removeClass(parentTr, 'dev-row-expanded');
		devDetailKey = null;
		return;
	}

	/* Collapse any previously expanded row */
	collapseDeviceDetail();

	var dev = deviceRegistry[key];
	if (!dev) return;
	devDetailKey = key;
	parentTr.className = (parentTr.className ? parentTr.className + ' ' : '') + 'dev-row-expanded';

	/* Build inline detail sub-row */
	var detailTr = document.createElement('tr');
	detailTr.className = 'dev-detail-row';
	/* Compute colspan from group's data columns */
	var devGroup = devGroups[dev.model];
	var devGroupCols = devGroup ? devGroup.dataKeys.length : 0;
	var detailTd = document.createElement('td');
	detailTd.setAttribute('colspan', '' + (3 + devGroupCols));

	/* Summary header */
	var summary = document.createElement('div');
	summary.className = 'dev-inline-summary';
	var pairs = [
		['Model', dev.model],
		['ID', dev.id || '--'],
		['First seen', formatTime(dev.firstSeen)],
		['Last seen', formatTime(dev.lastSeen)],
		['Events', String(dev.count)]
	];
	for (var p = 0; p < pairs.length; p++) {
		var span = document.createElement('span');
		var lbl = document.createElement('span');
		lbl.className = 'dev-sum-label';
		lbl.textContent = pairs[p][0] + ': ';
		span.appendChild(lbl);
		var val = document.createElement('span');
		val.className = 'dev-sum-value';
		val.textContent = pairs[p][1];
		span.appendChild(val);
		summary.appendChild(span);
	}
	detailTd.appendChild(summary);

	/* Collect visible data keys across all events (column headers) */
	var dataKeys = getVisibleEventKeys(dev.events);
	var totalCols = 1 + dataKeys.length; /* Time + data columns */

	/* Event history table — columnar layout (keys as headers, values underneath) */
	var evtTable = document.createElement('table');
	evtTable.className = 'dev-evt-table';
	var thead = document.createElement('thead');
	var headTr = document.createElement('tr');
	var th1 = document.createElement('th');
	th1.textContent = 'Time';
	headTr.appendChild(th1);
	for (var hi = 0; hi < dataKeys.length; hi++) {
		var th = document.createElement('th');
		th.textContent = dataKeys[hi];
		headTr.appendChild(th);
	}
	thead.appendChild(headTr);
	evtTable.appendChild(thead);

	var tbody = document.createElement('tbody');
	/* Events are stored oldest-first (push order); display newest-first */
	var showCount = Math.min(dev.events.length, DEVICE_DETAIL_PAGE);
	var evtStart = dev.events.length - 1;
	for (var i = 0; i < showCount; i++) {
		tbody.appendChild(buildEvtColumnarRow(dev.events[evtStart - i], dataKeys));
	}

	/* "Show more" row for paginated event history */
	if (dev.events.length > showCount) {
		var moreTr = document.createElement('tr');
		moreTr.className = 'dev-evt-more';
		var moreTd = document.createElement('td');
		moreTd.setAttribute('colspan', '' + totalCols);
		moreTd.textContent = 'Show more (' + (dev.events.length - showCount) + ' remaining)\u2026';
		moreTr.appendChild(moreTd);
		tbody.appendChild(moreTr);
		moreTr._devEvtShown = showCount;
		moreTr._dataKeys = dataKeys;
		moreTr.addEventListener('click', function () {
			var shown = moreTr._devEvtShown;
			var keys = moreTr._dataKeys;
			var next = Math.min(dev.events.length, shown + DEVICE_DETAIL_PAGE);
			var base = dev.events.length - 1;
			for (var j = shown; j < next; j++) {
				tbody.insertBefore(buildEvtColumnarRow(dev.events[base - j], keys), moreTr);
			}
			moreTr._devEvtShown = next;
			if (next >= dev.events.length) {
				moreTr.parentNode.removeChild(moreTr);
			} else {
				moreTd.textContent = 'Show more (' + (dev.events.length - next) + ' remaining)\u2026';
			}
		});
	}

	evtTable.appendChild(tbody);

	/* Click handler for event rows within the inline detail */
	tbody.addEventListener('click', function (e) {
		var tr = e.target;
		while (tr && tr.tagName !== 'TR') {
			tr = tr.parentNode;
		}
		if (!tr || !tr._eventData) return;
		if (hasClass(tr, 'detail-row')) return;
		toggleMonitorDetail(tr, totalCols);
	});

	detailTd.appendChild(evtTable);
	detailTr.appendChild(detailTd);

	/* Insert after parent row */
	parentTr._devDetailRow = detailTr;
	if (parentTr.nextSibling) {
		parentTr.parentNode.insertBefore(detailTr, parentTr.nextSibling);
	} else {
		parentTr.parentNode.appendChild(detailTr);
	}
}

/* Collapse any currently expanded device detail */
function collapseDeviceDetail() {
	if (!devDetailKey) return;
	/* Search across all device groups */
	for (var gm in devGroups) {
		if (!devGroups.hasOwnProperty(gm)) continue;
		var rows = devGroups[gm].tbody.childNodes;
		for (var i = 0; i < rows.length; i++) {
			var row = rows[i];
			if (row._devKey === devDetailKey && row._devDetailRow) {
				if (row._devDetailRow.parentNode) {
					row._devDetailRow.parentNode.removeChild(row._devDetailRow);
				}
				row._devDetailRow = null;
				removeClass(row, 'dev-row-expanded');
				devDetailKey = null;
				return;
			}
		}
	}
	devDetailKey = null;
}

/* Refresh inline detail if a device is expanded (called on new events) */
function refreshInlineDetail() {
	if (!devDetailKey) return;
	for (var gm in devGroups) {
		if (!devGroups.hasOwnProperty(gm)) continue;
		var rows = devGroups[gm].tbody.childNodes;
		for (var i = 0; i < rows.length; i++) {
			var row = rows[i];
			if (row._devKey === devDetailKey) {
				/* Remove old detail row if it survived (in-place update path) */
				if (row._devDetailRow && row._devDetailRow.parentNode) {
					row._devDetailRow.parentNode.removeChild(row._devDetailRow);
				}
				row._devDetailRow = null;
				removeClass(row, 'dev-row-expanded');
				var savedKey = devDetailKey;
				devDetailKey = null;
				toggleDeviceDetail(savedKey, row);
				return;
			}
		}
	}
}

/* Sort indicator updates for device groups */
function updateDevSortIndicators() {
	for (var model in devGroups) {
		if (!devGroups.hasOwnProperty(model)) continue;
		updateSortIndicators(devGroups[model].theadTr, devSortCol, devSortAsc, 'data-dev-sort');
	}
}

/* Re-render all Device rows with current data columns.
   Called when report meta display toggles change. */
function reRenderDeviceRows() {
	var t0 = performance.now();
	for (var model in devGroups) {
		if (!devGroups.hasOwnProperty(model)) continue;
		var group = devGroups[model];
		if (!group.needsRebuild) continue;  /* skip unchanged groups */
		group.needsRebuild = false;
		/* Rebuild column set from scratch */
		group.dataKeys = [];
		group.dataKeysSet = {};
		for (var di = 0; di < deviceKeys.length; di++) {
			var dev = deviceRegistry[deviceKeys[di]];
			if (!dev || dev.model !== model) continue;
			var keys = getVisibleKeys(dev.lastEvent);
			for (var ki = 0; ki < keys.length; ki++) {
				var k = keys[ki];
				if (!group.dataKeysSet[k]) {
					group.dataKeysSet[k] = 1;
					group.dataKeys.push(k);
				}
			}
		}
		rebuildGroupColumns(group);
		var rows = group.tbody.childNodes;
		for (var j = 0; j < rows.length; j++) {
			var tr = rows[j];
			if (!tr._devKey) continue;
			if (hasClass(tr, 'dev-detail-row')) continue;
			var dv = deviceRegistry[tr._devKey];
			if (!dv) continue;
			var totalCols = group.fixedCols + group.dataKeys.length;
			ensureCells(tr, totalCols);
			/* Refresh Last Seen cell (cell 1) with signal bar */
			tr.childNodes[1].textContent = '';
			var sb = buildSigBar(dv.lastEvent);
			if (sb) tr.childNodes[1].appendChild(sb);
			tr.childNodes[1].appendChild(document.createTextNode(formatTime(dv.lastSeen)));
			/* Refresh data cells */
			fillDataCells(tr, dv.lastEvent, group.dataKeys, 3);
		}
	}
	if (devDetailKey) refreshInlineDetail();
	perfMetrics.lastReRenderDevMs = performance.now() - t0;
}

/* CSV Export (Devices) */
function exportDevicesCSV() {
	var keys = getSortedDeviceKeys();
	if (keys.length === 0) return;
	var header = ['Model', 'ID', 'First Seen', 'Last Seen', 'Events'];
	var lines = [header.join(',')];
	for (var i = 0; i < keys.length; i++) {
		var dev = deviceRegistry[keys[i]];
		if (!dev) continue;
		lines.push([
			csvEscape(dev.model),
			csvEscape(dev.id),
			csvEscape(formatTime(dev.firstSeen)),
			csvEscape(formatTime(dev.lastSeen)),
			csvEscape(dev.count)
		].join(','));
	}
	downloadCSV('hydrasdr_433_devices.csv', lines.join('\n'));
}
$('dev-export').addEventListener('click', exportDevicesCSV);

/* Device search filter */
elDevSearch.addEventListener('input', function () {
	devFilterStr = elDevSearch.value.toLowerCase().trim();
	renderDeviceList();
});
