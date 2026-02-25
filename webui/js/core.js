/* hydrasdr_433 — self-hosted web UI
   Vanilla ES5, zero-framework, all browsers, maximum performance.

   Hot-path optimizations:
   - requestAnimationFrame batching (all events in one frame → one DOM flush)
   - Row object pool (recycle TR elements, zero GC pressure)
   - DocumentFragment batch insert (single reflow per frame)
   - Variable-tracked row count (no children.length layout thrash)
   - No innerHTML on hot path (direct DOM only)
*/

/* ---- Constants ---- */
var MAX_EVENTS = 500;
var RECONNECT_BASE_MS = 1000;
var RECONNECT_MAX_MS = 30000;
var STATS_INTERVAL_MS = 10000;
var MOD_NAMES = ['', 'OOK', 'FSK', 'FSK'];
var RATE_WINDOW_MS = 5000;

/* ---- State ---- */
var ws = null;
var reconnectDelay = RECONNECT_BASE_MS;
var reconnectCount = 0;
var eventCount = 0;
var rowCount = 0;
var rpcQueue = [];
var rpcQueueHead = 0;  /* index-based dequeue: avoids O(n) shift */
var metaCache = null;
var protoList = [];
var statsTimer = null;
var gotInitialMeta = false; /* flag: auto-sent meta received on connect */

/* ---- Pause / Resume state ---- */
var paused = false;
var pausedEvents = [];  /* buffered events while paused — replayed on resume */

/* ---- Event filter state ---- */
var monFilterStr = '';

/* ---- Event rate tracking (O(1) bucket counter) ---- */
var RATE_BUCKET_MS = 500;
var RATE_BUCKET_COUNT = 10;  /* RATE_WINDOW_MS / RATE_BUCKET_MS */
var rateBuckets = [0,0,0,0,0,0,0,0,0,0];
var rateBucketIdx = 0;
var rateBucketTs = 0;
var rateTimerId = null;

/* ---- Device registry state ---- */
var deviceRegistry = {};
var deviceKeys = [];
var deviceCount = 0;
var devFilterStr = '';
var devSortCol = 'seen';
var devSortAsc = false;
var devDetailKey = null;
var MAX_DEVICE_EVENTS = 200;
var DEVICE_DETAIL_PAGE = 20;
var devicesTabActive = false;
var devRafId = 0;
var devActiveKeys = {};    /* key → timestamp of last update, for green flash */
var devActiveCount = 0;    /* approximate size of devActiveKeys for purge trigger */
var DEV_FLASH_MS = 3000;   /* duration of green flash persistence */

/* ---- Activity chart state (dot plot) ---- */
var CHART_WINDOW_SEC = 300;        /* 5 minutes visible window */
var CHART_MAX_EVENTS = 2000;       /* max stored event dots */
var CHART_DB_MIN = -36;            /* Y-axis dB floor */
var CHART_DB_MAX = 0;              /* Y-axis dB ceiling */
var CHART_DOT_SIZE = 4;            /* dot size in CSS pixels */
var CHART_SESSION_GAP_MS = 2000;   /* gap threshold for session bands */
var CHART_TRIM_SLACK = 200;        /* over-fill before trimming (amortizes O(n) splice) */
var chartEvents = [];              /* [{ts, db, model, id, flagStart, flagEnd}] — oldest first */
var chartDirty = true;             /* set when chart data changes; cleared after render */
var devGroupsDirty = false;        /* set when device groups need re-sorting */
var chartTimerId = null;           /* periodic redraw timer */
var chartWindowSec = CHART_WINDOW_SEC; /* mutable — zoom changes this */
var chartPanOffsetMs = 0;  /* 0 = live (right edge = now); >0 = frozen, shifted back */
var chartDragging = false;
var chartDragStartX = 0;
var chartDragStartOffset = 0;
var CHART_ZOOM_MIN = 0.1;              /* minimum window: 100 milliseconds */
var CHART_ZOOM_MAX = 3600;             /* maximum window: 60 minutes */
var CHART_ZOOM_FACTOR = 1.25;          /* each wheel tick scales by ×1.25 */
var chartZoomSel = null;  /* {startX, startMs, canvas, wrap, currentX} */
var chartFrozenNowMs = 0; /* >0 = freeze chart time during interaction */
var chartLastNowMs = 0;   /* nowMs used in last render — freeze anchor */

/* Compute freeze time: in non-live mode, use last rendered time to avoid
   visual jumps when no renders happened between interactions. */
function chartFreezeNow() {
	return (chartPanOffsetMs > 0 && chartLastNowMs) ? chartLastNowMs : Date.now();
}

