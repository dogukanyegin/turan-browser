(async () => {
  try {
    const report = await chrome.runtime.sendMessage({kind: 'probe'});
    try { await fetch('/dnr-block'); report.results.dnrEnforcement = false; }
    catch { report.results.dnrEnforcement = true; }
    await fetch('/report', {method: 'POST', headers: {'Content-Type': 'application/json'}, body: JSON.stringify(report)});
    document.title = 'MV3_PROBE_REPORTED';
  } catch (error) {
    await fetch('/report', {method: 'POST', body: JSON.stringify({error: String(error)})});
  }
})();
