/* ---- Tab switching ---- */
function switchTab(name) {
	/* Overlay panels: toggle instead of switching */
	if (OVERLAY_NAMES.indexOf(name) !== -1) {
		toggleOverlay(name);
		return;
	}
	activeTabName = name;
	for (var i = 0; i < tabBtns.length; i++) {
		var t = tabBtns[i];
		var tabName = t.getAttribute('data-tab');
		/* Skip overlay tab buttons — they are managed by overlay logic */
		if (OVERLAY_NAMES.indexOf(tabName) !== -1) continue;
		var cls = 'tab';
		if (tabName === name) cls += ' active';
		t.className = cls;
	}
	for (var j = 0; j < panels.length; j++) {
		var p = panels[j];
		p.className = (p.id === 'tab-' + name) ? 'panel active' : 'panel';
	}
	if (name === 'stats') refreshStats();
	devicesTabActive = (name === 'devices');
	if (devicesTabActive) scheduleDevicesRender();
	if (name === 'monitor' || name === 'devices') chartDirty = true;
	if (name === 'monitor') renderAllCharts();
	/* Restore dock layout for the newly active tab */
	restoreDockLayout(name);
}

bindAll('.tab', 'click', function (e) {
	switchTab(e.currentTarget.getAttribute('data-tab'));
});

/* ---- Overlay panel logic (Settings, Debug as floating panels) ---- */
function initOverlays() {
	var defs = [
		{name: 'help', el: $('tab-help'), btn: elHelpTabBtn, yOff: 0},
		{name: 'settings', el: $('tab-settings'), btn: elSettingsTabBtn, yOff: 0},
		{name: 'debug', el: $('tab-debug'), btn: elDbgTabBtn, yOff: 40}
	];
	for (var i = 0; i < defs.length; i++) {
		var d = defs[i];
		if (!d.el) continue;
		overlayPanels[d.name] = {
			el: d.el,
			btn: d.btn,
			open: false,
			x: -1,  /* -1 = use CSS default (right:16px) */
			y: 80 + d.yOff,
			/* Dock fields */
			docked: null,       /* null | 'left' | 'right' | 'bottom' */
			dockedTab: null,    /* 'monitor' | 'devices' */
			dockSize: 400,
			dockMinSize: 200,
			dockMaxRatio: 0.6,
			splitterEl: null,
			wrapperEl: null
		};
	}
	/* Wrap Monitor and Devices panel children in .panel-main divs */
	wrapPanelContent($('tab-monitor'));
	wrapPanelContent($('tab-devices'));

	/* Close button click handlers */
	bindAll('.overlay-close', 'click', function (e) {
		var name = e.currentTarget.getAttribute('data-overlay');
		if (name) closeOverlay(name);
	});
	/* Titlebar drag handlers */
	bindAll('.overlay-titlebar', 'mousedown', function (e) {
		if (e.target.tagName === 'BUTTON') return;
		var panel = e.currentTarget.parentNode;
		var name = null;
		for (var n in overlayPanels) {
			if (overlayPanels[n].el === panel) { name = n; break; }
		}
		if (!name) return;
		overlayDragStart(e, name);
	});
	document.addEventListener('mousemove', overlayDragMove);
	document.addEventListener('mouseup', overlayDragEnd);

	/* Splitter drag handlers (document-level) */
	document.addEventListener('mousemove', splitterDragMove);
	document.addEventListener('mouseup', splitterDragEnd);

	/* Chart resize handle handlers */
	initChartResizeHandles();

	/* Window resize: auto-undock on mobile, clamp dock sizes */
	window.addEventListener('resize', handleWindowResize);
}

/* Wrap a panel's children in .panel-main inside .dock-row-wrap (preserves all DOM refs) */
function wrapPanelContent(panelEl) {
	if (!panelEl) return;
	var main = document.createElement('div');
	main.className = 'panel-main';
	while (panelEl.firstChild) {
		main.appendChild(panelEl.firstChild);
	}
	var rowWrap = document.createElement('div');
	rowWrap.className = 'dock-row-wrap';
	rowWrap.appendChild(main);
	panelEl.appendChild(rowWrap);
	panelEl._panelMain = main;
	panelEl._dockRowWrap = rowWrap;
}

