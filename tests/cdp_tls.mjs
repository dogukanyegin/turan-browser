// Observe certificate rejection in an explicitly launched local test browser.
let input='';for await(const chunk of process.stdin)input+=chunk;
const call=JSON.parse(input);
if(!/^ws:\/\/127\.0\.0\.1:\d+\/devtools\//.test(call.url)||!/^https:\/\/127\.0\.0\.1:\d+\/tls-fixture$/.test(call.destination))throw Error('Local TLS fixture required');
const socket=new WebSocket(call.url);
const timer=setTimeout(()=>{socket.close();process.stderr.write('TLS event timeout\n');process.exit(1)},15000);
await new Promise((resolve,reject)=>{socket.addEventListener('open',resolve,{once:true});socket.addEventListener('error',reject,{once:true})});
const pending=new Map();let next=1;let request;
let rejectEvent;
const rejection=new Promise(resolve=>rejectEvent=resolve);
socket.addEventListener('message',event=>{
 const message=JSON.parse(event.data);
 if(message.id&&pending.has(message.id)){pending.get(message.id)(message);pending.delete(message.id)}
 if(message.method==='Network.requestWillBeSent'&&message.params.request.url===call.destination)request=message.params.requestId;
 if(message.method==='Network.loadingFailed'&&message.params.requestId===request)rejectEvent(message.params);
});
const send=(method,params={})=>new Promise((resolve,reject)=>{const id=next++;pending.set(id,m=>m.error?reject(Error(JSON.stringify(m.error))):resolve(m.result));socket.send(JSON.stringify({id,method,params}))});
await send('Network.enable');const navigation=await send('Page.navigate',{url:call.destination});
const failure=await rejection;clearTimeout(timer);socket.close();
process.stdout.write(JSON.stringify({navigation,failure}));
