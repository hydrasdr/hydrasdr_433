/* DEBUG: Test data injection helper Call from browser console: _injectTestEvent('Acurite-Tower') */
var testTemplates = {
	/* --- Weather stations --- */
	'Kia-TPMS': {model:'Kia-TPMS', id:1234, pressure_kPa:230.5,
		temperature_C:22.3, battery_ok:1, bits:'04d2e69c01',
		rssi:-8.5, snr:15.2, noise:-24.0, protocol:198, description:'Kia TPMS'},
	'Acurite-Tower': {model:'Acurite-Tower', id:5678, temperature_C:18.7,
		humidity:62, battery_ok:1, bits:'162e3e4b0a',
		rssi:-12.1, snr:10.5, noise:-22.0, protocol:40,
		description:'Acurite tower sensor'},
	'LaCrosse-TX141W': {model:'LaCrosse-TX141W', id:91, temperature_C:21.0,
		humidity:55, bits:'5b00d237f8',
		rssi:-15.2, snr:8.0, noise:-23.0, protocol:141,
		description:'LaCrosse TX141W'},
	'Oregon-THN132N': {model:'Oregon-THN132N', id:42, temperature_C:19.5,
		battery_ok:0, bits:'2a0c3195e0',
		rssi:-20.0, snr:5.0, noise:-25.0, protocol:19,
		description:'Oregon THN132N'},
	'Nexus-TH': {model:'Nexus-TH', id:107, temperature_C:23.1, humidity:48,
		channel:2, bits:'6b17302e',
		rssi:-11.0, snr:12.0, noise:-23.0, protocol:47,
		description:'Nexus Temperature/Humidity'},
	'Acurite-5n1': {model:'Acurite-5n1', id:3100, wind_avg_km_h:12.5,
		wind_dir_deg:225, rain_mm:3.2, temperature_C:16.4, humidity:71,
		battery_ok:1, bits:'0c1ce10347a20d',
		rssi:-14.0, snr:9.0, noise:-23.0, protocol:40,
		description:'Acurite 5-in-1 weather station'},
	'Fine-Offset-WH65B': {model:'Fine-Offset-WH65B', id:210, uv:4,
		light_lux:32000, wind_avg_km_h:8.3, rain_mm:1.5, temperature_C:24.1,
		humidity:58, battery_ok:1, rssi:-10.5, snr:11.0, noise:-21.5,
		protocol:55, description:'Fine Offset WH65B solar weather station'},
	'WS2032': {model:'WS2032', id:480, wind_avg_m_s:3.7, wind_dir_deg:180,
		rain_mm:0.8, temperature_C:13.2, humidity:83, battery_ok:1,
		rssi:-16.0, snr:7.5, noise:-23.5, protocol:63,
		description:'Hyundai WS2032 weather station'},
	/* --- Indoor sensors --- */
	'Ambient-Weather-WH31E': {model:'Ambient-Weather-WH31E', id:55,
		temperature_C:22.8, humidity:45, battery_ok:1, channel:1,
		rssi:-9.0, snr:13.0, noise:-22.0, protocol:56,
		description:'Ambient Weather WH31E thermo-hygrometer'},
	'Prologue-TH': {model:'Prologue-TH', id:130, temperature_C:20.5,
		humidity:52, channel:3, battery_ok:1, rssi:-13.5, snr:9.5,
		noise:-23.0, protocol:22, description:'Prologue temperature/humidity'},
	'GT-WT02': {model:'GT-WT02', id:77, temperature_C:21.3, humidity:60,
		battery_ok:1, rssi:-11.5, snr:10.0, noise:-21.5,
		protocol:37, description:'GT-WT02 indoor sensor'},
	'Bresser-3CH': {model:'Bresser-3CH', id:200, temperature_C:19.8,
		humidity:67, channel:2, battery_ok:1, rssi:-14.0, snr:8.5,
		noise:-22.5, protocol:119, description:'Bresser 3-channel sensor'},
	/* --- TPMS --- */
	'Schrader-TPMS': {model:'Schrader-TPMS', id:0xAB12CD, pressure_kPa:245.0,
		temperature_C:28.5, flags:0, bits:'ab12cdf51c00',
		rssi:-7.0, snr:16.0, noise:-23.0, protocol:128,
		description:'Schrader TPMS sensor'},
	'Toyota-TPMS': {model:'Toyota-TPMS', id:0x11A03F, pressure_kPa:232.0,
		temperature_C:25.0, battery_ok:1, rssi:-9.5, snr:14.0, noise:-23.5,
		protocol:198, description:'Toyota TPMS sensor'},
	/* --- Cooking / BBQ --- */
	'Maverick-ET73x': {model:'Maverick-ET73x', id:620, temperature_1_C:72.5,
		temperature_2_C:85.3, rssi:-18.0, snr:6.0, noise:-24.0,
		protocol:57, description:'Maverick ET-73x BBQ thermometer'},
	/* --- Pool --- */
	'Thermopro-TX2': {model:'Thermopro-TX2', id:95, temperature_C:26.7,
		rssi:-12.0, snr:11.5, noise:-23.5, protocol:168,
		description:'ThermoPro TX-2 pool/spa sensor'},
	/* --- Soil --- */
	'Springfield-Soil': {model:'Springfield-Soil', id:310, moisture:42,
		temperature_C:15.3, rssi:-17.0, snr:7.0, noise:-24.0,
		protocol:180, description:'Springfield soil moisture sensor'},
	/* --- Energy --- */
	'Efergy-e2': {model:'Efergy-e2', id:4410, power_W:850, battery_ok:1,
		rssi:-15.5, snr:8.0, noise:-23.5, protocol:36,
		description:'Efergy e2 energy monitor'},
	'CurrentCost-TX': {model:'CurrentCost-TX', id:2200, power_W:1230,
		energy_kWh:14.7, rssi:-13.0, snr:10.0, noise:-23.0,
		protocol:83, description:'CurrentCost TX energy transmitter'},
	/* --- Security --- */
	'DSC-Security': {model:'DSC-Security', id:0x8F320, event:1, battery_ok:1,
		bits:'08f32001', rssi:-16.0, snr:7.5, noise:-23.5,
		protocol:68, description:'DSC wireless security sensor'},
	'Interlogix': {model:'Interlogix', id:0x5E100, event:0, battery_ok:1,
		tamper:0, bits:'05e10000c3',
		rssi:-14.5, snr:9.0, noise:-23.5, protocol:113,
		description:'Interlogix security sensor'},
	/* --- Smoke / Fire --- */
	'Smoke-GS558': {model:'Smoke-GS558', id:7300, alarm:0, battery_ok:1,
		rssi:-11.0, snr:12.0, noise:-23.0, protocol:160,
		description:'GS 558 smoke detector'},
	/* --- Water leak --- */
	'Govee-Water': {model:'Govee-Water', id:880, leak:0, battery_ok:1,
		rssi:-10.0, snr:13.0, noise:-23.0, protocol:171,
		description:'Govee water leak detector'},
	/* --- Shutter / blind --- */
	'Somfy-RTS': {model:'Somfy-RTS', id:0x3A100, button:1, state:0,
		rssi:-19.0, snr:5.5, noise:-24.5, protocol:70,
		description:'Somfy RTS rolling shutter'},
	/* --- Tank level --- */
	'Oil-Watchman': {model:'Oil-Watchman', id:560, depth_cm:72,
		temperature_C:8.5, rssi:-18.5, snr:6.0, noise:-24.5,
		protocol:145, description:'Oil Watchman tank level sensor'},
	/* --- Doorbell --- */
	'Byron-BY': {model:'Byron-BY', id:1440, button:1, chime:5,
		rssi:-12.5, snr:10.5, noise:-23.0, protocol:94,
		description:'Byron BY wireless doorbell'},
	/* --- Remote / keyfob --- */
	'Microchip-HCS200': {model:'Microchip-HCS200', id:0x1F200, button:2,
		battery_ok:1, encrypted:1, bits:'01f200a7e3b2',
		rssi:-15.0, snr:8.5, noise:-23.5, protocol:150,
		description:'Microchip HCS200 keyfob'}
};

