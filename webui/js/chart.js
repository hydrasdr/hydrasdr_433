/* ---- Cached chart font strings (avoid getComputedStyle per frame) ---- */
var _chartFont9 = '';
var _chartFont11 = '';
function chartFont(size) {
	/* Lazy-init: compute once, reuse forever */
	if (!_chartFont9) {
		var ff = getComputedStyle(document.body).fontFamily;
		_chartFont9 = '9px ' + ff;
		_chartFont11 = '11px ' + ff;
	}
	return size === 11 ? _chartFont11 : _chartFont9;
}

/* ---- Activity chart: event recording (dot plot) ---- */
function recordChartEvent(ts, msg) {
	var db = null;
	if (msg.rssi !== undefined) db = +msg.rssi;
	else if (msg.snr !== undefined) db = +msg.snr;
	chartEvents.push({
		ts: ts,
		db: db,
		model: msg.model || '',
		id: (msg.id !== undefined && msg.id !== null) ? String(msg.id) : '',
		flagStart: msg.flag_start === 1 ? 1 : 0,
		flagEnd: msg.flag_end === 1 ? 1 : 0
	});
	chartDirty = true;
	/* Trim oldest events with slack to amortize the O(n) splice cost.
	   Only trim when CHART_TRIM_SLACK over limit, reducing frequency ~10x. */
	if (chartEvents.length > CHART_MAX_EVENTS + CHART_TRIM_SLACK) {
		chartEvents.splice(0, chartEvents.length - CHART_MAX_EVENTS);
	}
}

/* ---- Activity chart: canvas helpers ---- */
function roundRect(ctx, x, y, w, h, r) {
	if (w < 2 * r) r = w / 2;
	if (h < 2 * r) r = h / 2;
	ctx.beginPath();
	ctx.moveTo(x + r, y);
	ctx.arcTo(x + w, y, x + w, y + h, r);
	ctx.arcTo(x + w, y + h, x, y + h, r);
	ctx.arcTo(x, y + h, x, y, r);
	ctx.arcTo(x, y, x + w, y, r);
	ctx.closePath();
}

function chartEventY(ev, marginTop, plotH, dbMin, dbMax, noDbModels, noDbCount,
		laneMargin, laneStep, laneArea) {
	if (ev.db !== null) {
		var dbClamped = Math.max(dbMin, Math.min(dbMax, ev.db));
		return Math.round(marginTop + plotH - ((dbClamped - dbMin) / (dbMax - dbMin)) * plotH);
	}
	if (ev.model && ev.model in noDbModels) {
		var lane = noDbModels[ev.model];
		return Math.round(marginTop + laneMargin + (noDbCount > 1 ? lane * laneStep : laneArea / 2));
	}
	return Math.round(marginTop + plotH / 2);
}

/* Binary search: returns index of first event with ts >= target.
   chartEvents is sorted by ts (oldest first). O(log n). */
function chartBisect(target) {
	var lo = 0, hi = chartEvents.length;
	while (lo < hi) {
		var mid = (lo + hi) >>> 1;
		if (chartEvents[mid].ts < target) lo = mid + 1;
		else hi = mid;
	}
	return lo;
}

