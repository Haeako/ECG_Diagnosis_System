"""Verify all8 module selections and ESP-IDF early dependency discovery."""
from pathlib import Path
import itertools
import json
import os
import subprocess
import uuid
import shutil
from contextlib import contextmanager

HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[1]
PROBE='''cmake_minimum_required(VERSION 3.16)
set(COMPONENT_LIB dummy_check)
function(idf_component_register)
  cmake_parse_arguments(part "" "" "SRCS;INCLUDE_DIRS;PRIV_REQUIRES" ${ARGN})
  file(WRITE "${CHECK_OUTPUT}" "${part_SRCS}")
endfunction()
function(target_compile_definitions)
endfunction()
include("${CHECK_MAIN}")
'''


@contextmanager
def scratch():
    folder=(HERE/('dummy-config-'+uuid.uuid4().hex)).resolve()
    assert folder.parent==HERE and folder.name.startswith('dummy-config-')
    folder.mkdir()
    try:yield folder
    finally:shutil.rmtree(folder)


def main():
    checks=[]
    with scratch() as temp:
        folder=Path(temp);script=folder/'probe.cmake';script.write_text(PROBE)
        output=folder/'sources.txt'
        def run(flags,early=False,seconds='60'):
            cmd=['cmake',f'-DCHECK_MAIN={(HERE/"main/CMakeLists.txt").as_posix()}',f'-DCHECK_OUTPUT={output.as_posix()}',
                 f'-DECG_TEST_SECONDS={seconds}']
            env=os.environ.copy()
            for name,value in zip(['BPM','SQI','AF'],flags):
                if early:env['ECG_TEST_'+name]='ON' if value else 'OFF'
                else:cmd.append('-DECG_TEST_'+name+'='+('ON' if value else 'OFF'))
            if early:
                cmd.append('-DCMAKE_BUILD_EARLY_EXPANSION=ON');env['ECG_TEST_SECONDS']=seconds
            result=subprocess.run([*cmd,'-P',str(script)],capture_output=True,text=True,env=env)
            return result,output.read_text().split(';') if result.returncode==0 else []
        for flags in itertools.product([False,True],repeat=3):
            expected={'dummy_modules.c'}
            if flags[0]:expected.add('fast_qrs_port.c')
            if flags[1]:expected.update(['sqi_runtime.c','sqi_orphanidou.c','sqi_template_matching.c'])
            if flags[2]:expected.update(['af_runtime.c','af_model_int8.c'])
            for early in [False,True]:
                result,sources=run(flags,early)
                assert result.returncode==0,result.stderr
                assert {Path(p).name for p in sources}==expected,(flags,early,sources)
                assert len(sources)==len(expected)
                checks.append(dict(bpm=flags[0],sqi=flags[1],af=flags[2],early=early,sources=sorted(expected)))
        for seconds in ['0','29','3601','invalid']:
            result,_=run([True,True,True],seconds=seconds)
            assert result.returncode!=0 and 'ECG_TEST_SECONDS' in result.stderr
    out=ROOT/'outputs/benchmarks/dummy_config_20261115';out.mkdir(parents=True,exist_ok=True)
    (out/'audit.json').write_text(json.dumps(dict(status='PASS',module_combinations=8,
        registrations_checked=16,invalid_duration_rejected=4,checks=checks),indent=2)+'\n')
    print('PASS: 8 module combinations, 16 normal/early registrations, 4 invalid durations rejected.')


if __name__=='__main__':main()