/* ---- Syslog state ---- */
var syslogEntries = [];
var SYSLOG_MAX = 200;
var syslogFilterStr = '';
var LOG_LEVELS = ['', 'FATAL', 'CRITICAL', 'ERROR', 'WARNING', 'NOTICE', 'INFO', 'DEBUG', 'TRACE'];
var LOG_CLASSES = ['', 'log-fatal', 'log-critical', 'log-error', 'log-warning', 'log-notice', 'log-info', 'log-debug', 'log-trace'];

/* ---- Debug tab state ---- */
var debugEnabled = false;    /* set from server meta web_ui_debug */
var dbgAutoTimer = null;     /* auto-refresh timer for perf dashboard */
var dbgRateTimer = null;     /* event rate simulator interval */

/* ---- Overlay panel state ---- */
var overlayPanels = {};      /* name → {el, btn, open, x, y, docked, dockedTab, dockSize, ...} */
var overlayDrag = null;      /* active drag: {name, startX, startY, origX, origY, pendingDockZone, dragDist} */
var activeTabName = 'monitor';
var splitterDrag = null;     /* {name, startPos, startSize, axis, side} */
var dockZoneThreshold = 40;
var OVERLAY_NAMES = ['help', 'settings', 'debug'];

/* ---- rAF event batching ---- */
var pendingEvents = [];
var rafId = 0;
var hiddenBatch = [];  /* events accumulated while document.hidden — flushed on visibility */

/* ---- Client performance metrics ---- */
var perfMetrics = {
	eventsReceived: 0,     /* total events received since connect */
	lastFlushMs: 0,        /* last flushEvents() duration */
	lastFlushBatch: 0,     /* last flushEvents() batch size */
	lastReRenderMonMs: 0,  /* last reRenderMonitorRows() duration */
	lastReRenderDevMs: 0,  /* last reRenderDeviceRows() duration */
	lastChartMs: 0,        /* last renderActivityChart() duration */
	flushCount: 0,         /* total rAF flush cycles */
	peakFlushMs: 0,        /* worst-case flush time */
	peakBatchSize: 0       /* largest batch processed */
};

/* ---- Per-model groups (Monitor + Devices) ---- */
/* Each group: { el, table, thead, theadTr, tbody, hdr, hdrModel, hdrCount,
                 dataKeys, dataKeysSet, rowCount, model } */
var monGroups = {};      /* model → group object */
var monGroupOrder = [];  /* ordered model names (most-recent-event first) */
var devGroups = {};      /* model → group object */
var devGroupOrder = [];  /* ordered model names */

/* ---- Monitor column sorting ---- */
var monSortCol = null;  /* null = arrival order, or 'time'|'model'|'id' */
var monSortAsc = true;

/* ---- Protocol column sorting ---- */
var protoSortCol = null;   var protoSortAsc = true;

/* ---- Syslog column sorting ---- */
var syslogSortCol = null;  var syslogSortAsc = true;

/* ---- Stats column sorting (persists across 10s refresh) ---- */
var statsSortState = {};   /* label → {col, asc} */

/* ---- Display filter state (synced with report meta checkboxes) ---- */
var META_SIGNAL_KEYS = {rssi:1, snr:1, noise:1, rssi_dB:1, snr_dB:1};
var META_PROTO_KEY = 'protocol';
var META_DESC_KEY = 'description';
var showMetaLevel = false;
var showMetaProto = false;
var showMetaDesc = false;
var showMetaHires = false; /* true when Hi-res time is toggled ON */
var META_BITS_KEY = 'bits';
var showMetaBits = false;   /* true when Verbose bits is toggled ON */

/* ---- Smart value formatting ---- */
var SKIP_KEYS = {model:1, time:1, id:1, type:1, mic:1};

/* ---- Row object pool ---- */
var rowPool = [];
var POOL_MAX = 128;

function allocRow() {
	if (rowPool.length > 0) return rowPool.pop();
	var tr = document.createElement('tr');
	/* Pre-create 2 fixed TDs (time, id) — data columns added by ensureCells */
	for (var i = 0; i < 2; i++) tr.appendChild(document.createElement('td'));
	return tr;
}

function freeRow(tr) {
	if (rowPool.length < POOL_MAX) {
		/* Clear contents for reuse */
		var cells = tr.childNodes;
		for (var i = 0; i < cells.length; i++) {
			cells[i].textContent = '';
			cells[i].style.color = '';
		}
		tr.className = '';
		tr._eventData = null;
		tr._detailRow = null;
		rowPool.push(tr);
	}
}

/* ---- DOM helpers ---- */
var $ = function (id) { return document.getElementById(id); };
function hasClass(e, c) { return e.classList.contains(c); }
function addClass(e, c) { e.classList.add(c); }
function removeClass(e, c) { e.classList.remove(c); }
function toggleClass(e, c, force) {
	if (force === undefined) e.classList.toggle(c);
	else if (force) e.classList.add(c);
	else e.classList.remove(c);
}
function mkEl(tag, cls, text) {
	var n = document.createElement(tag);
	if (cls) n.className = cls;
	if (text !== undefined) n.textContent = text;
	return n;
}
function prependChild(parent, node) {
	if (parent.firstChild) parent.insertBefore(node, parent.firstChild);
	else parent.appendChild(node);
}
function pluralize(n, s, p) { return n + ' ' + (n === 1 ? s : (p || s + 's')); }
function _identity(x) { return x; }
function _lastEvent(d) { return d.lastEvent; }
function bindAll(sel, evt, fn) {
	var a = document.querySelectorAll(sel);
	for (var i = 0; i < a.length; i++) a[i].addEventListener(evt, fn);
}

