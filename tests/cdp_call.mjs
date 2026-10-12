// Transport for disposable local CEF test targets only. Never connects remotely.
// Failure stages are classified so a caller NEVER replays a command after send.
let input = '';
for await (const chunk of process.stdin) input += chunk;
let socket;
let timer;
let stage = 'connect';
function failure(code, message) {
  const err = new Error(message);
  err.code = code;
  return err;
}
function reason(event) {
  return event?.error?.message || event?.message || 'CDP WebSocket unavailable';
}
try {
  const call = JSON.parse(input);
  if (!/^ws:\/\/127\.0\.0\.1:\d+\/devtools\//.test(call.url)) {
    throw failure('CDP_INVALID_TARGET', 'Local 127.0.0.1 DevTools URL required');
  }
  if (typeof call.method !== 'string' || !/^[A-Za-z]+\.[A-Za-z]+$/.test(call.method)) {
    throw failure('CDP_INVALID_METHOD', 'Invalid DevTools method');
  }
  socket = new WebSocket(call.url);
  await new Promise((resolve, reject) => {
    timer = setTimeout(() => reject(failure('CDP_CONNECT_TIMEOUT', 'DevTools connect timeout')), 15000);
    socket.addEventListener('open', resolve, {once: true});
    socket.addEventListener('error', (event) => reject(failure('CDP_CONNECT_FAILED', reason(event))), {once: true});
    socket.addEventListener('close', () => reject(failure('CDP_CONNECT_FAILED', 'Target closed during connect')), {once: true});
  });
  clearTimeout(timer);
  stage = 'sent';
  const result = await new Promise((resolve, reject) => {
    timer = setTimeout(() => reject(failure('CDP_RESPONSE_TIMEOUT', 'DevTools reply timeout after send')), 15000);
    socket.addEventListener('message', event => {
      let message;
      try { message = JSON.parse(event.data); }
      catch (error) { reject(failure('CDP_INVALID_RESPONSE', error.message)); return; }
      if (message.id !== 1) return;
      if (message.error) reject(failure('CDP_PROTOCOL_ERROR', JSON.stringify(message.error)));
      else resolve(message.result);
    });
    socket.addEventListener('error', event => reject(failure('CDP_RESPONSE_FAILED', reason(event))), {once: true});
    socket.addEventListener('close', () => reject(failure('CDP_RESPONSE_CLOSED', 'Target closed after command send')), {once: true});
    try { socket.send(JSON.stringify({id: 1, method: call.method, params: call.params || {}})); }
    catch (error) { reject(failure('CDP_RESPONSE_FAILED', error.message)); }
  });
  clearTimeout(timer);
  process.stdout.write(JSON.stringify(result));
} catch (error) {
  const code = error.code || (stage === 'connect' ? 'CDP_CONNECT_FAILED' : 'CDP_RESPONSE_FAILED');
  process.stderr.write(code + ' ' + (error.message || String(error)) + '\n');
  // Only this exit code is eligible for bounded, target-identity-checked retry.
  process.exitCode = code === 'CDP_CONNECT_FAILED' ? 31 : 32;
} finally {
  clearTimeout(timer);
  if (socket) socket.close();
}