/* ---- Activity chart: canvas rendering (dot plot) ---- */
function renderActivityChart(canvas, wrap) {
	if (!canvas || !wrap) return;

	/* Size canvas to CSS pixel dimensions (handle DPR for sharpness)
	   Only resize when dimensions actually change to avoid jitter */
	var dpr = window.devicePixelRatio || 1;
	var w = wrap.clientWidth;
	var h = wrap.clientHeight;
	if (w === 0 || h === 0) return;
	var newW = Math.round(w * dpr);
	var newH = Math.round(h * dpr);
	if (canvas.width !== newW || canvas.height !== newH) {
		canvas.width = newW;
		canvas.height = newH;
	}
	var ctx = canvas.getContext('2d');
	ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
	/* Always clear entire canvas before redraw — prevents ghosting */
	ctx.clearRect(0, 0, w, h);

	var len = chartEvents.length;
	if (len === 0) {
		/* Empty state */
		ctx.fillStyle = '#666';
		ctx.font = chartFont(11);
		ctx.textAlign = 'center';
		ctx.fillText('Waiting for events\u2026', w / 2, h / 2 + 4);
		return;
	}

	/* Layout constants */
	var marginLeft = 30;   /* Y-axis label space */
	var marginBottom = 14; /* X-axis label space */
	var marginTop = 4;
	var marginRight = 4;
	var plotW = w - marginLeft - marginRight;
	var plotH = h - marginTop - marginBottom;
	var dot = CHART_DOT_SIZE;

	/* Time range: pan-aware — right edge = now - panOffset.
	   Freeze time during interactions to prevent chart auto-scrolling. */
	var nowMs = chartFrozenNowMs || Date.now();
	chartLastNowMs = nowMs;
	var windowMs = chartWindowSec * 1000;
	var tMax = nowMs - chartPanOffsetMs;
	var tMin = tMax - windowMs;

	/* Binary search bounds for visible events — O(log n) */
	var iStart = chartBisect(tMin);
	var iEnd = chartBisect(tMax + 1);  /* exclusive upper bound */

	/* Y-axis: dB range — auto-scale from data if possible */
	var dbMin = CHART_DB_MIN;
	var dbMax = CHART_DB_MAX;

	/* Pre-compute time→pixel factor: avoids division per dot */
	var pxPerMs = (tMax > tMin) ? plotW / (tMax - tMin) : 0;

	/* Shared params object for sub-functions */
	var p = {
		ctx: ctx, w: w, h: h, canvas: canvas,
		marginLeft: marginLeft, marginRight: marginRight,
		marginTop: marginTop, marginBottom: marginBottom,
		plotW: plotW, plotH: plotH, dot: dot,
		tMin: tMin, tMax: tMax, dbMin: dbMin, dbMax: dbMax,
		iStart: iStart, iEnd: iEnd,
		pxPerMs: pxPerMs
	};

	/* Build no-dB lane map (shared by sessions + dots) */
	p.noDbModels = {};
	p.noDbCount = 0;
	for (var mi = iStart; mi < iEnd; mi++) {
		var mev = chartEvents[mi];
		if (mev.db === null && mev.model && !(mev.model in p.noDbModels)) {
			p.noDbModels[mev.model] = p.noDbCount++;
		}
	}
	p.laneMargin = Math.round(plotH * 0.1);
	p.laneArea = plotH - 2 * p.laneMargin;
	p.laneStep = p.noDbCount > 1 ? p.laneArea / (p.noDbCount - 1) : 0;

	/* Pre-compute Y coords for visible events — eliminates duplicate
	   chartEventY() calls in drawChartSessions + drawChartDots */
	for (var yi = iStart; yi < iEnd; yi++) {
		chartEvents[yi]._cy = chartEventY(chartEvents[yi], marginTop, plotH,
			dbMin, dbMax, p.noDbModels, p.noDbCount,
			p.laneMargin, p.laneStep, p.laneArea);
	}

	drawChartGrid(p);
	drawChartSessions(p);
	drawChartDots(p);
	drawChartOverlays(p);
}