/* Randomize sensor values in a test message to simulate realistic variation */
function mutateTestValues(msg) {
	if (msg.temperature_C !== undefined) msg.temperature_C = +(msg.temperature_C + (Math.random()-0.5)*4).toFixed(1);
	if (msg.temperature_1_C !== undefined) msg.temperature_1_C = +(msg.temperature_1_C + (Math.random()-0.5)*4).toFixed(1);
	if (msg.temperature_2_C !== undefined) msg.temperature_2_C = +(msg.temperature_2_C + (Math.random()-0.5)*4).toFixed(1);
	if (msg.humidity !== undefined) msg.humidity = Math.round(msg.humidity + (Math.random()-0.5)*10);
	if (msg.pressure_kPa !== undefined) msg.pressure_kPa = +(msg.pressure_kPa + (Math.random()-0.5)*20).toFixed(1);
	if (msg.wind_avg_km_h !== undefined) msg.wind_avg_km_h = +(Math.max(0, msg.wind_avg_km_h + (Math.random()-0.5)*6)).toFixed(1);
	if (msg.wind_avg_m_s !== undefined) msg.wind_avg_m_s = +(Math.max(0, msg.wind_avg_m_s + (Math.random()-0.5)*2)).toFixed(1);
	if (msg.wind_dir_deg !== undefined) msg.wind_dir_deg = Math.round((msg.wind_dir_deg + (Math.random()-0.5)*30 + 360) % 360);
	if (msg.rain_mm !== undefined) msg.rain_mm = +(msg.rain_mm + Math.random()*0.4).toFixed(1);
	if (msg.power_W !== undefined) msg.power_W = Math.round(Math.max(0, msg.power_W + (Math.random()-0.5)*100));
	if (msg.energy_kWh !== undefined) msg.energy_kWh = +(msg.energy_kWh + Math.random()*0.3).toFixed(1);
	if (msg.moisture !== undefined) msg.moisture = Math.round(Math.max(0, Math.min(100, msg.moisture + (Math.random()-0.5)*10)));
	if (msg.depth_cm !== undefined) msg.depth_cm = Math.round(Math.max(0, msg.depth_cm + (Math.random()-0.5)*4));
	if (msg.uv !== undefined) msg.uv = Math.round(Math.max(0, msg.uv + (Math.random()-0.5)*2));
	if (msg.light_lux !== undefined) msg.light_lux = Math.round(Math.max(0, msg.light_lux + (Math.random()-0.5)*1000));
	if (msg.rssi !== undefined) msg.rssi = +(msg.rssi + (Math.random()-0.5)*6).toFixed(1);
}