function toggleOverlay(name) {
	var p = overlayPanels[name];
	if (!p) return;
	if (p.open) closeOverlay(name);
	else openOverlay(name);
}

function openOverlay(name) {
	var p = overlayPanels[name];
	if (!p || p.open) return;
	/* If docked, switch to the tab it's docked in */
	if (p.docked && p.dockedTab) {
		switchTab(p.dockedTab);
		p.open = true;
		if (p.btn) {
			removeClass(p.btn, 'overlay-active');
			addClass(p.btn, 'overlay-active');
		}
		return;
	}
	p.open = true;
	p.el.classList.add('overlay-open');
	/* Position: use stored x/y or CSS default */
	if (p.x >= 0) {
		p.el.style.left = p.x + 'px';
		p.el.style.top = p.y + 'px';
		p.el.style.right = 'auto';
	}
	/* Clamp to viewport */
	clampOverlay(p);
	/* Bring to front */
	for (var n in overlayPanels) {
		if (!overlayPanels[n].docked) overlayPanels[n].el.style.zIndex = 100;
	}
	p.el.style.zIndex = 101;
	/* Mark tab button active */
	if (p.btn) {
		removeClass(p.btn, 'overlay-active');
		addClass(p.btn, 'overlay-active');
	}
	if (name === 'debug') refreshDebugDashboard();
}

function closeOverlay(name) {
	var p = overlayPanels[name];
	if (!p || !p.open) return;
	/* If docked, undock first */
	if (p.docked) undockOverlay(name);
	p.open = false;
	p.el.classList.remove('overlay-open');
	p.el.style.zIndex = 100;
	/* Remove active from tab button */
	if (p.btn) {
		removeClass(p.btn, 'overlay-active');
	}
}

function closeAllOverlays() {
	for (var n in overlayPanels) {
		if (overlayPanels[n].open) closeOverlay(n);
	}
}

function anyOverlayOpen() {
	for (var n in overlayPanels) {
		if (overlayPanels[n].open) return true;
	}
	return false;
}

function clampOverlay(p) {
	var rect = p.el.getBoundingClientRect();
	var vw = window.innerWidth;
	var vh = window.innerHeight;
	if (p.x >= 0) {
		if (p.x + rect.width > vw) p.x = Math.max(0, vw - rect.width);
		if (p.y + rect.height > vh) p.y = Math.max(0, vh - rect.height);
		if (p.x < 0) p.x = 0;
		if (p.y < 0) p.y = 0;
		p.el.style.left = p.x + 'px';
		p.el.style.top = p.y + 'px';
	}
}

/* ---- Overlay drag ---- */
function overlayDragStart(e, name) {
	var p = overlayPanels[name];
	if (!p) return;
	var rect = p.el.getBoundingClientRect();
	/* If docked, record start position but don't undock yet */
	if (p.docked) {
		overlayDrag = {
			name: name,
			startX: e.clientX,
			startY: e.clientY,
			origX: rect.left,
			origY: rect.top,
			wasDocked: true,
			dragDist: 0,
			pendingDockZone: null
		};
		e.preventDefault();
		return;
	}
	/* Switch from CSS right-positioned to left-positioned on first drag */
	if (p.x < 0) {
		p.x = rect.left;
		p.y = rect.top;
		p.el.style.left = p.x + 'px';
		p.el.style.top = p.y + 'px';
		p.el.style.right = 'auto';
	}
	overlayDrag = {
		name: name,
		startX: e.clientX,
		startY: e.clientY,
		origX: p.x,
		origY: p.y,
		wasDocked: false,
		dragDist: 0,
		pendingDockZone: null
	};
	/* Bring to front */
	for (var n in overlayPanels) {
		if (!overlayPanels[n].docked) overlayPanels[n].el.style.zIndex = 100;
	}
	p.el.style.zIndex = 101;
	e.preventDefault();
}

