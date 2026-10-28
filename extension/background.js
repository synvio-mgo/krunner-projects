// KRunner Projects – Chrome side.
//
// Keeps a native messaging port open to krunner_projects_host.py and executes
// the small set of commands it sends: list the open tab groups, focus one, or
// (re)create a saved group from its urls.

const HOST = "be.synvio.krunner_projects";
let port = null;
let retryMs = 1000;

let reconnectTimer = null;

function connect() {
  if (port) return; // one native host at a time; a second one would just exit again
  let p;
  try {
    p = chrome.runtime.connectNative(HOST);
  } catch (e) {
    console.warn("connectNative failed", e);
    scheduleReconnect();
    return;
  }
  port = p;
  p.onMessage.addListener(onHostMessage);
  p.onDisconnect.addListener(() => {
    console.warn("native host disconnected", chrome.runtime.lastError?.message);
    if (port === p) {
      port = null;
      scheduleReconnect();
    }
  });
  p.postMessage({ hello: "krunner-projects", version: chrome.runtime.getManifest().version });
  retryMs = 1000;
}

function scheduleReconnect() {
  if (reconnectTimer) return;
  reconnectTimer = setTimeout(() => {
    reconnectTimer = null;
    connect();
  }, retryMs);
  retryMs = Math.min(retryMs * 2, 30000);
}

async function onHostMessage(msg) {
  const { id, cmd, args = {} } = msg;
  let result;
  try {
    result = await handle(cmd, args);
    result.ok = result.ok ?? true;
  } catch (e) {
    result = { ok: false, error: String(e?.message || e) };
  }
  port?.postMessage({ id, result });
}

async function handle(cmd, args) {
  switch (cmd) {
    case "listOpen":
      return { groups: await listOpenGroups() };
    case "focus":
      return focusGroup(args.groupId, args.windowId);
    case "create":
      return createGroup(args.title, args.color, args.urls);
    case "openWindow": {
      const w = await chrome.windows.create({ url: args.urls, focused: true });
      return { windowId: w.id };
    }
    default:
      throw new Error(`unknown command ${cmd}`);
  }
}

async function listOpenGroups() {
  const groups = await chrome.tabGroups.query({});
  const out = [];
  for (const g of groups) {
    const tabs = await chrome.tabs.query({ groupId: g.id });
    out.push({
      id: g.id,
      windowId: g.windowId,
      title: g.title || "",
      color: g.color,
      collapsed: g.collapsed,
      tabs: tabs.map((t) => ({ id: t.id, url: t.url, title: t.title })),
    });
  }
  return out;
}

async function focusGroup(groupId, windowId) {
  await chrome.windows.update(windowId, { focused: true, state: "normal" }).catch(() => {});
  await chrome.tabGroups.update(groupId, { collapsed: false }).catch(() => {});
  const tabs = await chrome.tabs.query({ groupId });
  const active = tabs.find((t) => t.active) || tabs[0];
  if (active) await chrome.tabs.update(active.id, { active: true });
  return { groupId, windowId, focused: true };
}

async function targetWindow() {
  // Prefer the last focused normal window; otherwise make one.
  const wins = await chrome.windows.getAll({ windowTypes: ["normal"] });
  if (!wins.length) {
    return chrome.windows.create({ focused: true });
  }
  try {
    const w = await chrome.windows.getLastFocused({ windowTypes: ["normal"] });
    if (w) return w;
  } catch (e) {
    /* fall through */
  }
  return wins[0];
}

async function createGroup(title, color, urls) {
  if (!urls || !urls.length) throw new Error("group has no tabs");
  const win = await targetWindow();
  const tabIds = [];
  for (const [i, url] of urls.entries()) {
    const tab = await chrome.tabs.create({ windowId: win.id, url, active: i === 0 });
    tabIds.push(tab.id);
  }
  const groupId = await chrome.tabs.group({ tabIds, createProperties: { windowId: win.id } });
  await chrome.tabGroups.update(groupId, { title, color, collapsed: false });
  await chrome.windows.update(win.id, { focused: true });
  return { groupId, windowId: win.id, created: true, tabs: tabIds.length };
}

chrome.runtime.onStartup.addListener(connect);
chrome.runtime.onInstalled.addListener(connect);
// Keep the service worker alive while the native port is open; also reconnect
// if the worker was restarted for any reason.
chrome.alarms.create("krunner-projects-keepalive", { periodInMinutes: 0.5 });
chrome.alarms.onAlarm.addListener(() => {
  if (!port) connect();
});
connect();