/* ---- DOM refs (cached once, never re-queried) ---- */
var elStatus   = $('hdr-status');
var elInfo     = $('hdr-info');
var elMonGroups = $('mon-groups');
var elMonCount = $('mon-count');
var elMonWrap  = $('mon-wrap');
var elAutoScrl = $('mon-autoscroll');
var elMonPause = $('mon-pause');
var elMonFilter = $('mon-filter');
var elMonRate  = $('mon-rate');
var elProtoSearch = $('proto-search');
var elProtoBody = $('proto-body');
var elProtoCount = $('proto-count');
var elProtoModCounts = $('proto-mod-counts');
var elStatsContent = $('stats-content');
var elSysDev   = $('sys-dev');
var elSysMeta  = $('sys-meta');
var elSysFreqs = $('sys-freqs');
var elSyslogBody   = $('syslog-body');
var elSyslogCount  = $('syslog-count');
var elSyslogSearch = $('syslog-search');
var elDevChart  = $('dev-chart');
var elDevChartWrap = $('dev-chart-wrap');
var elDevSearch = $('dev-search');
var elDevGroups = $('dev-groups');
var elDevCount  = $('dev-count');
var elDevListWrap    = $('dev-list-wrap');
var elDevZoomReset = $('dev-zoom-reset');
var elDevHistory = $('dev-history');
var elMonChart       = $('mon-chart');
var elMonChartWrap   = $('mon-chart-wrap');
var elMonChartScroll = $('mon-chart-scroll');
var elDevChartScroll = $('dev-chart-scroll');
var elDbgTabBtn = $('tab-btn-debug');
var elDbgCheckbox = $('s-debug-enable');
var elSettingsTabBtn = $('tab-btn-settings');
var elHelpTabBtn = $('tab-btn-help');
var elDockZoneHl = $('dock-zone-hl');

/* ---- Tab button / panel references ---- */
var tabBtns = document.querySelectorAll('.tab');
var panels = document.querySelectorAll('.panel');

/* ---- Settings guard state ---- */
var settingsGuard = {};  /* key → expiry timestamp */
var SETTINGS_GUARD_MS = 2000;

/* ---- Device render tracking ---- */
var devPrevKeys = null;  /* stringified sorted key order from last render */

/* ---- Chart resize drag state ---- */
var chartResizeDrag = null; /* {target, startY, startHeight, wrapEl} */

/* ---- Sort indicator constants ---- */
var SORT_ASC = '\u25b2';  /* ▲ */
var SORT_DESC = '\u25bc'; /* ▼ */

/* ---- Shared sort helpers ---- */

/* Update sort indicators on all th.sortable within a container.
   attr defaults to 'data-sort' (override with 'data-dev-sort' for devices). */
function updateSortIndicators(container, col, asc, attr) {
	attr = attr || 'data-sort';
	var ths = container.querySelectorAll('th.sortable');
	for (var i = 0; i < ths.length; i++) {
		var th = ths[i];
		var ind = th.querySelector('.sort-ind');
		if (th.getAttribute(attr) === col) {
			addClass(th, 'sort-active');
			if (ind) ind.textContent = asc ? SORT_ASC : SORT_DESC;
		} else {
			removeClass(th, 'sort-active');
			if (ind) ind.textContent = '';
		}
	}
}

/* 3-cycle sort state: asc → desc → reset.
   Returns {col, asc}. Default reset is {null, true}. */
function cycleSortState(curCol, curAsc, clicked, defCol, defAsc) {
	if (curCol === clicked) {
		if (curAsc) return {col: clicked, asc: false};
		return {col: defCol !== undefined ? defCol : null,
			asc: defAsc !== undefined ? defAsc : true};
	}
	return {col: clicked, asc: true};
}

/* Generic value comparator: handles numbers, strings, and mixed types.
   Returns negative/0/positive like standard compare functions.
   Pass asc=true for ascending, false for descending. */
function compareValues(a, b, asc) {
	var cmp;
	if (typeof a === 'number' && typeof b === 'number') {
		cmp = a - b;
	} else {
		a = String(a === undefined || a === null ? '' : a).toLowerCase();
		b = String(b === undefined || b === null ? '' : b).toLowerCase();
		cmp = a < b ? -1 : a > b ? 1 : 0;
	}
	return asc ? cmp : -cmp;
}