function drawChartGrid(p) {
	var ctx = p.ctx;
	var font = chartFont(9);
	ctx.strokeStyle = '#2a2a2a';
	ctx.lineWidth = 0.5;
	ctx.fillStyle = '#555';
	ctx.font = font;

	/* Y-axis grid */
	ctx.textAlign = 'right';
	ctx.textBaseline = 'middle';
	var dbStep = 6;
	for (var dbv = p.dbMin; dbv <= p.dbMax; dbv += dbStep) {
		var yy = Math.round(p.marginTop + p.plotH - ((dbv - p.dbMin) / (p.dbMax - p.dbMin)) * p.plotH) + 0.5;
		ctx.beginPath();
		ctx.moveTo(p.marginLeft, yy);
		ctx.lineTo(p.w - p.marginRight, yy);
		ctx.stroke();
		ctx.fillText(dbv + '', p.marginLeft - 3, yy);
	}

	/* X-axis grid */
	ctx.textAlign = 'center';
	ctx.textBaseline = 'top';
	var pxPerMs = p.plotW / (p.tMax - p.tMin);
	var targetMs = 80 / pxPerMs;
	var GRID_STEPS = [
		1, 2, 5, 10, 20, 50, 100, 200, 500,
		1000, 2000, 5000, 10000, 15000, 30000,
		60000, 120000, 300000, 600000, 1800000, 3600000
	];
	var xStep = GRID_STEPS[GRID_STEPS.length - 1];
	for (var gi = 0; gi < GRID_STEPS.length; gi++) {
		if (GRID_STEPS[gi] >= targetMs) { xStep = GRID_STEPS[gi]; break; }
	}
	var SUB_DIVS = {
		1:1, 2:2, 5:5, 10:5, 20:4, 50:5,
		100:5, 200:4, 500:5, 1000:5, 2000:4, 5000:5, 10000:5,
		15000:3, 30000:6, 60000:6, 120000:4, 300000:5, 600000:6,
		1800000:6, 3600000:6
	};
	var subStep = xStep / (SUB_DIVS[xStep] || 5);

	/* Minor grid */
	ctx.strokeStyle = '#222';
	ctx.lineWidth = 0.3;
	var subStart = Math.ceil(p.tMin / subStep) * subStep;
	for (var st = subStart; st <= p.tMax; st += subStep) {
		if (st % xStep === 0) continue;
		var sx = Math.round(p.marginLeft + ((st - p.tMin) / (p.tMax - p.tMin)) * p.plotW) + 0.5;
		if (sx < p.marginLeft || sx > p.w - p.marginRight) continue;
		ctx.beginPath();
		ctx.moveTo(sx, p.marginTop);
		ctx.lineTo(sx, p.marginTop + p.plotH);
		ctx.stroke();
	}

	/* Major grid + labels */
	var xStart = Math.ceil(p.tMin / xStep) * xStep;
	for (var xt = xStart; xt <= p.tMax; xt += xStep) {
		var xx = Math.round(p.marginLeft + ((xt - p.tMin) / (p.tMax - p.tMin)) * p.plotW) + 0.5;
		if (xx < p.marginLeft || xx > p.w - p.marginRight) continue;
		ctx.strokeStyle = '#2a2a2a';
		ctx.lineWidth = 0.5;
		ctx.beginPath();
		ctx.moveTo(xx, p.marginTop);
		ctx.lineTo(xx, p.marginTop + p.plotH);
		ctx.stroke();
		ctx.fillStyle = '#555';
		ctx.fillText(fmtChartTime(xt, xStep), xx, p.marginTop + p.plotH + 2);
	}

	/* Baseline */
	ctx.strokeStyle = '#444';
	ctx.lineWidth = 1;
	ctx.beginPath();
	ctx.moveTo(p.marginLeft, p.marginTop + p.plotH);
	ctx.lineTo(p.w - p.marginRight, p.marginTop + p.plotH);
	ctx.stroke();
	ctx.beginPath();
	ctx.moveTo(p.marginLeft, p.marginTop);
	ctx.lineTo(p.marginLeft, p.marginTop + p.plotH);
	ctx.stroke();
}

