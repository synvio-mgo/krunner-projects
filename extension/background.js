// KRunner Projects – Chrome side.
//
// Keeps a native messaging port open to krunner_projects_host.py and executes
// the small set of commands it sends: list the open tab groups, focus one,
// restore a recently closed window or tab (Chrome re-links it to its saved
// group), or (re)create a saved group from its urls.

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
    case "recentlyClosed": {
      const sessions = await chrome.sessions.getRecentlyClosed({ maxResults: 25 });
      return {
        sessions: sessions.map((s) => ({
          lastModified: s.lastModified,
          window: s.window
            ? { sessionId: s.window.sessionId, tabs: (s.window.tabs || []).map((t) => ({ url: t.url, title: t.title, groupId: t.groupId })) }
            : undefined,
          tab: s.tab ? { sessionId: s.tab.sessionId, url: s.tab.url, title: s.tab.title, groupId: s.tab.groupId } : undefined,
        })),
      };
    }
    case "restore": {
      let s;
      try {
        s = await chrome.sessions.restore(args.sessionId);
      } catch (e) {
        // When the restored tabs belong to a saved group Chrome hands them to its
        // tab group code and then reports "No active tab." - the group is open anyway.
        if (/no active tab/i.test(String(e?.message || e))) return { restored: true, relinked: true };
        throw e;
      }
      const w = s.window ? s.window.id : s.tab ? s.tab.windowId : undefined;
      if (w !== undefined) await chrome.windows.update(w, { focused: true }).catch(() => {});
      return { restored: true, windowId: w };
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

async function createGroup(title, color, urls) {
  if (!urls || !urls.length) throw new Error("group has no tabs");
  // A saved group gets a window of its own, holding nothing but the group.
  const win = await chrome.windows.create({ url: urls, focused: true, state: "maximized" });
  const tabs = await chrome.tabs.query({ windowId: win.id });
  const tabIds = tabs.map((t) => t.id);
  const groupId = await chrome.tabs.group({ tabIds, createProperties: { windowId: win.id } });
  await chrome.tabGroups.update(groupId, { title, color, collapsed: false });
  if (tabIds.length) await chrome.tabs.update(tabIds[0], { active: true });
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
