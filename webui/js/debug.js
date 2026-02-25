/* DEBUG: Test data injection helper Call from browser console: _injectTestEvent('Acurite-Tower') */
var testTemplates = {
	'Kia-TPMS': {model:'Kia-TPMS', id:1234, pressure_kPa:230.5,
		temperature_C:22.3, battery_ok:1, rssi:-8.5, snr:15.2, noise:-24.0,
		protocol:198, description:'Kia TPMS'},
	'Acurite-Tower': {model:'Acurite-Tower', id:5678, temperature_C:18.7,
		humidity:62, battery_ok:1, rssi:-12.1, snr:10.5, noise:-22.0,
		protocol:40, description:'Acurite tower sensor'},
	'LaCrosse-TX141W': {model:'LaCrosse-TX141W', id:91, temperature_C:21.0,
		humidity:55, rssi:-15.2, snr:8.0, noise:-23.0,
		protocol:141, description:'LaCrosse TX141W'},
	'Oregon-THN132N': {model:'Oregon-THN132N', id:42, temperature_C:19.5,
		battery_ok:0, rssi:-20.0, snr:5.0, noise:-25.0,
		protocol:19, description:'Oregon THN132N'},
	'Nexus-TH': {model:'Nexus-TH', id:107, temperature_C:23.1, humidity:48,
		channel:2, rssi:-11.0, snr:12.0, noise:-23.0,
		protocol:47, description:'Nexus Temperature/Humidity'}
};

window._injectTestEvent = function (model) {
	model = model || 'Acurite-Tower';
	var base = testTemplates[model]
		|| {model: model, id: Math.floor(Math.random()*9999),
			temperature_C: 20 + Math.random()*10, rssi: -10 - Math.random()*20};
	var msg = {};
	for (var k in base) msg[k] = base[k];
	msg.id = base.id + Math.floor(Math.random() * 10);
	msg.time = nowTime();
	if (msg.temperature_C !== undefined) msg.temperature_C = +(msg.temperature_C + (Math.random()-0.5)*4).toFixed(1);
	if (msg.humidity !== undefined) msg.humidity = Math.round(msg.humidity + (Math.random()-0.5)*10);
	if (msg.pressure_kPa !== undefined) msg.pressure_kPa = +(msg.pressure_kPa + (Math.random()-0.5)*20).toFixed(1);
	if (msg.rssi !== undefined) msg.rssi = +(msg.rssi + (Math.random()-0.5)*6).toFixed(1);
	recordEventRate(1);
	updateDeviceRegistry(msg);
	pendingEvents.push(msg);
	if (!rafId) rafId = requestAnimationFrame(flushEvents);
};

/* Bulk inject events for performance testing.
   Usage: _injectBulkEvents(10000)       — 10K events, 5 models
          _injectBulkEvents(10000, 20)    — 10K events, 20 models
          _injectBulkEvents(10000, 1, 'Somfy-IOHC') — 10K events, single model */
window._injectBulkEvents = function (count, numModels, forcedModel) {
	count = count || 1000;
	numModels = numModels || 5;
	var models = Object.keys(testTemplates);
	/* Extend with generated model names if needed */
	while (models.length < numModels) {
		models.push('TestDevice-' + models.length);
	}
	if (forcedModel) { models = [forcedModel]; numModels = 1; }

	var t0 = performance.now();
	for (var i = 0; i < count; i++) {
		var modelName = models[i % numModels];
		_injectTestEvent(modelName);
	}
	var elapsed = performance.now() - t0;
	var rate = (count / (elapsed / 1000)).toFixed(0);
	console.log('[perf] Injected ' + count + ' events across ' + numModels
		+ ' models in ' + elapsed.toFixed(1) + 'ms (' + rate + ' evt/s)');
	console.log('[perf] perfMetrics:', JSON.stringify(perfMetrics, null, 2));
	if (performance.memory) {
		console.log('[perf] JS heap: '
			+ (performance.memory.usedJSHeapSize / 1048576).toFixed(1) + ' MB used / '
			+ (performance.memory.totalJSHeapSize / 1048576).toFixed(1) + ' MB total');
	}
};

/* ---- Debug tab functions ---- */
function refreshDebugDashboard() {
	refreshDebugPerf();
	refreshDebugMem();
}

