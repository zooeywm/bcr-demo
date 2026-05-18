const overlayId = "__bcr_blank_overlay__";

function ensureBlankOverlay() {
  if (document.getElementById(overlayId)) {
    return;
  }

  const overlay = document.createElement("div");
  overlay.id = overlayId;
  overlay.style.position = "fixed";
  overlay.style.inset = "0";
  overlay.style.background = "#ffffff";
  overlay.style.zIndex = "2147483647";
  overlay.style.pointerEvents = "auto";
  overlay.style.cursor = "default";
  document.documentElement.appendChild(overlay);
}

function storageSnapshot() {
  return {
    type: "snapshot",
    url: window.location.href,
    localStorage: Object.fromEntries(
      Array.from({ length: window.localStorage.length }, (_, index) => {
        const key = window.localStorage.key(index);
        return [key, window.localStorage.getItem(key) ?? ""];
      }),
    ),
    sessionStorage: Object.fromEntries(
      Array.from({ length: window.sessionStorage.length }, (_, index) => {
        const key = window.sessionStorage.key(index);
        return [key, window.sessionStorage.getItem(key) ?? ""];
      }),
    ),
    userAgent: navigator.userAgent,
  };
}

async function pushSnapshot() {
  try {
    await chrome.runtime.sendMessage(storageSnapshot());
  } catch (error) {
  }
}

async function pollDesiredState() {
  try {
    const response = await chrome.runtime.sendMessage({ type: "desiredState" });
    if (!response?.active || !response.state?.desiredUrl) {
      return;
    }

    const desiredUrl = response.state.desiredUrl;
    if (desiredUrl && desiredUrl !== window.location.href) {
      window.location.replace(desiredUrl);
    }
  } catch (error) {
  }
}

ensureBlankOverlay();
setInterval(ensureBlankOverlay, 1000);
setInterval(pollDesiredState, 1000);

window.addEventListener("load", () => {
  ensureBlankOverlay();
  pushSnapshot();
});

window.addEventListener("pageshow", () => {
  ensureBlankOverlay();
  pushSnapshot();
});

document.addEventListener("visibilitychange", () => {
  if (!document.hidden) {
    pushSnapshot();
  }
});
