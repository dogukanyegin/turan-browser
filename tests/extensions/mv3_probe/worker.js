// Test real asynchronous APIs and their results, not just property presence.
chrome.runtime.onMessage.addListener((message, sender, reply) => {
  if (message.kind !== 'probe') return;
  (async () => {
    const results = {};
    async function check(name, test) {
      try { results[name] = await test(); }
      catch (error) { results[name] = {error: String(error)}; }
    }
    await check('runtime', async () => chrome.runtime.getManifest().manifest_version === 3);
    await check('serviceWorker', async () => typeof ServiceWorkerGlobalScope !== 'undefined' && self instanceof ServiceWorkerGlobalScope);
    await check('storage', async () => {
      await chrome.storage.local.set({turanProbe: 71});
      return (await chrome.storage.local.get('turanProbe')).turanProbe === 71;
    });
    await check('tabs', async () => {
      const tabs = await chrome.tabs.query({});
      return tabs.some(tab => tab.id === sender.tab?.id && tab.url?.includes('/fixture'));
    });
    await check('windows', async () => {
      const windows = await chrome.windows.getAll({populate: true});
      return windows.some(window => window.tabs?.some(tab => tab.id === sender.tab?.id));
    });
    await check('scripting', async () => {
      const value = await chrome.scripting.executeScript({target: {tabId: sender.tab.id}, func: () => document.querySelector('#fixture').textContent});
      return value[0]?.result === 'Turan fixture';
    });
    await check('contextMenus', async () => {
      await new Promise((resolve, reject) => chrome.contextMenus.create({id: 'turan-probe', title: 'Turan test', contexts: ['page']}, () => chrome.runtime.lastError ? reject(chrome.runtime.lastError.message) : resolve()));
      await chrome.contextMenus.remove('turan-probe');
      return true;
    });
    await check('alarms', async () => {
      await chrome.alarms.create('turan-probe', {delayInMinutes: 1});
      const alarm = await chrome.alarms.get('turan-probe');
      await chrome.alarms.clear('turan-probe');
      return alarm?.name === 'turan-probe';
    });
    await check('notifications', async () => ['granted', 'denied'].includes(await chrome.notifications.getPermissionLevel()));
    await check('permissions', async () => await chrome.permissions.contains({permissions: ['storage', 'scripting']}));
    await check('webNavigation', async () => {
      const frames = await chrome.webNavigation.getAllFrames({tabId: sender.tab.id});
      return frames.some(frame => frame.frameId === 0 && frame.url.includes('/fixture'));
    });
    await check('declarativeNetRequest', async () => {
      await chrome.declarativeNetRequest.updateDynamicRules({removeRuleIds: [1], addRules: [{id: 1, priority: 1, action: {type: 'block'}, condition: {urlFilter: '/dnr-block', resourceTypes: ['xmlhttprequest']}}]});
      return (await chrome.declarativeNetRequest.getDynamicRules()).some(rule => rule.id === 1);
    });
    const topology = (await chrome.windows.getAll({populate: true})).map(window => ({id: window.id, type: window.type, tabs: (window.tabs || []).map(tab => ({id: tab.id, url: tab.url}))}));
    reply({results, topology, extensionId: chrome.runtime.id, tabId: sender.tab?.id});
  })();
  return true;
});
