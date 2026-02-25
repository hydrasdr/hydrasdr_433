/* ---- Event rate tracking ---- */
function recordEventRate(count) {
	var now = Date.now();
	for (var i = 0; i < count; i++) {
		rateWindow.push(now);
	}
}

function calcRate() {
	var now = Date.now();
	var cutoff = now - RATE_WINDOW_MS;
	/* Prune old timestamps */
	while (rateWindow.length > 0 && rateWindow[0] < cutoff) {
		rateWindow.shift();
	}
	if (rateWindow.length === 0) return 0;
	return rateWindow.length / (RATE_WINDOW_MS / 1000);
}

function updateRateDisplay() {
	var rate = calcRate();
	elMonRate.textContent = rate.toFixed(1) + ' evt/s';
}

/* Start rate display timer (updates every 500ms) */
function startRateTimer() {
	if (rateTimerId) return;
	rateTimerId = setInterval(updateRateDisplay, 500);
}

/* ---- WebSocket ---- */
function connect() {
	var proto = location.protocol === 'https:' ? 'wss:' : 'ws:';
	ws = new WebSocket(proto + '//' + location.host);
	setStatus('wait', 'connecting');

	ws.onopen = function () {
		setStatus('on', 'connected');
		reconnectDelay = RECONNECT_BASE_MS;
		reconnectCount = 0;
		gotInitialMeta = false;
		rpcQueue = []; /* clear any stale callbacks */
		/* Force all report options ON so server always sends full data */
		forceServerReportMeta();
		/* Delay queries so auto-sent data (meta + history) arrives
		   while rpcQueue is empty. History can contain state messages
		   (no model key) that would be mis-consumed as RPC responses. */
		setTimeout(queryInitialState, 150);
	};

	ws.onmessage = function (e) {
		var msg;
		try { msg = JSON.parse(e.data); } catch (err) { return; }

		/* Shutdown */
		if (msg.shutdown) {
			setStatus('off', 'server stopped');
			return;
		}

		/* Event with model key — queue for rAF batch flush */
		if (msg.model !== undefined) {
			/* Ensure hi-res client arrival time is always stored.
			   If server didn't send time or sent without fractions,
			   stamp with client-side millisecond precision so the
			   hi-res toggle can show/hide fractions for ALL events. */
			if (!msg.time) {
				msg.time = nowTime();
			} else if (msg.time.indexOf('.') === -1) {
				msg.time = msg.time + '.' + pad3(new Date().getMilliseconds());
			}

			/* Track rate regardless of pause state */
			recordEventRate(1);

			/* If paused, buffer — replayed on resume (nothing lost) */
			if (paused) {
				pausedEvents.push(msg);
				return;
			}

			updateDeviceRegistry(msg);
			pendingEvents.push(msg);
			if (!rafId) rafId = requestAnimationFrame(flushEvents);
			return;
		}

		/* Log message (has lvl + msg keys) */
		if (msg.lvl !== undefined && msg.msg !== undefined) {
			appendSyslogEntry(msg);
			return;
		}

		/* Auto-sent meta on connect (arrives before RPC responses).
		   The server sends meta_data() on WEBSOCKET_HANDSHAKE_DONE,
		   which has center_frequency but no result/error/model keys.
		   Only consume the first one as auto-sent; subsequent ones
		   with center_frequency are RPC responses to get_meta. */
		if (!gotInitialMeta && msg.center_frequency !== undefined) {
			gotInitialMeta = true;
			metaCache = msg;
			updateHeaderFromMeta(msg);
			populateSettingsFromMeta(msg);
			if (msg.web_ui_debug) {
				debugEnabled = true;
				if (elDbgTabBtn) elDbgTabBtn.style.display = '';
				if (elDbgCheckbox) elDbgCheckbox.checked = true;
			}
			return;
		}

		/* RPC response with explicit result/error — always dequeue */
		if (msg.result !== undefined || msg.error !== undefined) {
			if (rpcQueue.length > 0) {
				var cb = rpcQueue.shift();
				if (msg.result !== undefined) cb(msg.result, null);
				else cb(null, msg.error);
			}
			return;
		}

		/* Distinguish SDR broadcasts from raw RPC responses.
		   Broadcasts only contain known SDR event keys:
		   center_frequency, frequencies, sample_rate, freq_correction.
		   Raw RPC responses (get_meta, get_stats, get_protocols,
		   get_dev_info) contain other keys. */
		var BROADCAST_KEYS = {center_frequency:1, frequencies:1, sample_rate:1, freq_correction:1};
		var isBroadcast = true;
		for (var bk in msg) {
			if (msg.hasOwnProperty(bk) && !BROADCAST_KEYS[bk]) {
				isBroadcast = false;
				break;
			}
		}

		/* Unsolicited SDR broadcast (freq change, rate change, etc.)
		   Merge into metaCache instead of replacing it. */
		if (isBroadcast) {
			if (metaCache) {
				if (msg.center_frequency !== undefined) metaCache.center_frequency = msg.center_frequency;
				if (msg.frequencies) metaCache.frequencies = msg.frequencies;
				if (msg.sample_rate !== undefined) metaCache.samp_rate = msg.sample_rate;
				if (msg.freq_correction !== undefined) metaCache.ppm_error = msg.freq_correction;
			}
			updateHeaderFromMeta(metaCache);
			populateSettingsFromMeta(metaCache);
			return;
		}

		/* Raw RPC response (ret_code=1) — dequeue callback */
		if (rpcQueue.length > 0) {
			var cb2 = rpcQueue.shift();
			cb2(msg, null);
			return;
		}
	};

	ws.onclose = function () {
		ws = null;
		setStatus('off', 'disconnected');
		scheduleReconnect();
	};

	ws.onerror = function () {
		if (ws) ws.close();
	};
}