function drawChartSessions(p) {
	var ctx = p.ctx;
	var sessionsByKey = {};
	for (var si2 = p.iStart; si2 < p.iEnd; si2++) {
		var sev = chartEvents[si2];
		var sKey = sev.model + '::' + sev.id;
		if (!sessionsByKey[sKey]) sessionsByKey[sKey] = [];
		var sList = sessionsByKey[sKey];
		var isNewSession = sList.length === 0
			|| sev.flagStart === 1
			|| (sList[sList.length - 1].length > 0 && sList[sList.length - 1][sList[sList.length - 1].length - 1].flagEnd === 1)
			|| (sev.ts - sList[sList.length - 1][sList[sList.length - 1].length - 1].ts > CHART_SESSION_GAP_MS);
		if (isNewSession) sList.push([]);
		sList[sList.length - 1].push(sev);
	}

	var bracketY = p.marginTop + p.plotH;
	var bracketH = 4;
	for (var bKey in sessionsByKey) {
		if (!sessionsByKey.hasOwnProperty(bKey)) continue;
		var sessions = sessionsByKey[bKey];
		for (var bs = 0; bs < sessions.length; bs++) {
			var sess = sessions[bs];
			if (sess.length === 0) continue;
			var bColor = sess[0].model ? modelColor(sess[0].model) : '#6a9955';
			var bx1 = Math.round(p.marginLeft + (sess[0].ts - p.tMin) * p.pxPerMs);
			var bx2 = (sess.length > 1)
				? Math.round(p.marginLeft + (sess[sess.length - 1].ts - p.tMin) * p.pxPerMs)
				: bx1;

			if (sess.length >= 2) {
				var byMin = 9999, byMax = -9999;
				for (var be = 0; be < sess.length; be++) {
					var by = sess[be]._cy;
					if (by < byMin) byMin = by;
					if (by > byMax) byMax = by;
				}
				var bPad = 4;
				ctx.globalAlpha = 0.12;
				ctx.fillStyle = bColor;
				roundRect(ctx, bx1 - bPad, byMin - bPad, (bx2 - bx1) + 2 * bPad,
					(byMax - byMin) + 2 * bPad, 3);
				ctx.fill();
				ctx.globalAlpha = 0.4;
				ctx.strokeStyle = bColor;
				ctx.lineWidth = 1;
				ctx.beginPath();
				ctx.moveTo(bx1 - bPad, byMin - bPad);
				ctx.lineTo(bx1 - bPad, byMax + bPad);
				ctx.stroke();
				ctx.beginPath();
				ctx.moveTo(bx2 + bPad, byMin - bPad);
				ctx.lineTo(bx2 + bPad, byMax + bPad);
				ctx.stroke();
			}

			ctx.strokeStyle = bColor;
			ctx.globalAlpha = 0.8;
			ctx.lineWidth = 1.5;
			if (bx2 - bx1 < 3) {
				ctx.beginPath();
				ctx.moveTo(bx1, bracketY);
				ctx.lineTo(bx1, bracketY + bracketH);
				ctx.stroke();
			} else {
				ctx.beginPath();
				ctx.moveTo(bx1, bracketY - bracketH);
				ctx.lineTo(bx1, bracketY);
				ctx.lineTo(bx2, bracketY);
				ctx.lineTo(bx2, bracketY - bracketH);
				ctx.stroke();
			}
		}
	}
	ctx.globalAlpha = 1.0;
}

function drawChartDots(p) {
	var ctx = p.ctx;
	ctx.globalAlpha = 0.7;
	for (var ei = p.iStart; ei < p.iEnd; ei++) {
		var ev = chartEvents[ei];
		var ex = Math.round(p.marginLeft + (ev.ts - p.tMin) * p.pxPerMs);
		var ey = ev._cy;
		ctx.fillStyle = ev.model ? modelColor(ev.model) : '#6a9955';
		ctx.fillRect(ex - 1, ey - 1, p.dot, p.dot);
	}
	ctx.globalAlpha = 1.0;
}