function overlayDragMove(e) {
	if (!overlayDrag) return;
	var dx = e.clientX - overlayDrag.startX;
	var dy = e.clientY - overlayDrag.startY;
	var dist = Math.sqrt(dx * dx + dy * dy);
	overlayDrag.dragDist = dist;
	var p = overlayPanels[overlayDrag.name];
	if (!p) return;

	/* If currently docked and drag distance exceeds 20px, undock */
	if (overlayDrag.wasDocked && p.docked && dist > 20) {
		var rect = p.el.getBoundingClientRect();
		undockOverlay(overlayDrag.name);
		p.x = rect.left;
		p.y = rect.top;
		p.el.style.left = p.x + 'px';
		p.el.style.top = p.y + 'px';
		p.el.style.right = 'auto';
		p.el.classList.add('overlay-open');
		overlayDrag.wasDocked = false;
		overlayDrag.origX = p.x - dx;
		overlayDrag.origY = p.y - dy;
	}

	if (p.docked) return; /* still docked, don't move */

	p.x = Math.max(0, overlayDrag.origX + dx);
	p.y = Math.max(0, overlayDrag.origY + dy);
	p.el.style.left = p.x + 'px';
	p.el.style.top = p.y + 'px';

	/* Dock zone detection: check if mouse is near panel edge */
	detectDockZone(e.clientX, e.clientY);
}

function overlayDragEnd() {
	if (overlayDrag) {
		var name = overlayDrag.name;
		var p = overlayPanels[name];
		var zone = overlayDrag.pendingDockZone;
		/* Hide dock zone highlight */
		if (elDockZoneHl) elDockZoneHl.className = 'dock-zone-highlight';
		if (p && zone && !p.docked) {
			/* Dock the overlay */
			dockOverlay(name, zone, activeTabName);
		} else if (p && !p.docked) {
			clampOverlay(p);
		}
		overlayDrag = null;
	}
}

/* ---- Dock zone detection ---- */
function detectDockZone(mx, my) {
	if (!overlayDrag || !elDockZoneHl) return;
	var panelEl = $('tab-' + activeTabName);
	if (!panelEl || !panelEl.classList.contains('active')) {
		overlayDrag.pendingDockZone = null;
		elDockZoneHl.className = 'dock-zone-highlight';
		return;
	}
	var rect = panelEl.getBoundingClientRect();
	var zone = null;
	var hlRect = {top: 0, left: 0, width: 0, height: 0};
	var th = dockZoneThreshold;

	if (mx >= rect.left && mx <= rect.left + th && my >= rect.top && my <= rect.bottom) {
		zone = 'left';
		hlRect = {top: rect.top, left: rect.left, width: rect.width * 0.3, height: rect.height};
	} else if (mx >= rect.right - th && mx <= rect.right && my >= rect.top && my <= rect.bottom) {
		zone = 'right';
		hlRect = {top: rect.top, left: rect.right - rect.width * 0.3, width: rect.width * 0.3, height: rect.height};
	} else if (my >= rect.bottom - th && my <= rect.bottom && mx >= rect.left && mx <= rect.right) {
		zone = 'bottom';
		hlRect = {top: rect.bottom - rect.height * 0.3, left: rect.left, width: rect.width, height: rect.height * 0.3};
	}

	overlayDrag.pendingDockZone = zone;
	if (zone) {
		elDockZoneHl.style.top = hlRect.top + 'px';
		elDockZoneHl.style.left = hlRect.left + 'px';
		elDockZoneHl.style.width = hlRect.width + 'px';
		elDockZoneHl.style.height = hlRect.height + 'px';
		elDockZoneHl.className = 'dock-zone-highlight dock-zone-active';
	} else {
		elDockZoneHl.className = 'dock-zone-highlight';
	}
}

