/* KEYBOARD SHORTCUTS */

document.addEventListener('keydown', function (e) {
	var tag = e.target.tagName;
	var inInput = (tag === 'INPUT' || tag === 'SELECT' || tag === 'TEXTAREA');

	/* Esc: close overlays, clear filter, unfocus — works even when in input */
	if (e.keyCode === 27) { /* Escape */
		/* Close any open overlay first */
		if (anyOverlayOpen()) {
			closeAllOverlays();
			e.preventDefault();
			return;
		}
		if (document.activeElement === elMonFilter) {
			elMonFilter.value = '';
			monFilterStr = '';
			removeClass(elMonFilter, 'filter-active');
			applyFilterToExistingRows();
			elMonFilter.blur();
			e.preventDefault();
			return;
		}
		if (document.activeElement === elDevSearch) {
			elDevSearch.value = '';
			devFilterStr = '';
			renderDeviceList();
			elDevSearch.blur();
			e.preventDefault();
			return;
		}
		/* If a device is expanded inline, collapse it */
		if (devicesTabActive && devDetailKey) {
			collapseDeviceDetail();
			e.preventDefault();
			return;
		}
		/* If in any other input, just blur */
		if (inInput) {
			e.target.blur();
			e.preventDefault();
			return;
		}
	}

	/* Don't process other shortcuts while typing in inputs */
	if (inInput) return;

	/* Tab switching: 1-2 real tabs, 3-7 overlay toggles, 8=settings, 9=debug */
	var names = ['monitor', 'devices', 'syslog', 'protocols', 'stats', 'system', 'help'];
	var idx = e.keyCode - 49;
	if (idx >= 0 && idx < names.length) {
		switchTab(names[idx]);
		return;
	}
	if (e.keyCode === 56) { /* 8 = settings overlay */
		toggleOverlay('settings');
		return;
	}
	if (e.keyCode === 57 && debugEnabled) { /* 9 = debug overlay */
		toggleOverlay('debug');
		return;
	}

	/* Space: pause/resume (global) */
	if (e.keyCode === 32) { /* Space */
		e.preventDefault();
		togglePause();
		return;
	}

	/* F: focus monitor filter */
	if (e.keyCode === 70) { /* F */
		e.preventDefault();
		/* Switch to monitor tab if not already there */
		switchTab('monitor');
		elMonFilter.focus();
		return;
	}

	/* D: focus device search */
	if (e.keyCode === 68) { /* D */
		e.preventDefault();
		switchTab('devices');
		elDevSearch.focus();
		return;
	}
});

/* ---- Visibility change: flush deferred events when tab becomes visible ---- */
document.addEventListener('visibilitychange', function () {
	if (!document.hidden && hiddenBatch.length > 0) {
		var buf = hiddenBatch;
		hiddenBatch = [];
		for (var i = 0; i < buf.length; i++) pendingEvents.push(buf[i]);
		if (pendingEvents.length && !rafId)
			rafId = requestAnimationFrame(flushEvents);
		chartDirty = true;
	}
});

/* ---- Start ---- */
initOverlays();
startRateTimer();
startChartTimer();
connect();