function drawChartOverlays(p) {
	var ctx = p.ctx;
	/* Shift+drag zoom selection overlay */
	if (chartZoomSel && chartZoomSel.canvas === p.canvas) {
		var zx1 = Math.max(p.marginLeft, Math.min(p.w - p.marginRight, chartZoomSel.startX));
		var zx2 = Math.max(p.marginLeft, Math.min(p.w - p.marginRight, chartZoomSel.currentX));
		var zLeft = Math.min(zx1, zx2);
		var zWidth = Math.abs(zx2 - zx1);
		ctx.fillStyle = 'rgba(86, 156, 214, 0.2)';
		ctx.fillRect(zLeft, p.marginTop, zWidth, p.plotH);
		ctx.strokeStyle = 'rgba(86, 156, 214, 0.7)';
		ctx.lineWidth = 1;
		ctx.beginPath();
		ctx.moveTo(zx1 + 0.5, p.marginTop);
		ctx.lineTo(zx1 + 0.5, p.marginTop + p.plotH);
		ctx.stroke();
		ctx.beginPath();
		ctx.moveTo(zx2 + 0.5, p.marginTop);
		ctx.lineTo(zx2 + 0.5, p.marginTop + p.plotH);
		ctx.stroke();
	}

	/* Zoom indicator */
	var font = chartFont(9);
	ctx.fillStyle = '#555';
	ctx.font = font;
	ctx.textAlign = 'right';
	ctx.textBaseline = 'top';
	var zoomLabel;
	if (chartWindowSec >= 60) zoomLabel = Math.round(chartWindowSec / 60) + 'min';
	else if (chartWindowSec >= 1) zoomLabel = Math.round(chartWindowSec) + 's';
	else zoomLabel = Math.round(chartWindowSec * 1000) + 'ms';
	ctx.fillText(zoomLabel, p.w - p.marginRight - 2, p.marginTop + 2);

	/* LIVE / pan offset indicator */
	if (chartPanOffsetMs === 0) {
		ctx.fillStyle = '#6a9955';
		ctx.fillText('LIVE', p.w - p.marginRight - 2, p.marginTop + 12);
	} else {
		ctx.fillStyle = '#ce9178';
		ctx.fillText('\u25c0 ' + fmtPanOffset(chartPanOffsetMs), p.w - p.marginRight - 2, p.marginTop + 12);
	}

	/* Y-axis label */
	ctx.save();
	ctx.fillStyle = '#555';
	ctx.font = font;
	ctx.textAlign = 'center';
	ctx.translate(8, p.marginTop + p.plotH / 2);
	ctx.rotate(-Math.PI / 2);
	ctx.fillText('dB', 0, 0);
	ctx.restore();
}

function fmtPanOffset(ms) {
	var sec = Math.round(ms / 1000);
	if (sec < 60) return sec + 's ago';
	if (sec < 3600) return Math.round(sec / 60) + 'min ago';
	return Math.round(sec / 3600) + 'h ago';
}

function fmtChartTime(ms, step) {
	var d = new Date(ms);
	var h = pad2(d.getHours());
	var m = pad2(d.getMinutes());
	var s = pad2(d.getSeconds());
	var frac = d.getMilliseconds();
	if (step < 1000) return h + ':' + m + ':' + s + '.' + pad3(frac);
	if (step < 60000) return h + ':' + m + ':' + s;
	return h + ':' + m;
}

function renderAllCharts() {
	if (document.hidden) return;
	var isLive = (chartPanOffsetMs === 0);
	/* In live mode, always render (view scrolls with time).
	   In pan mode, only render when something changed. */
	if (!isLive && !chartDirty) return;
	chartDirty = false;
	var t0 = performance.now();
	renderActivityChart(elDevChart, elDevChartWrap);
	renderActivityChart(elMonChart, elMonChartWrap);
	syncScrollbars();
	perfMetrics.lastChartMs = performance.now() - t0;
}

function syncScrollbars() {
	var nowMs = chartFrozenNowMs || Date.now();
	var oldest = chartEvents.length > 0 ? chartEvents[0].ts : nowMs;
	var range = nowMs - oldest;
	if (range < 1000) range = 1000;
	/* Scrollbar value: 0 = oldest, 1000 = live (now) */
	var val = Math.round(((nowMs - chartPanOffsetMs) - oldest) / range * 1000);
	val = Math.max(0, Math.min(1000, val));
	elMonChartScroll.value = val;
	elDevChartScroll.value = val;
}

/* Start/stop chart redraw timer (redraws every second) */
function startChartTimer() {
	if (chartTimerId) return;
	chartTimerId = setInterval(renderAllCharts, 1000);
}

