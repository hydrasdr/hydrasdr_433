/* SYSLOG TAB — server log messages (lvl + msg) */

onDisplayChange(reRenderSyslogRows);

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
	var _t0 = performance.now();
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
	perfMetrics.lastSyslogMs = performance.now() - _t0;
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