/* ---- Dock / Undock overlay ---- */
function dockOverlay(name, side, tabName) {
	var p = overlayPanels[name];
	if (!p) return;
	var panelEl = $('tab-' + tabName);
	if (!panelEl) return;
	var mainEl = panelEl._panelMain;
	var rowWrap = panelEl._dockRowWrap;
	if (!mainEl || !rowWrap) return;

	/* Strip floating styles */
	p.el.classList.remove('overlay-open');
	p.el.style.left = '';
	p.el.style.top = '';
	p.el.style.right = '';
	p.el.style.zIndex = '';

	/* Add dock classes */
	p.el.classList.add('overlay-docked', 'dock-' + side);
	p.open = true;
	p.docked = side;
	p.dockedTab = tabName;

	/* Mark tab button active */
	if (p.btn) {
		removeClass(p.btn, 'overlay-active');
		addClass(p.btn, 'overlay-active');
	}

	/* Create splitter */
	var splitter = document.createElement('div');
	var isVertical = (side === 'left' || side === 'right');
	splitter.className = 'dock-splitter ' + (isVertical ? 'splitter-v' : 'splitter-h');
	splitter.addEventListener('mousedown', function (e) {
		splitterDragStart(e, name);
	});
	p.splitterEl = splitter;

	/* Insert into correct container */
	if (side === 'left') {
		rowWrap.insertBefore(p.el, mainEl);
		rowWrap.insertBefore(splitter, mainEl);
		p.el.style.width = p.dockSize + 'px';
	} else if (side === 'right') {
		rowWrap.appendChild(splitter);
		rowWrap.appendChild(p.el);
		p.el.style.width = p.dockSize + 'px';
	} else if (side === 'bottom') {
		panelEl.appendChild(splitter);
		panelEl.appendChild(p.el);
		p.el.style.height = p.dockSize + 'px';
	}

	/* Trigger chart redraw */
	chartDirty = true;
	renderAllCharts();
}

function undockOverlay(name) {
	var p = overlayPanels[name];
	if (!p || !p.docked) return;

	/* Remove splitter */
	if (p.splitterEl && p.splitterEl.parentNode) {
		p.splitterEl.parentNode.removeChild(p.splitterEl);
	}
	p.splitterEl = null;

	/* Move overlay back to body */
	if (p.el.parentNode) {
		p.el.parentNode.removeChild(p.el);
	}
	document.body.appendChild(p.el);

	/* Strip dock classes and inline size */
	p.el.classList.remove('overlay-docked', 'dock-left', 'dock-right', 'dock-bottom');
	p.el.style.width = '';
	p.el.style.height = '';

	p.docked = null;
	p.dockedTab = null;

	/* Restore floating position */
	if (p.x >= 0) {
		p.el.style.left = p.x + 'px';
		p.el.style.top = p.y + 'px';
		p.el.style.right = 'auto';
	}

	/* Trigger chart redraw */
	chartDirty = true;
	renderAllCharts();
}

/* Restore dock layout when switching tabs */
function restoreDockLayout(tabName) {
	chartDirty = true;
	setTimeout(renderAllCharts, 10);
}

/* ---- Splitter resize ---- */
function splitterDragStart(e, name) {
	var p = overlayPanels[name];
	if (!p || !p.docked) return;
	var isVertical = (p.docked === 'left' || p.docked === 'right');
	splitterDrag = {
		name: name,
		startPos: isVertical ? e.clientX : e.clientY,
		startSize: p.dockSize,
		axis: isVertical ? 'x' : 'y',
		side: p.docked
	};
	if (p.splitterEl) p.splitterEl.classList.add('splitter-active');
	document.body.style.cursor = isVertical ? 'col-resize' : 'row-resize';
	document.body.style.userSelect = 'none';
	e.preventDefault();
}