/* ---- Chart zoom via mouse wheel (both charts) ---- */
/* Zoom anchored at cursor position — chart stays centered under mouse */
function chartWheelHandler(e) {
	e.preventDefault();
	e.stopPropagation();
	var canvas = e.target;
	var wrap = canvas.parentNode;
	var rect = canvas.getBoundingClientRect();
	var mx = e.clientX - rect.left;
	var marginLeft = 30, marginRight = 4;
	var plotW = wrap.clientWidth - marginLeft - marginRight;
	var windowMs = chartWindowSec * 1000;
	var nowMs = chartFreezeNow();
	chartFrozenNowMs = nowMs;
	var tMax = nowMs - chartPanOffsetMs;
	var tMin = tMax - windowMs;
	/* Time under cursor before zoom */
	var frac = (mx - marginLeft) / plotW;
	frac = Math.max(0, Math.min(1, frac));
	var tCursor = tMin + frac * windowMs;

	var oldWindow = chartWindowSec;
	if (e.deltaY < 0) {
		chartWindowSec = Math.max(CHART_ZOOM_MIN, chartWindowSec / CHART_ZOOM_FACTOR);
	} else {
		chartWindowSec = Math.min(CHART_ZOOM_MAX, chartWindowSec * CHART_ZOOM_FACTOR);
	}

	/* Adjust pan offset so tCursor stays at the same screen position */
	var newWindowMs = chartWindowSec * 1000;
	var newTMin = tCursor - frac * newWindowMs;
	var newTMax = newTMin + newWindowMs;
	chartPanOffsetMs = Math.max(0, nowMs - newTMax);

	chartDirty = true;
	renderAllCharts();
	chartFrozenNowMs = 0;
}
elDevChart.addEventListener('wheel', chartWheelHandler, { passive: false });
elMonChart.addEventListener('wheel', chartWheelHandler, { passive: false });

/* ---- Drag-to-pan handlers (shared helper) ---- */
function chartDragHelper(canvas, wrap) {
	canvas.addEventListener('mousedown', function (e) {
		if (e.button !== 0) return;
		if (e.shiftKey) {
			/* Shift+drag: rubber-band zoom selection */
			var rect = canvas.getBoundingClientRect();
			var mx = e.clientX - rect.left;
			var marginLeft = 30, marginRight = 4;
			var plotW = wrap.clientWidth - marginLeft - marginRight;
			var windowMs = chartWindowSec * 1000;
			chartFrozenNowMs = chartFreezeNow();
			var tMax = chartFrozenNowMs - chartPanOffsetMs;
			var tMin = tMax - windowMs;
			var startMs = tMin + ((mx - marginLeft) / plotW) * windowMs;
			chartZoomSel = {startX: mx, startMs: startMs, canvas: canvas, wrap: wrap, currentX: mx};
			canvas.style.cursor = 'col-resize';
			e.preventDefault();
			return;
		}
		chartFrozenNowMs = chartFreezeNow();
		chartDragging = true;
		chartDragStartX = e.clientX;
		chartDragStartOffset = chartPanOffsetMs;
		canvas.style.cursor = 'grabbing';
		e.preventDefault();
	});
	canvas.addEventListener('mousemove', function (e) {
		if (chartZoomSel && chartZoomSel.canvas === canvas) {
			e.preventDefault();
			var rect = canvas.getBoundingClientRect();
			chartZoomSel.currentX = e.clientX - rect.left;
			chartDirty = true;
			renderAllCharts();
			return;
		}
		if (!chartDragging) return;
		e.preventDefault();
		var dx = e.clientX - chartDragStartX;
		/* Convert pixel delta to time delta */
		var plotW = wrap.clientWidth - 34;  /* marginLeft(30) + marginRight(4) */
		var msPerPx = (chartWindowSec * 1000) / plotW;
		/* Drag right = pan forward (decrease offset), drag left = pan backward */
		var newOffset = chartDragStartOffset + dx * msPerPx;
		chartPanOffsetMs = Math.max(0, newOffset);
		chartDirty = true;
		renderAllCharts();
	});
	canvas.addEventListener('mouseup', function (e) {
		if (chartZoomSel && chartZoomSel.canvas === canvas) {
			var rect = canvas.getBoundingClientRect();
			var endX = e.clientX - rect.left;
			var marginLeft = 30, marginRight = 4;
			var plotW = wrap.clientWidth - marginLeft - marginRight;
			var windowMs = chartWindowSec * 1000;
			var tMax = chartFrozenNowMs - chartPanOffsetMs;
			var tMin = tMax - windowMs;
			var endMs = tMin + ((endX - marginLeft) / plotW) * windowMs;
			var selPx = Math.abs(endX - chartZoomSel.startX);
			var selMinMs = Math.min(chartZoomSel.startMs, endMs);
			var selMaxMs = Math.max(chartZoomSel.startMs, endMs);
			var selDurSec = (selMaxMs - selMinMs) / 1000;
			chartFrozenNowMs = 0;
			if (selPx > 5 && selDurSec >= CHART_ZOOM_MIN) {
				chartWindowSec = Math.max(CHART_ZOOM_MIN, Math.min(CHART_ZOOM_MAX, selDurSec));
				var selCenter = (selMinMs + selMaxMs) / 2;
				var realNow = Date.now();
				chartPanOffsetMs = Math.max(0, realNow - selCenter - (chartWindowSec * 500));
			}
			chartZoomSel = null;
			canvas.style.cursor = '';
			chartDirty = true;
			renderAllCharts();
			return;
		}
		if (chartDragging) {
			/* Adjust offset for elapsed time so view doesn't jump on unfreeze */
			if (chartFrozenNowMs) {
				chartPanOffsetMs = Math.max(0, chartPanOffsetMs + (Date.now() - chartFrozenNowMs));
				chartFrozenNowMs = 0;
			}
			chartDragging = false;
			canvas.style.cursor = '';
		}
	});
	canvas.addEventListener('mouseleave', function () {
		if (chartZoomSel && chartZoomSel.canvas === canvas) {
			chartZoomSel = null;
			chartFrozenNowMs = 0;
			canvas.style.cursor = '';
			chartDirty = true;
			renderAllCharts();
		}
		if (chartDragging) {
			if (chartFrozenNowMs) {
				chartPanOffsetMs = Math.max(0, chartPanOffsetMs + (Date.now() - chartFrozenNowMs));
				chartFrozenNowMs = 0;
			}
			chartDragging = false;
			canvas.style.cursor = '';
		}
	});
	/* Double-click to return to live */
	canvas.addEventListener('dblclick', function () {
		chartPanOffsetMs = 0;
		chartDirty = true;
		renderAllCharts();
	});
}
chartDragHelper(elDevChart, elDevChartWrap);
chartDragHelper(elMonChart, elMonChartWrap);