function scheduleReconnect() {
	reconnectCount++;
	setStatus('wait', 'reconnecting (' + reconnectCount + ')');
	setTimeout(function () {
		reconnectDelay = Math.min(reconnectDelay * 1.5, RECONNECT_MAX_MS);
		connect();
	}, reconnectDelay);
}

function setStatus(cls, text) {
	elStatus.className = cls;
	elStatus.textContent = text;
}

/* ---- RPC (cmd protocol over WebSocket) ---- */
function rpc(method, args, cb) {
	if (!ws || ws.readyState !== 1) return;
	var msg = { cmd: method };
	if (args) {
		if (args.arg !== undefined) msg.arg = args.arg;
		if (args.val !== undefined) msg.val = args.val;
	}
	if (cb) rpcQueue.push(cb);
	ws.send(JSON.stringify(msg));
}

/* ---- Initial queries ---- */
function queryInitialState() {
	rpc('get_dev_info', null, function (r) {
		if (r && typeof r === 'object') {
			var parts = [];
			for (var k in r) {
				if (r.hasOwnProperty(k)) parts.push(k + ': ' + r[k]);
			}
			elSysDev.textContent = parts.join('\n') || '--';
		} else {
			elSysDev.textContent = r || '--';
		}
	});
	rpc('get_version', null, function (r) {
		if (r) {
			elSysDev.textContent = elSysDev.textContent + '\nversion: ' + r;
		}
	});
	rpc('get_meta', null, function (r) {
		if (r) {
			metaCache = (typeof r === 'string') ? tryParse(r) : r;
			if (metaCache) {
				updateHeaderFromMeta(metaCache);
				populateSettingsFromMeta(metaCache);
				updateSystemMeta(metaCache);
			}
		}
	});
	rpc('get_gain', null, function (r) {
		if (r !== null && r !== undefined) {
			var s = String(r);
			if (s.indexOf('=') !== -1) {
				var parts = s.split('=');
				selectGainMode(parts[0]);
				$('s-gain').value = parts[1];
			} else {
				selectGainMode('auto');
				$('s-gain').value = (s === '0' || s === '') ? '' : s;
			}
		}
	});
	rpc('get_ppm_error', null, function (r) {
		if (r !== null) $('s-ppm').value = r;
	});
	rpc('get_hop_interval', null, function (r) {
		if (r !== null) $('s-hop').value = r;
	});
	rpc('get_conversion_mode', null, function (r) {
		if (r !== null) $('s-convert').value = r;
	});
	rpc('get_verbosity', null, function (r) {
		if (r !== null) $('s-verbosity').value = r;
	});
	rpc('get_protocols', null, function (r) {
		if (r) {
			var data = (typeof r === 'string') ? tryParse(r) : r;
			if (data && data.protocols) {
				protoList = data.protocols;
				renderProtocols(protoList);
			}
		}
	});
	refreshStats();
}

/* Force all report options ON server-side.
   Web UI uses client-side display toggles only.
   Server must always send complete data so toggling reveals retroactive data. */
function forceServerReportMeta() {
	rpc('report_meta', {arg: 'level', val: 1});
	rpc('report_meta', {arg: 'bits', val: 1});
	rpc('report_meta', {arg: 'protocol', val: 1});
	rpc('report_meta', {arg: 'description', val: 1});
	rpc('report_meta', {arg: 'hires', val: 1});
}
