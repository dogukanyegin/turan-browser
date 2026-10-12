"""Reproduce a pinned official EasyList/EasyPrivacy corpus compile; QA only."""
import hashlib
import json
from pathlib import Path
import subprocess
import urllib.request
ROOT=Path(__file__).resolve().parents[1]

def main():
    lock=json.loads((ROOT/'tests/filter.corpus.lock.json').read_text());folder=ROOT/'.deps/filter-corpus';folder.mkdir(exist_ok=True)
    files=[]
    for name,item in lock['lists'].items():
        path=folder/(name+'.txt')
        if not path.exists():
            with urllib.request.urlopen(item['url'],timeout=40) as response:data=response.read(item['size']+1)
            if len(data)!=item['size'] or hashlib.sha256(data).hexdigest()!=item['sha256']:raise RuntimeError('Corpus download mismatch')
            path.write_bytes(data)
        if hashlib.sha256(path.read_bytes()).hexdigest()!=item['sha256']:raise RuntimeError('Corpus cache checksum mismatch')
        files.append(path)
    result=subprocess.run([ROOT/'out-cef/Release/turan_filter_corpus.exe',ROOT/'resources/public_suffix_list.dat',*files],capture_output=True,text=True,encoding='utf-8')
    proof={'scope':'Compilation and synthetic request timing only; partial ABP syntax, no publisher list enabled in application','sources':lock,'exitCode':result.returncode,'diagnostics':[]}
    for line in result.stdout.splitlines():
        name,_,value=line.partition('=')
        if name=='diagnostic':proof['diagnostics'].append(value)
        else:proof[name]=float(value) if name.endswith(('Ms','Us','MiB')) else int(value) if value.isdigit() else value
    proof['stderr']=result.stderr
    (ROOT/'artifacts/filter-corpus.json').write_text(json.dumps(proof,indent=2)+'\n')
    print(json.dumps({k:v for k,v in proof.items() if k not in ('sources','diagnostics')},indent=2))
    if result.returncode:raise RuntimeError('Corpus compilation failed; retained artifacts/filter-corpus.json')
    if proof.get('blockedSamples')!=500 or proof.get('sampleRequests')!=1000:raise RuntimeError('Corpus positive/negative controls failed')

if __name__=='__main__':main()