/* ---- Scrollbar input handlers ---- */
function handleChartScroll() {
	var val = +this.value;
	var nowMs = chartFreezeNow();
	chartFrozenNowMs = nowMs;
	if (val >= 998) {
		/* Snap to live */
		chartPanOffsetMs = 0;
	} else {
		var oldest = chartEvents.length > 0 ? chartEvents[0].ts : nowMs;
		var range = nowMs - oldest;
		if (range < 1000) range = 1000;
		var tMaxScroll = oldest + (val / 1000) * range;
		chartPanOffsetMs = Math.max(0, nowMs - tMaxScroll);
	}
	chartDirty = true;
	renderAllCharts();
	chartFrozenNowMs = 0;
}
elMonChartScroll.addEventListener('input', handleChartScroll);
elDevChartScroll.addEventListener('input', handleChartScroll);

/* ---- Zoom Reset button ---- */
elDevZoomReset.addEventListener('click', function () {
	chartWindowSec = CHART_WINDOW_SEC;
	chartPanOffsetMs = 0;
	chartDirty = true;
	renderAllCharts();
});

/* ---- History length selector ---- */
elDevHistory.addEventListener('change', function () {
	var sec = parseInt(this.value, 10);
	CHART_WINDOW_SEC = sec;
	CHART_ZOOM_MAX = Math.max(sec, 3600);
	CHART_MAX_EVENTS = Math.max(2000, Math.round(sec * 7));
	chartWindowSec = sec;
	/* Trim chartEvents to new max — single splice is O(n) */
	var excess = chartEvents.length - CHART_MAX_EVENTS;
	if (excess > 0) {
		chartEvents.splice(0, excess);
	}
	chartDirty = true;
	renderAllCharts();
});
