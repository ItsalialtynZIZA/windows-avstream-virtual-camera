"use strict";

// Dynamically install MAIN-world document_start scripts only for domains
// the user has explicitly approved. Extension matches ignore TCP ports.
const SCRIPT_ID = "avcam-chrome-route-v1";
let synchronization = Promise.resolve();

function queueReconcile() {
  synchronization = synchronization.catch(() => {}).then(reconcile);
  return synchronization;
}

async function reconcile() {
  const {patterns = []} = await chrome.storage.local.get({patterns: []});
  const authorized = [];
  for (const pattern of [...new Set(patterns)]) {
    if (typeof pattern !== "string" ||
        !/^https?:\/\/[a-z0-9.[\]-]+\/\*$/i.test(pattern)) continue;
    if (await chrome.permissions.contains({origins: [pattern]})) {
      authorized.push(pattern);
    }
  }

  const registrations = await chrome.scripting.getRegisteredContentScripts({
    ids: [SCRIPT_ID]
  });
  const desired = authorized.slice().sort();
  if (registrations.length) {
    const existing = (registrations[0].matches || []).slice().sort();
    if (JSON.stringify(existing) === JSON.stringify(desired)) return;
    await chrome.scripting.unregisterContentScripts({ids: [SCRIPT_ID]});
  }
  if (desired.length) {
    await chrome.scripting.registerContentScripts([{
      id: SCRIPT_ID,
      matches: desired,
      js: ["bridge.js"],
      runAt: "document_start",
      world: "MAIN",
      allFrames: true,
      persistAcrossSessions: true
    }]);
  }
}

chrome.runtime.onInstalled.addListener(() => { void queueReconcile(); });
chrome.runtime.onStartup.addListener(() => { void queueReconcile(); });
chrome.permissions.onRemoved.addListener(() => { void queueReconcile(); });
chrome.runtime.onMessage.addListener((message, sender, respond) => {
  if (message?.type !== "AVCAM_RECONCILE") return;
  void queueReconcile()
    .then(() => respond({ok: true}))
    .catch((error) => respond({ok: false, error: String(error)}));
  return true;
});
