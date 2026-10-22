"use strict";

const host = document.getElementById("host");
const status = document.getElementById("status");
const error = document.getElementById("error");
const enable = document.getElementById("enable");
const disable = document.getElementById("disable");
let tab;
let pattern;

function patternFor(urlText) {
  const url = new URL(urlText);
  if (!["https:", "http:"].includes(url.protocol)) {
    throw new Error("Только обычные HTTP/HTTPS сайты. Chrome settings не поддерживаются.");
  }
  // Chrome host match patterns are domain-scoped and ignore ports.
  if (!/^[a-z0-9.-]+$/i.test(url.hostname)) {
    throw new Error("Этот адрес сайта не поддерживается.");
  }
  return url.protocol + "//" + url.hostname + "/*";
}

async function showState() {
  const {patterns = []} = await chrome.storage.local.get({patterns: []});
  const enabled = patterns.includes(pattern) &&
    await chrome.permissions.contains({origins: [pattern]});
  status.textContent = enabled ? "Включено на этом домене" : "Отключено";
  enable.disabled = enabled;
  disable.disabled = !enabled;
}

async function syncAndReload() {
  const response = await chrome.runtime.sendMessage({type: "AVCAM_RECONCILE"});
  if (!response?.ok) throw new Error(response?.error || "Не удалось обновить расширение");
  await chrome.tabs.reload(tab.id);
}

async function init() {
  try {
    const tabs = await chrome.tabs.query({active: true, currentWindow: true});
    tab = tabs[0];
    if (!tab?.url) throw new Error("Нет доступа к текущей вкладке");
    pattern = patternFor(tab.url);
    host.textContent = new URL(tab.url).hostname;
    await showState();
  } catch (e) {
    host.textContent = "Недоступен";
    status.textContent = "Нельзя включить";
    error.textContent = String(e.message || e);
  }
}

enable.addEventListener("click", async () => {
  enable.disabled = true;
  error.textContent = "";
  try {
    // The permission prompt is directly associated with this click.
    const granted = await chrome.permissions.request({origins: [pattern]});
    if (!granted) throw new Error("Пользователь отклонил доступ к домену.");
    const {patterns = []} = await chrome.storage.local.get({patterns: []});
    await chrome.storage.local.set({patterns: [...new Set([...patterns, pattern])]});
    await syncAndReload();
    await showState();
  } catch (e) {
    error.textContent = String(e.message || e);
    await showState();
  }
});
disable.addEventListener("click", async () => {
  disable.disabled = true;
  error.textContent = "";
  try {
    const {patterns = []} = await chrome.storage.local.get({patterns: []});
    await chrome.storage.local.set({patterns: patterns.filter((x) => x !== pattern)});
    await syncAndReload();
    // Site permission is optional; release it after the script is unregistered.
    await chrome.permissions.remove({origins: [pattern]});
    await showState();
  } catch (e) {
    error.textContent = String(e.message || e);
    await showState();
  }
});
void init();
