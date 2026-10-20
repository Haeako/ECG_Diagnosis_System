"""Download a pinned public BUTQDB snapshot, resumably and with hash checks."""
import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import time
from urllib.parse import quote

REPO='epr-labs/butqdb'
BASE=f'https://huggingface.co/datasets/{REPO}'

def api(url):
    return json.loads(subprocess.check_output(['curl.exe','-fsSL','--retry','3',
                      '--connect-timeout','20','--max-time','120',url],text=True))

def digest(path,git=False):
    h=hashlib.sha1() if git else hashlib.sha256()
    if git:h.update(f'blob {path.stat().st_size}\0'.encode())
    with path.open('rb') as f:
        for block in iter(lambda:f.read(8*1024*1024),b''):h.update(block)
    return h.hexdigest()

def main():
    p=argparse.ArgumentParser()
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--workers',type=int,default=3)
    args=p.parse_args();out=args.output.resolve()
    meta=api(f'https://huggingface.co/api/datasets/{REPO}/revision/main')
    rev=meta['sha']
    tree=api(f'https://huggingface.co/api/datasets/{REPO}/tree/{rev}?recursive=true&expand=false&limit=1000')
    files=[f for f in tree if f['type']=='file']
    assert {f['path'] for f in files}=={f['rfilename'] for f in meta['siblings']},'Incomplete tree'
    total=sum(f['size'] for f in files)
    assert shutil.disk_usage(out.parent).free>total+512*1024*1024,'Insufficient disk space'
    out.mkdir(parents=True,exist_ok=True)
    (out/'hf_source.json').write_text(json.dumps(dict(repository=REPO,revision=rev,total_bytes=total,
        files=files,metadata=meta),indent=2),encoding='utf-8')
    print(f'Snapshot {rev}: {len(files)} files, {total:,} bytes ({total/1024**3:.3f} GiB)',flush=True)
    def download(item):
        rel=item['path'];dest=(out/rel).resolve()
        assert dest.is_relative_to(out)
        dest.parent.mkdir(parents=True,exist_ok=True)
        git='lfs' not in item
        expected=item.get('lfs',{}).get('oid',item['oid'])
        def valid(path):return path.stat().st_size==item['size'] and digest(path,git)==expected
        if dest.exists():
            if not valid(dest):raise RuntimeError(f'Existing file differs; will not overwrite: {dest}')
            return dict(path=rel,bytes=item['size'],checksum=expected,status='already_verified')
        part=dest.with_name(dest.name+'.partial')
        url=f'{BASE}/resolve/{rev}/{quote(rel,safe="/")}?download=true'
        if not part.exists() or part.stat().st_size!=item['size']:
            subprocess.run(['curl.exe','-fLsS','--retry','5','--retry-delay','3',
                '--connect-timeout','20','--speed-limit','1024','--speed-time','120',
                '--continue-at','-','--output',str(part),url],check=True)
        if not valid(part):raise RuntimeError(f'Size/checksum mismatch: {rel}')
        part.rename(dest)
        return dict(path=rel,bytes=item['size'],checksum=expected,status='downloaded_verified')
    started=time.monotonic();results=[]
    # Small label table first, alongside waveforms.
    files.sort(key=lambda f:f['size'])
    with ThreadPoolExecutor(max_workers=args.workers) as pool:
        for future in as_completed([pool.submit(download,f) for f in files]):
            result=future.result();results.append(result)
            done=sum(r['bytes'] for r in results)
            print(f"[{len(results)}/{len(files)}] {result['path']} verified; {done/total:.1%}; {time.monotonic()-started:.0f}s",flush=True)
    (out/'download_manifest.json').write_text(json.dumps(dict(repository=REPO,revision=rev,
        complete=True,total_bytes=total,elapsed_seconds=time.monotonic()-started,
        verification='SHA256 for LFS; git blob SHA1 for ordinary files',files=results),indent=2),encoding='utf-8')
    print('COMPLETE',out,flush=True)

if __name__=='__main__':main()