function refreshDebugPerf() {
	var el = $('dbg-perf-output');
	if (!el) return;
	var lines = [];
	lines.push('eventsReceived:    ' + perfMetrics.eventsReceived);
	lines.push('flushCount:        ' + perfMetrics.flushCount);
	lines.push('lastFlushMs:       ' + perfMetrics.lastFlushMs.toFixed(2) + ' ms');
	lines.push('lastFlushBatch:    ' + perfMetrics.lastFlushBatch);
	lines.push('peakFlushMs:       ' + perfMetrics.peakFlushMs.toFixed(2) + ' ms');
	lines.push('peakBatchSize:     ' + perfMetrics.peakBatchSize);
	lines.push('lastReRenderMonMs: ' + perfMetrics.lastReRenderMonMs.toFixed(2) + ' ms');
	lines.push('lastReRenderDevMs: ' + perfMetrics.lastReRenderDevMs.toFixed(2) + ' ms');
	lines.push('lastChartMs:       ' + perfMetrics.lastChartMs.toFixed(2) + ' ms');
	lines.push('---');
	lines.push('monGroups:         ' + monGroupOrder.length);
	lines.push('devGroups:         ' + devGroupOrder.length);
	lines.push('deviceCount:       ' + deviceCount);
	el.textContent = lines.join('\n');
}

function refreshDebugMem() {
	var el = $('dbg-mem-output');
	if (!el) return;
	var lines = [];
	if (performance.memory) {
		lines.push('JS heap used:  ' + (performance.memory.usedJSHeapSize / 1048576).toFixed(1) + ' MB');
		lines.push('JS heap total: ' + (performance.memory.totalJSHeapSize / 1048576).toFixed(1) + ' MB');
		lines.push('JS heap limit: ' + (performance.memory.jsHeapSizeLimit / 1048576).toFixed(1) + ' MB');
	} else {
		lines.push('JS heap: not available (Chrome only)');
	}
	lines.push('DOM nodes:     ' + document.querySelectorAll('*').length);
	lines.push('chartEvents:   ' + chartEvents.length + ' / ' + CHART_MAX_EVENTS);
	lines.push('deviceKeys:    ' + deviceKeys.length);
	lines.push('rowPool size:  ' + rowPool.length + ' / ' + POOL_MAX);
	el.textContent = lines.join('\n');
}

function dbgBenchmark(count) {
	var el = $('dbg-bench-output');
	if (!el) return;
	el.textContent = 'Running ' + count + ' event benchmark...';
	/* Use setTimeout to allow DOM to update before heavy work */
	setTimeout(function () {
		var t0 = performance.now();
		_injectBulkEvents(count);
		var elapsed = performance.now() - t0;
		var rate = (count / (elapsed / 1000)).toFixed(0);
		var lines = [];
		lines.push('Injected: ' + count + ' events');
		lines.push('Time:     ' + elapsed.toFixed(1) + ' ms');
		lines.push('Rate:     ' + rate + ' evt/s');
		lines.push('Peak flush: ' + perfMetrics.peakFlushMs.toFixed(2) + ' ms');
		lines.push('Peak batch: ' + perfMetrics.peakBatchSize);
		el.textContent = lines.join('\n');
		refreshDebugDashboard();
	}, 16);
}

function dbgToggleAutoRefresh(on) {
	if (on) {
		if (!dbgAutoTimer) {
			dbgAutoTimer = setInterval(refreshDebugDashboard, 1000);
		}
	} else {
		if (dbgAutoTimer) {
			clearInterval(dbgAutoTimer);
			dbgAutoTimer = null;
		}
	}
}

function dbgStartRateSim(rate, sec) {
	dbgStopRateSim();
	/* Time-compensated injection: track elapsed time and inject the exact
	   number of events needed to match the target rate, catching up if
	   a tick fires late due to browser scheduling jitter. */
	var total = rate * sec;
	var injected = 0;
	var t0 = performance.now();
	var el = $('dbg-bench-output');
	if (el) el.textContent = 'Rate sim: ' + rate + ' evt/s for ' + sec + 's...';
	dbgRateTimer = setInterval(function () {
		var elapsed = (performance.now() - t0) / 1000;
		var target = Math.min(Math.round(elapsed * rate), total);
		var n = target - injected;
		if (n > 0) {
			for (var i = 0; i < n; i++) _injectTestEvent();
			injected = target;
		}
		if (injected >= total) {
			dbgStopRateSim();
			if (el) el.textContent += '\nRate sim complete. '
				+ injected + ' events in ' + elapsed.toFixed(1) + 's';
		}
	}, 20);
}

