/* PROTOCOLS — DocumentFragment, no innerHTML */

function renderProtocols(list) {
	var _t0 = performance.now();
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

		/* Count by modulation: OOK = 3..15, FSK = 16+ */
		if (modIdx > 0 && modIdx < FSK_MOD_MIN) ookCount++;
		else if (modIdx >= FSK_MOD_MIN) fskCount++;

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
		var isFsk = modIdx >= FSK_MOD_MIN;
		var isOok = modIdx > 0 && modIdx < FSK_MOD_MIN;
		tag.className = 'mod-tag ' + (isOok ? 'mod-ook' : isFsk ? 'mod-fsk' : 'mod-other');
		tag.textContent = modName(modIdx);
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
	perfMetrics.lastProtocolsMs = performance.now() - _t0;
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

/* ---- Protocol: toggle enable/disable on dot click ---- */
function toggleProtocol(tr, dot) {
	var p = tr._protoData;
	if (!p || p.num === undefined) return;
	var wasEnabled = p.en;
	var action = wasEnabled ? 'disable' : 'enable';
	var num = p.num;
	var name = p.name || '#' + num;

	/* Optimistic UI update */
	p.en = !wasEnabled;
	toggleClass(dot, 'on', !wasEnabled);
	toggleClass(dot, 'off', wasEnabled);
	toggleClass(tr, 'proto-disabled', wasEnabled);

	rpc('protocol', {arg: action, val: num}, function (result, error) {
		if (error) {
			/* Revert on failure */
			p.en = wasEnabled;
			toggleClass(dot, 'on', wasEnabled);
			toggleClass(dot, 'off', !wasEnabled);
			toggleClass(tr, 'proto-disabled', !wasEnabled);
			showToast(name + ': ' + (error.message || 'Error'), 'err');
		} else {
			showToast(name + ' ' + action + 'd', 'ok');
			updateProtoEnabledCounts();
		}
	});
}

/* Update enabled/total counts in toolbar after toggle */
function updateProtoEnabledCounts() {
	var ookCount = 0, fskCount = 0;
	for (var i = 0; i < protoList.length; i++) {
		var mod = protoList[i].mod || 0;
		if (mod > 0 && mod < FSK_MOD_MIN) ookCount++;
		else if (mod >= FSK_MOD_MIN) fskCount++;
	}
	updateProtoModCounts(ookCount, fskCount, protoList.length);
}

/* ---- Protocol: Row click to expand detail (dot click toggles enable) ---- */
elProtoBody.addEventListener('click', function (e) {
	var target = e.target;
	var tr = target;
	while (tr && tr.tagName !== 'TR') tr = tr.parentNode;
	if (!tr) return;

	/* Check if the click was on an .en-dot element */
	if (hasClass(target, 'en-dot')) {
		e.stopPropagation();
		if (tr._protoData) toggleProtocol(tr, target);
		return;
	}

	if (!tr._protoData) return;
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

function buildProtoDetailContent(p) {
	var content = mkEl('div', 'proto-detail-content');

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

	return content;
}

function toggleProtoDetail(tr) {
	toggleDetailRow(tr, '_protoDetailRow', 'proto-expanded', 'proto-detail-row', 4, function () {
		return buildProtoDetailContent(tr._protoData);
	});
}

var protoSearchTimer = 0;
elProtoSearch.addEventListener('input', function () {
	if (protoSearchTimer) clearTimeout(protoSearchTimer);
	protoSearchTimer = setTimeout(function () {
		protoSearchTimer = 0;
		renderProtocols(protoList);
	}, PROTO_SEARCH_DEBOUNCE_MS);
});

/* ---- Protocol: Enable All / Disable All ---- */
function batchToggleProtocols(enable) {
	/* Collect protocols that need toggling */
	var queue = [];
	for (var i = 0; i < protoList.length; i++) {
		var p = protoList[i];
		if (p.num === undefined) continue;
		if (enable && !p.en) queue.push(p);
		else if (!enable && p.en) queue.push(p);
	}
	if (queue.length === 0) {
		showToast('All protocols already ' + (enable ? 'enabled' : 'disabled'), 'ok');
		return;
	}

	var action = enable ? 'enable' : 'disable';
	var total = queue.length;
	var done = 0;
	var errors = 0;

	showToast((enable ? 'Enabling ' : 'Disabling ') + total + ' protocols...', 'ok');

	/* Send RPCs with staggered dispatch to avoid flooding */
	function sendNext(idx) {
		if (idx >= queue.length) return;
		var p = queue[idx];
		rpc('protocol', {arg: action, val: p.num}, function (result, error) {
			done++;
			if (error) errors++;
			else p.en = enable;
			if (done === total) {
				/* Refresh full protocol list from server to sync state */
				rpc('get_protocols', null, function (r) {
					if (r) {
						var data = (typeof r === 'string') ? tryParse(r) : r;
						if (data && data.protocols) {
							protoList = data.protocols;
							renderProtocols(protoList);
						}
					}
				});
				if (errors > 0) {
					showToast(errors + ' protocol' + (errors > 1 ? 's' : '') + ' failed', 'warn');
				} else {
					showToast('All protocols ' + action + 'd', 'ok');
				}
			}
		});
		/* Stagger 10ms apart to avoid flooding */
		setTimeout(function () { sendNext(idx + 1); }, PROTO_BATCH_STAGGER_MS);
	}
	sendNext(0);
}

$('proto-enable-all').addEventListener('click', function () { batchToggleProtocols(true); });
$('proto-disable-all').addEventListener('click', function () { batchToggleProtocols(false); });