window._injectTestEvent = function (model) {
	model = model || 'Acurite-Tower';
	var base = testTemplates[model]
		|| {model: model, id: Math.floor(Math.random()*9999),
			temperature_C: 20 + Math.random()*10, rssi: -10 - Math.random()*20};
	var msg = {};
	for (var k in base) msg[k] = base[k];
	msg.id = base.id + Math.floor(Math.random() * 10);
	msg.time = nowTime();
	mutateTestValues(msg);
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
	count = count || DBG_BULK_DEFAULT;
	numModels = numModels || DBG_MODELS_DEFAULT;
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

/* Populate the UI with all model templates, each with multiple device IDs
   and varied events.  Great for demos and visual testing.
   Usage: dbgPopulateAllModels(3)  — 3 events per device, ~75 devices */
function dbgPopulateAllModels(eventsPerModel) {
	eventsPerModel = eventsPerModel || DBG_POP_DEFAULT;
	var models = Object.keys(testTemplates);
	var devicesPerModel = [0, 1, 2]; /* offsets → 2-3 unique IDs per model */
	var t0 = performance.now();
	var totalEvents = 0;
	for (var m = 0; m < models.length; m++) {
		var tpl = testTemplates[models[m]];
		for (var d = 0; d < devicesPerModel.length; d++) {
			for (var e = 0; e < eventsPerModel; e++) {
				var msg = {};
				for (var k in tpl) msg[k] = tpl[k];
				msg.id = tpl.id + devicesPerModel[d];
				msg.time = nowTime();
				mutateTestValues(msg);
				recordEventRate(1);
				updateDeviceRegistry(msg);
				pendingEvents.push(msg);
				totalEvents++;
			}
		}
	}
	if (!rafId) rafId = requestAnimationFrame(flushEvents);
	var elapsed = performance.now() - t0;
	console.log('[populate] ' + totalEvents + ' events across '
		+ models.length + ' models in ' + elapsed.toFixed(1) + 'ms');
	return {models: models.length, events: totalEvents, ms: elapsed};
}

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
	}, DBG_RATE_TICK_MS);
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
		dbgStartRateSim(DBG_RATE_DEFAULT, DBG_RATE_DUR);
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
		var count = parseInt($('dbg-bulk-count').value, 10) || DBG_BULK_DEFAULT;
		var models = parseInt($('dbg-bulk-models').value, 10) || DBG_MODELS_DEFAULT;
		_injectBulkEvents(count, models);
		refreshDebugDashboard();
	});

	/* Populate all models */
	var populateBtn = $('dbg-populate');
	if (populateBtn) populateBtn.addEventListener('click', function () {
		var n = parseInt($('dbg-pop-count').value, 10) || DBG_POP_DEFAULT;
		var el = $('dbg-bench-output');
		if (el) el.textContent = 'Populating all models...';
		setTimeout(function () {
			var r = dbgPopulateAllModels(n);
			if (el) el.textContent = 'Populated ' + r.models + ' models, '
				+ r.events + ' events in ' + r.ms.toFixed(1) + ' ms';
			refreshDebugDashboard();
		}, 16);
	});

	/* Benchmark buttons by data-bench attribute */
	bindAll('[data-bench]', 'click', function (e) {
		dbgBenchmark(parseInt(e.currentTarget.getAttribute('data-bench'), 10));
	});
})();
