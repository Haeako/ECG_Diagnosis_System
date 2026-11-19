"""Small shared file, native-build and plotting utilities; no benchmark entry point."""
import hashlib, importlib, importlib.util, json, os, shutil, subprocess, sys
from pathlib import Path
import numpy as np
ROOT=Path(__file__).resolve().parents[2]

def sha(path):
    with Path(path).open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()

def save(path,value):
    path=Path(path);path.parent.mkdir(parents=True,exist_ok=True)
    temp=path.with_suffix(path.suffix+'.tmp')
    temp.write_text(json.dumps(value,indent=2,ensure_ascii=False,allow_nan=False,default=lambda x:x.item() if isinstance(x,np.generic) else str(x)),encoding='utf-8');temp.replace(path)

def checked(path,digest):
    path=Path(path)
    if sha(path)!=digest:raise ValueError('Changed input: '+str(path))
    return path

def load_source(path,name):
    path=Path(path);spec=importlib.util.spec_from_file_location(name,path)
    module=importlib.util.module_from_spec(spec)
    exec(compile(path.read_bytes(),str(path),'exec'),module.__dict__)
    return module

def build(output,name,sources,include=(),flags=()):
    output=Path(output);output.mkdir(parents=True,exist_ok=True)
    digest=hashlib.sha256((''.join(sha(Path(p)) for p in sources)+str(flags)).encode()).hexdigest()[:16]
    lib=output/(name+'_'+digest+('.dll' if os.name=='nt' else '.so'))
    if not lib.exists():
        compiler=shutil.which('gcc')
        if not compiler:raise RuntimeError('GCC is required for native C modules')
        command=[compiler,'-O2','-std=c11','-shared',*(['-fPIC'] if os.name!='nt' else []),*['-I'+str(p) for p in include],*flags,*map(str,sources),'-o',str(lib),'-lm']
        result=subprocess.run(command,capture_output=True,text=True)
        (output/(name+'_build.log')).write_text(result.stdout+result.stderr);result.check_returncode()
    return lib

def registry(domain):
    import importlib
    result={}
    for p in sorted((ROOT/'server'/domain/'modules').glob('*.py')):
        if p.name.startswith('_'):continue
        m=importlib.import_module('server.'+domain+'.modules.'+p.stem)
        if hasattr(m,'METHOD'):result[m.METHOD]=m
    return result

def plot(frame,path,title,x='method',y='rmse',ylabel=None):
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    path=Path(path);path.parent.mkdir(parents=True,exist_ok=True)
    frame=frame.sort_values(y)
    fig,ax=plt.subplots(figsize=(max(8,len(frame)*.65),5))
    ax.bar(frame[x].astype(str),frame[y]);ax.set_title(title);ax.set_ylabel(ylabel or y)
    ax.tick_params(axis='x',labelrotation=65);ax.grid(axis='y',alpha=.25)
    fig.tight_layout();fig.savefig(path,dpi=160);plt.close(fig)

def verify_manifest(path,aspect):
    p=Path(path);m=json.loads(p.read_text(encoding='utf-8'))
    if m['aspect']!=aspect:raise ValueError('Manifest belongs to another aspect')
    for name,digest in m['source_sha256'].items():checked(ROOT/name,digest)
    return m

def fresh_output(path):
    p=Path(path).resolve()
    if not p.is_relative_to(ROOT/'outputs'):raise ValueError('Output must be inside workspace outputs/')
    p.mkdir(parents=True,exist_ok=False);return p
