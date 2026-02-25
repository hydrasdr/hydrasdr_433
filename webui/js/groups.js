/* ---- Check if a model name passes the current filter ---- */
function matchesFilter(modelName) {
	if (!monFilterStr) return true;
	return (modelName || '').toLowerCase().indexOf(monFilterStr) !== -1;
}

/* ---- Per-model group creation (shared base) ---- */
function createGroupBase(model, opts) {
	var el = mkEl('div', opts.groupClass);
	el.setAttribute('data-model', model);

	var hdr = mkEl('div', 'group-hdr');
	var toggle = mkEl('span', 'group-toggle', '\u25BC');
	hdr.appendChild(toggle);
	var hdrModel = mkEl('span', 'group-model', model);
	hdrModel.style.color = modelColor(model);
	hdr.appendChild(hdrModel);
	var hdrCount = mkEl('span', 'group-count', '(0 ' + opts.countLabel + ')');
	hdr.appendChild(hdrCount);
	el.appendChild(hdr);

	hdr.addEventListener('click', function () {
		toggleClass(el, 'group-collapsed');
		toggle.textContent = hasClass(el, 'group-collapsed') ? '\u25B6' : '\u25BC';
	});

	var table = document.createElement('table');
	table.className = 'group-table';
	var thead = document.createElement('thead');
	var theadTr = document.createElement('tr');
	for (var i = 0; i < opts.columns.length; i++) {
		var c = opts.columns[i];
		var th = document.createElement('th');
		th.className = c.cls;
		th.setAttribute(opts.sortAttr, c.sort);
		th.innerHTML = c.label + ' <span class="sort-ind"></span>';
		theadTr.appendChild(th);
	}
	thead.appendChild(theadTr);
	table.appendChild(thead);
	var tbody = document.createElement('tbody');
	table.appendChild(tbody);
	el.appendChild(table);

	theadTr.addEventListener('click', function (e) {
		var th = e.target;
		while (th && th.tagName !== 'TH') th = th.parentNode;
		if (!th || !th.getAttribute(opts.sortAttr)) return;
		e.stopPropagation();
		opts.onSort(th.getAttribute(opts.sortAttr));
	});

	tbody.addEventListener('click', function (e) {
		var tr = e.target;
		while (tr && tr.tagName !== 'TR') tr = tr.parentNode;
		if (!tr) return;
		opts.onRowClick(tr, group);
	});

	var group = {
		model: model,
		el: el,
		table: table,
		thead: thead,
		theadTr: theadTr,
		tbody: tbody,
		hdr: hdr,
		hdrModel: hdrModel,
		hdrCount: hdrCount,
		dataKeys: [],
		dataKeysSet: {},
		fixedCols: opts.columns.length,
		needsRebuild: false
	};
	return group;
}

/* ---- Monitor group ---- */
function createMonGroup(model) {
	var group = createGroupBase(model, {
		groupClass: 'mon-group',
		countLabel: 'events',
		sortAttr: 'data-sort',
		columns: [
			{ cls: 'col-time sortable', sort: 'time', label: 'Time' },
			{ cls: 'col-id sortable', sort: 'id', label: 'ID' }
		],
		onSort: function (col) {
			var s = cycleSortState(monSortCol, monSortAsc, col);
			monSortCol = s.col;
			monSortAsc = s.asc;
			updateMonSortIndicators();
			if (monSortCol) sortAllMonGroups();
		},
		onRowClick: function (tr, grp) {
			if (!tr._eventData) return;
			if (hasClass(tr, 'detail-row')) return;
			toggleMonitorDetail(tr, grp.fixedCols + grp.dataKeys.length);
		}
	});
	group.rowCount = 0;
	return group;
}

/* Get or create a Monitor group for a model */
function getOrCreateMonGroup(model) {
	if (monGroups[model]) return monGroups[model];
	var group = createMonGroup(model);
	monGroups[model] = group;
	monGroupOrder.push(model);
	/* Apply filter: hide group if it doesn't match */
	if (monFilterStr && !matchesFilter(model)) {
		group.el.style.display = 'none';
	}
	elMonGroups.appendChild(group.el);
	return group;
}

/* ---- Device group ---- */
function createDevGroup(model) {
	return createGroupBase(model, {
		groupClass: 'dev-group',
		countLabel: 'devices',
		sortAttr: 'data-dev-sort',
		columns: [
			{ cls: 'col-dev-id sortable', sort: 'id', label: 'ID' },
			{ cls: 'col-dev-seen sortable', sort: 'seen', label: 'Last Seen' },
			{ cls: 'col-dev-count sortable', sort: 'count', label: 'Events' }
		],
		onSort: function (col) {
			var s = cycleSortState(devSortCol, devSortAsc, col, 'seen', false);
			devSortCol = s.col;
			devSortAsc = s.asc;
			renderDeviceList();
		},
		onRowClick: function (tr) {
			if (hasClass(tr, 'dev-detail-row')) return;
			if (!tr._devKey) return;
			toggleDeviceDetail(tr._devKey, tr);
		}
	});
}

/* Get or create a Device group for a model */
function getOrCreateDevGroup(model) {
	if (devGroups[model]) return devGroups[model];
	var group = createDevGroup(model);
	devGroups[model] = group;
	devGroupOrder.push(model);
	elDevGroups.appendChild(group.el);
	return group;
}

/* ---- Column management ---- */

/* Update a group's data columns from a list of items.
   getMsgFn extracts the message from each item.
   Returns true if new columns were added. */
function updateGroupDataKeys(group, items, getMsgFn) {
	var changed = false;
	for (var i = 0; i < items.length; i++) {
		var keys = getVisibleKeys(getMsgFn(items[i]));
		for (var j = 0; j < keys.length; j++) {
			var k = keys[j];
			if (!group.dataKeysSet[k]) {
				group.dataKeysSet[k] = 1;
				group.dataKeys.push(k);
				changed = true;
			}
		}
	}
	return changed;
}

/* Rebuild a group's <thead> with current data columns */
function rebuildGroupColumns(group) {
	var theadTr = group.theadTr;
	while (theadTr.children.length > group.fixedCols) {
		theadTr.removeChild(theadTr.lastChild);
	}
	while (theadTr.lastChild && theadTr.lastChild.nodeType !== 1) {
		theadTr.removeChild(theadTr.lastChild);
	}
	for (var i = 0; i < group.dataKeys.length; i++) {
		var th = document.createElement('th');
		th.className = 'col-dyn';
		th.textContent = group.dataKeys[i];
		theadTr.appendChild(th);
	}
}