function dbgStopRateSim() {
	if (dbgRateTimer) {
		clearInterval(dbgRateTimer);
		dbgRateTimer = null;
	}
}

function dbgChartStress() {
	var el = $('dbg-bench-output');
	if (!el) return;
	var needed = CHART_MAX_EVENTS - chartEvents.length;
	if (needed <= 0) {
		el.textContent = 'Chart already at max (' + CHART_MAX_EVENTS + ' events).';
		return;
	}
	el.textContent = 'Filling chart to ' + CHART_MAX_EVENTS + ' events...';
	setTimeout(function () {
		var t0 = performance.now();
		_injectBulkEvents(needed);
		var elapsed = performance.now() - t0;
		el.textContent = 'Chart stress: ' + needed + ' events injected in '
			+ elapsed.toFixed(1) + 'ms\nChart now at ' + chartEvents.length
			+ ' / ' + CHART_MAX_EVENTS;
		refreshDebugDashboard();
	}, 16);
}

/* Debug tab event wiring (IIFE — only runs once) */
(function initDebugTab() {
	var refresh = $('dbg-refresh');
	var clearAll = $('dbg-clear-all');
	var resetPerf = $('dbg-reset-perf');
	var autoRefresh = $('dbg-auto-refresh');
	var rateSim = $('dbg-rate-sim');
	var rateStop = $('dbg-rate-stop');
	var chartStress = $('dbg-chart-stress');
	var injectCustom = $('dbg-inject-custom');
	var injectBulk = $('dbg-inject-bulk');

	if (refresh) refresh.addEventListener('click', refreshDebugDashboard);

	if (clearAll) clearAll.addEventListener('click', function () {
		/* Reuse the monitor Clear button logic */
		var monClear = $('mon-clear');
		if (monClear) monClear.click();
		/* Reset perf metrics */
		for (var k in perfMetrics) {
			if (perfMetrics.hasOwnProperty(k)) perfMetrics[k] = 0;
		}
		var bench = $('dbg-bench-output');
		if (bench) bench.textContent = 'Cleared.';
		refreshDebugDashboard();
	});

	if (resetPerf) resetPerf.addEventListener('click', function () {
		for (var k in perfMetrics) {
			if (perfMetrics.hasOwnProperty(k)) perfMetrics[k] = 0;
		}
		refreshDebugDashboard();
	});

	if (autoRefresh) autoRefresh.addEventListener('change', function () {
		dbgToggleAutoRefresh(this.checked);
	});

	if (rateSim) rateSim.addEventListener('click', function () {
		dbgStartRateSim(100, 10);
	});

	if (rateStop) rateStop.addEventListener('click', dbgStopRateSim);

	if (chartStress) chartStress.addEventListener('click', dbgChartStress);

	/* Inject buttons by data-inject attribute */
	bindAll('[data-inject]', 'click', function (e) {
		_injectTestEvent(e.currentTarget.getAttribute('data-inject'));
	});

	/* Custom model inject */
	if (injectCustom) injectCustom.addEventListener('click', function () {
		var model = $('dbg-custom-model');
		if (model && model.value.trim()) {
			_injectTestEvent(model.value.trim());
		}
	});

	/* Bulk inject */
	if (injectBulk) injectBulk.addEventListener('click', function () {
		var count = parseInt($('dbg-bulk-count').value, 10) || 1000;
		var models = parseInt($('dbg-bulk-models').value, 10) || 5;
		_injectBulkEvents(count, models);
		refreshDebugDashboard();
	});

	/* Benchmark buttons by data-bench attribute */
	bindAll('[data-bench]', 'click', function (e) {
		dbgBenchmark(parseInt(e.currentTarget.getAttribute('data-bench'), 10));
	});
})();
