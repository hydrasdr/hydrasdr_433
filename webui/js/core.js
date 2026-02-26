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
var MAX_DEVICES = 500;             /* max tracked devices before stale eviction */
var MAX_HIDDEN_BATCH = 5000;       /* max buffered events while tab hidden */
var MAX_PAUSED_EVENTS = 5000;      /* max buffered events while paused */
var RECONNECT_BASE_MS = 1000;
var RECONNECT_MAX_MS = 30000;
var STATS_INTERVAL_MS = 10000;
/* Modulation type helpers — r_device.h: values < 16 = OOK, >= 16 = FSK.
   Known coding subtypes are looked up; unknown future values get a
   sensible fallback like "OOK-7" or "FSK-19" so nothing breaks. */
var FSK_MOD_MIN = 16;
var MOD_CODING = {
	3: 'MC', 4: 'PCM', 5: 'PPM', 6: 'PWM', 8: 'PIWM',
	9: 'DMC', 10: 'PWM', 11: 'PIWM', 12: 'NRZS',
	16: 'PCM', 17: 'PWM', 18: 'MC'
};
function modName(idx) {
	if (!idx || idx <= 0) return '?';
	var base = idx >= FSK_MOD_MIN ? 'FSK' : 'OOK';
	var coding = MOD_CODING[idx];
	return coding ? base + '-' + coding : base + '-' + idx;
}
var RATE_WINDOW_MS = 5000;
/* Toast */
var TOAST_ANIM_MS = 300;           /* fade-out duration before DOM removal */
/* Connection */
var RATE_DISPLAY_MS = 500;         /* rate counter update interval */
var RPC_COMPACT_THRESH = 16;       /* compact RPC queue when head exceeds this */
var INIT_QUERY_DELAY_MS = 150;     /* delay before initial RPC queries on connect */
/* Overlay / layout */
var OVERLAY_Z_BASE = 100;
var OVERLAY_Z_TOP = 101;
var OVERLAY_INIT_Y = 80;           /* default floating panel Y offset */
var DOCK_DEFAULT_SIZE = 400;       /* default dock panel width/height */
var DOCK_MIN_SIZE = 200;
var DOCK_MAX_RATIO = 0.6;          /* max fraction of parent for dock panel */
var UNDOCK_DRAG_PX = 20;           /* drag distance to undock */
var CHART_MIN_H = 60;              /* chart resize clamp min height */
var CHART_MAX_H = 600;             /* chart resize clamp max height */
var MOBILE_BP_PX = 700;            /* mobile breakpoint — auto-undock below this */
/* Chart rendering */
var CHART_MARGIN_LEFT = 30;
var CHART_MARGIN_BOTTOM = 14;
var CHART_MARGIN_TOP = 4;
var CHART_MARGIN_RIGHT = 4;
var CHART_DB_STEP = 6;             /* Y-axis dB grid step */
var CHART_LABEL_SPACING_PX = 80;   /* min pixels between X-axis labels */
var CHART_REDRAW_MS = 1000;        /* periodic chart redraw interval */
var CHART_SCROLL_MAX = 1000;       /* scrollbar max value */
var CHART_SCROLL_SNAP = 998;       /* scrollbar value to snap to live */
var CHART_EST_EVENTS_PER_SEC = 7;  /* estimated event rate for history sizing */
var CHART_MIN_ZOOM_SEL_PX = 5;    /* min pixel width for zoom selection */
/* Signal bar */
var SIG_DB_OFFSET = 30;            /* dB offset for negative signal mapping */
var SIG_DB_RANGE = 29;             /* dB range divisor for percentage calc */
var SIG_THRESH_HI = 60;            /* % threshold for high signal class */
var SIG_THRESH_MID = 30;           /* % threshold for mid signal class */
var SIG_BAR_MIN_PX = 4;            /* minimum signal bar width */
/* Device flash */
var DEV_FLASH_DELAY_MS = 50;       /* delay before starting CSS transition */
/* Frequency input */
var FREQ_MHZ_THRESH = 10000;       /* below this, assume MHz (e.g. "433.92") */
/* Protocol UI */
var PROTO_SEARCH_DEBOUNCE_MS = 150;
var PROTO_BATCH_STAGGER_MS = 10;   /* ms between batch enable/disable RPCs */
/* Debug defaults */
var DBG_RATE_DEFAULT = 100;        /* default rate sim: events per second */
var DBG_RATE_DUR = 10;             /* default rate sim: duration in seconds */
var DBG_RATE_TICK_MS = 20;         /* rate sim timer interval */
var DBG_BULK_DEFAULT = 1000;       /* default bulk inject count */
var DBG_MODELS_DEFAULT = 5;        /* default bulk inject model count */
var DBG_POP_DEFAULT = 3;           /* default populate events per model */

