const agentBaseUrl = "http://127.0.0.1:45455";

let activeTabId = null;
const tabSnapshots = new Map();

async function postJson(path, payload) {
  const response = await fetch(`${agentBaseUrl}${path}`, {
    method: "POST",
    headers: {
      "Content-Type": "application/json",
    },
    body: JSON.stringify(payload),
  });

  if (!response.ok) {
    throw new Error(`agent request failed: ${response.status}`);
  }

  return response.json().catch(() => ({}));
}

async function fetchDesiredState() {
  const response = await fetch(`${agentBaseUrl}/desired-state`);
  if (!response.ok) {
    return null;
  }

  return response.json().catch(() => null);
}

async function queryTabs() {
  return chrome.tabs.query({ currentWindow: true });
}

async function currentActiveTab() {
  const [tab] = await chrome.tabs.query({ active: true, currentWindow: true });
  return tab ?? null;
}

function serializeTabs(tabs) {
  return tabs.map((tab) => ({
    id: tab.id,
    url: tab.url ?? "",
    title: tab.title ?? "",
    active: Boolean(tab.active),
  }));
}

async function buildAuthSnapshot(tab) {
  if (!tab?.id || !tab?.url || !/^https?:/i.test(tab.url)) {
    return null;
  }

  const cookies = await chrome.cookies.getAll({ url: tab.url }).catch(() => []);
  const snapshot = tabSnapshots.get(tab.id) ?? {
    localStorage: {},
    sessionStorage: {},
    userAgent: "",
  };

  return {
    cookies: cookies.map((cookie) => ({
      name: cookie.name,
      value: cookie.value,
      domain: cookie.domain,
      path: cookie.path,
      secure: cookie.secure,
      httpOnly: cookie.httpOnly,
      sameSite: cookie.sameSite ?? "unspecified",
      expirationDate: cookie.expirationDate ?? null,
    })),
    localStorage: snapshot.localStorage ?? {},
    sessionStorage: snapshot.sessionStorage ?? {},
    userAgent: snapshot.userAgent ?? "",
  };
}

async function syncBrowserState(reason = "event") {
  const [tabs, activeTab] = await Promise.all([queryTabs(), currentActiveTab()]);
  if (!activeTab) {
    return;
  }

  activeTabId = activeTab.id ?? null;
  const auth = await buildAuthSnapshot(activeTab);
  await postJson("/extension/state", {
    reason,
    activeTab: {
      id: activeTab.id,
      url: activeTab.url ?? "",
      title: activeTab.title ?? "",
      active: true,
      auth,
    },
    tabs: serializeTabs(tabs),
    timestamp: Date.now(),
  });
}

chrome.runtime.onInstalled.addListener(() => {
  syncBrowserState("installed").catch(() => {});
});

chrome.runtime.onStartup.addListener(() => {
  syncBrowserState("startup").catch(() => {});
});

chrome.tabs.onActivated.addListener(({ tabId }) => {
  activeTabId = tabId;
  syncBrowserState("activated").catch(() => {});
});

chrome.tabs.onUpdated.addListener((tabId, changeInfo) => {
  if (changeInfo.status === "complete" || changeInfo.url || changeInfo.title) {
    syncBrowserState("updated").catch(() => {});
  }
});

chrome.tabs.onRemoved.addListener((tabId) => {
  tabSnapshots.delete(tabId);
  syncBrowserState("removed").catch(() => {});
});

chrome.runtime.onMessage.addListener((message, sender, sendResponse) => {
  if (message?.type === "snapshot" && sender.tab?.id) {
    tabSnapshots.set(sender.tab.id, {
      localStorage: message.localStorage ?? {},
      sessionStorage: message.sessionStorage ?? {},
      userAgent: message.userAgent ?? "",
      url: message.url ?? "",
    });
    syncBrowserState("snapshot").catch(() => {});
    sendResponse({ ok: true });
    return true;
  }

  if (message?.type === "isActiveTab") {
    sendResponse({ active: sender.tab?.id === activeTabId });
    return false;
  }

  if (message?.type === "desiredState") {
    fetchDesiredState()
      .then((state) => sendResponse({ active: sender.tab?.id === activeTabId, state }))
      .catch(() => sendResponse({ active: sender.tab?.id === activeTabId, state: null }));
    return true;
  }

  return false;
});
