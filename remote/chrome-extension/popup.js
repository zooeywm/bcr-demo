const agentEndpoint = "http://127.0.0.1:45455/command";

async function getActiveTab() {
  const [tab] = await chrome.tabs.query({ active: true, currentWindow: true });
  return tab;
}

async function sendAgentRequest(message) {
  let response;
  try {
    response = await fetch(agentEndpoint, {
      method: "POST",
      body: JSON.stringify(message),
    });
  } catch (error) {
    throw new Error("无法连接本地 bcr-agent，请先手动运行它");
  }

  let payload;
  try {
    payload = await response.json();
  } catch (error) {
    throw new Error("bcr-agent 返回了无效响应");
  }

  if (!response.ok) {
    throw new Error(payload?.error || "bcr-agent 请求失败");
  }

  return payload;
}

function renderStatus(text) {
  document.getElementById("status").textContent = text;
}

function renderTab(tab) {
  const element = document.getElementById("tab-url");
  if (!tab || !tab.url) {
    element.textContent = "当前标签页没有可发送的 URL";
    return;
  }
  element.textContent = tab.url;
}

async function sendAction(action, withUrl = false) {
  const tab = await getActiveTab();
  if (withUrl && (!tab || !tab.url || !/^https?:/i.test(tab.url))) {
    throw new Error("当前标签页不是 http/https 页面");
  }

  const payload = { action };
  if (withUrl) {
    payload.url = tab.url;
  }

  const response = await sendAgentRequest(payload);
  renderStatus(JSON.stringify(response, null, 2));
}

async function init() {
  const tab = await getActiveTab();
  renderTab(tab);

  document.getElementById("open-url").addEventListener("click", async () => {
    try {
      await sendAction("openUrl", true);
    } catch (error) {
      renderStatus(String(error.message || error));
    }
  });

  document.getElementById("open-fullscreen").addEventListener("click", async () => {
    try {
      await sendAction("openUrlAndFullscreen", true);
    } catch (error) {
      renderStatus(String(error.message || error));
    }
  });

  document.getElementById("enter-fullscreen").addEventListener("click", async () => {
    try {
      await sendAction("enterFullscreen");
    } catch (error) {
      renderStatus(String(error.message || error));
    }
  });

  document.getElementById("exit-fullscreen").addEventListener("click", async () => {
    try {
      await sendAction("exitFullscreen");
    } catch (error) {
      renderStatus(String(error.message || error));
    }
  });

  document.getElementById("ping").addEventListener("click", async () => {
    try {
      await sendAction("ping");
    } catch (error) {
      renderStatus(String(error.message || error));
    }
  });
}

init().catch((error) => {
  renderStatus(String(error.message || error));
});