/* ---- State ---- */
var ws = null;
var reconnectDelay = RECONNECT_BASE_MS;
var reconnectCount = 0;
var eventCount = 0;
var rpcQueue = [];
var rpcQueueHead = 0;  /* index-based dequeue: avoids O(n) shift */
var metaCache = null;
var hdrGainStr = '';       /* "Auto", "Linearity 15", etc. */
var hdrBiasTee = false;
var hdrHopInterval = 0;    /* 0 = no hopping */
var gainRanges = {         /* from device_info, populated on connect */
	linearity:   {min: 0, max: 0, step: 1, def: 0},
	sensitivity: {min: 0, max: 0, step: 1, def: 0}
};
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
var devSortCol = null;     /* null = default order (seen desc); set by column click */
var devSortAsc = true;
var devDetailKey = null;
var MAX_DEVICE_EVENTS = 200;
var DEVICE_DETAIL_PAGE = 20;
var devicesTabActive = false;
var devRafId = 0;
var devActiveKeys = {};    /* key → timestamp of last update, for green flash */
var DEV_FLASH_MS = 3000;   /* duration of green flash persistence */
var DEV_ACTIVE_PURGE = 500; /* purge stale flash entries when map exceeds this */

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
	/* Event pipeline */
	eventsReceived: 0,     /* total events received since connect */
	lastFlushMs: 0,        /* last flushEvents() duration */
	lastFlushBatch: 0,     /* last flushEvents() batch size */
	flushCount: 0,         /* total rAF flush cycles */
	peakFlushMs: 0,        /* worst-case flush time */
	peakBatchSize: 0,      /* largest batch processed */
	peakEventRate: 0,      /* highest evt/s recorded */
	/* Render timing */
	lastReRenderMonMs: 0,  /* last reRenderMonitorRows() duration */
	lastReRenderDevMs: 0,  /* last reRenderDeviceRows() duration */
	lastChartMs: 0,        /* last renderActivityChart() duration */
	lastProtocolsMs: 0,    /* last renderProtocols() duration */
	lastSyslogMs: 0,       /* last rebuildSyslogTable() duration */
	lastDevRegistryMs: 0,  /* last updateDeviceRegistry() cumulative per flush */
	/* WebSocket / RPC */
	lastMsgParseMs: 0,     /* last JSON.parse() time */
	peakMsgParseMs: 0,     /* worst-case parse time */
	rpcQueueDepth: 0,      /* current pending RPC callbacks */
	peakRpcQueueDepth: 0,  /* peak RPC queue depth */
	wsReconnects: 0,       /* total reconnection count */
	/* Row pool */
	rowPoolHits: 0,        /* allocations served from pool */
	rowPoolMisses: 0,      /* allocations requiring createElement */
	/* DOM */
	domNodeCount: 0,       /* current DOM element count (updated on stats refresh) */
	/* Hidden tab */
	hiddenBatchMax: 0,     /* largest hiddenBatch size */
	hiddenFlushes: 0,      /* number of hidden→visible flushes */
	/* UI */
	toastsShown: 0,        /* total toast notifications created */
	fillDataCellsMs: 0     /* cumulative fillDataCells() time in last flush */
};

/* ---- Per-model groups (Monitor + Devices) ---- */
/* Each group: { el, table, thead, theadTr, tbody, hdr, hdrModel, hdrCount,
                 dataKeys, dataKeysSet, rowCount (monitor only), model } */
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
	if (rowPool.length > 0) {
		perfMetrics.rowPoolHits++;
		return rowPool.pop();
	}
	perfMetrics.rowPoolMisses++;
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

/* ---- Display-change notification ---- */
var displayChangeListeners = [];
function onDisplayChange(fn) { displayChangeListeners.push(fn); }
function notifyDisplayChange() {
	for (var i = 0; i < displayChangeListeners.length; i++)
		displayChangeListeners[i]();
}

/* ---- Toast notifications ---- */
var TOAST_MAX = 5;
var TOAST_DUR_OK = 4000;
var TOAST_DUR_ERR = 6000;
var TOAST_DUR_WARN = 5000;
var TOAST_ICONS = {ok: '\u2713', err: '\u2717', warn: '\u26a0'};  /* ✓ ✗ ⚠ */
var toastContainer = null;  /* cached on first use */

function showToast(message, type) {
	perfMetrics.toastsShown++;
	if (!toastContainer) toastContainer = $('toast-container');
	if (!toastContainer) return;

	/* Enforce stack limit — remove oldest */
	while (toastContainer.childNodes.length >= TOAST_MAX) {
		toastContainer.removeChild(toastContainer.lastChild);
	}

	var t = type || 'ok';
	var dur = t === 'err' ? TOAST_DUR_ERR : t === 'warn' ? TOAST_DUR_WARN : TOAST_DUR_OK;
	var el = mkEl('div', 'toast toast-' + t + ' toast-enter');
	var icon = mkEl('span', 'toast-icon', TOAST_ICONS[t] || '');
	var msg = mkEl('span', '', message);
	el.appendChild(icon);
	el.appendChild(msg);

	/* Click to dismiss */
	el.addEventListener('click', function () { dismissToast(el); });

	toastContainer.insertBefore(el, toastContainer.firstChild);

	/* Trigger enter animation on next frame */
	requestAnimationFrame(function () {
		removeClass(el, 'toast-enter');
	});

	/* Auto-dismiss */
	el._toastTimer = setTimeout(function () { dismissToast(el); }, dur);
}

function dismissToast(el) {
	if (el._toastDismissed) return;
	el._toastDismissed = true;
	if (el._toastTimer) clearTimeout(el._toastTimer);
	addClass(el, 'toast-exit');
	setTimeout(function () {
		if (el.parentNode) el.parentNode.removeChild(el);
	}, TOAST_ANIM_MS);
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
var elDevEvtCount = $('dev-evt-count');
var elDevRate   = $('dev-rate');
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