function splitterDragMove(e) {
	if (!splitterDrag) return;
	var p = overlayPanels[splitterDrag.name];
	if (!p) return;
	var delta;
	if (splitterDrag.axis === 'x') {
		delta = e.clientX - splitterDrag.startPos;
	} else {
		delta = e.clientY - splitterDrag.startPos;
	}
	/* For right dock, drag left = grow; for left dock, drag right = grow */
	if (splitterDrag.side === 'right' || splitterDrag.side === 'bottom') {
		delta = -delta;
	}
	var newSize = splitterDrag.startSize + delta;
	/* Clamp */
	var panelEl = $('tab-' + p.dockedTab);
	if (panelEl) {
		var maxDim = splitterDrag.axis === 'x'
			? panelEl.clientWidth * p.dockMaxRatio
			: panelEl.clientHeight * p.dockMaxRatio;
		newSize = Math.max(p.dockMinSize, Math.min(maxDim, newSize));
	} else {
		newSize = Math.max(p.dockMinSize, newSize);
	}
	p.dockSize = newSize;
	if (splitterDrag.axis === 'x') {
		p.el.style.width = newSize + 'px';
	} else {
		p.el.style.height = newSize + 'px';
	}
}

function splitterDragEnd() {
	if (!splitterDrag) return;
	var p = overlayPanels[splitterDrag.name];
	if (p && p.splitterEl) p.splitterEl.classList.remove('splitter-active');
	document.body.style.cursor = '';
	document.body.style.userSelect = '';
	splitterDrag = null;
	/* Trigger chart redraw */
	chartDirty = true;
	renderAllCharts();
}

/* ---- Chart resize handles ---- */
function initChartResizeHandles() {
	bindAll('.chart-resize-handle', 'mousedown', function (e) {
		var handle = e.currentTarget;
		var chartId = handle.getAttribute('data-chart');
		var wrapEl = (chartId === 'mon') ? elMonChartWrap : elDevChartWrap;
		if (!wrapEl) return;
		chartFrozenNowMs = chartFreezeNow();
		chartResizeDrag = {
			target: handle,
			startY: e.clientY,
			startHeight: wrapEl.clientHeight,
			wrapEl: wrapEl
		};
		handle.classList.add('resize-active');
		document.body.style.cursor = 'row-resize';
		document.body.style.userSelect = 'none';
		e.preventDefault();
	});
	document.addEventListener('mousemove', function (e) {
		if (!chartResizeDrag) return;
		var dy = e.clientY - chartResizeDrag.startY;
		var newH = Math.max(60, Math.min(600, chartResizeDrag.startHeight + dy));
		chartResizeDrag.wrapEl.style.height = newH + 'px';
		chartResizeDrag.wrapEl.style.minHeight = newH + 'px';
		chartDirty = true;
		renderAllCharts();
	});
	document.addEventListener('mouseup', function () {
		if (!chartResizeDrag) return;
		chartResizeDrag.target.classList.remove('resize-active');
		document.body.style.cursor = '';
		document.body.style.userSelect = '';
		chartResizeDrag = null;
		if (chartFrozenNowMs) {
			chartPanOffsetMs = Math.max(0, chartPanOffsetMs + (Date.now() - chartFrozenNowMs));
			chartFrozenNowMs = 0;
		}
		chartDirty = true;
		renderAllCharts();
	});
}

/* ---- Window resize handler ---- */
function handleWindowResize() {
	/* Auto-undock on mobile */
	if (window.innerWidth <= 700) {
		for (var n in overlayPanels) {
			if (overlayPanels[n].docked) undockOverlay(n);
		}
	}
	/* Clamp dock sizes */
	for (var n2 in overlayPanels) {
		var p = overlayPanels[n2];
		if (!p.docked) continue;
		var panelEl = $('tab-' + p.dockedTab);
		if (!panelEl) continue;
		var isV = (p.docked === 'left' || p.docked === 'right');
		var maxDim = isV
			? panelEl.clientWidth * p.dockMaxRatio
			: panelEl.clientHeight * p.dockMaxRatio;
		if (p.dockSize > maxDim) {
			p.dockSize = Math.max(p.dockMinSize, maxDim);
			if (isV) p.el.style.width = p.dockSize + 'px';
			else p.el.style.height = p.dockSize + 'px';
		}
	}
	chartDirty = true;
	renderAllCharts();
}
