"""PanTompkinsPlusPlus_2022: isolated implementation adapter."""
METHOD='PanTompkinsPlusPlus_2022'
CACHE='outputs/BPM/panpp_fixed_edb_20261115'
C_PORT=('pan_tompkins_plus_plus_port', 'panpp_workspace_t', 'panpp_detect_360')
from BPM.modules._runtime import ROOT,load_source

"""Project repair for repeated emission of one candidate by upstream search-back.

Load untouched upstream through a checked AST transformation. Suppress only a
second emission in the SAME candidate iteration within one sample. No general
refractory period, label-based cleanup, global deduplication or threshold tuning.
"""
from pathlib import Path
import ast
import types

ROOT=Path(__file__).resolve().parents[2]
UPSTREAM=ROOT/'external/pan-tompkins-plus-plus/algos/pan_tompkins_plus_plus.py'


def _load_fixed():
    import scipy.signal as signal
    if not hasattr(signal,'flattop'):signal.flattop=signal.windows.flattop
    tree=ast.parse(UPSTREAM.read_text(encoding='utf-8'))
    class Repair(ast.NodeTransformer):
        count=0
        def visit_For(self,node):
            self.generic_visit(node)
            if ast.unparse(node.target)=='i' and ast.unparse(node.iter)=='range(LLp)':
                # Emission tracking resets for every candidate, not every beat.
                node.body[:0]=ast.parse('_candidate_emission = None').body
            return node
        def visit_Assign(self,node):
            if len(node.targets)==1 and ast.unparse(node.targets[0])=='qrs_i_raw[Beat_C1 - 1]':
                assert ast.unparse(node.value)=='temp_value'
                self.count+=1
                return ast.parse('''
if _candidate_emission is not None and abs(temp_value - _candidate_emission) <= 1:
    Beat_C1 -= 1
else:
    qrs_i_raw[Beat_C1-1] = temp_value
    _candidate_emission = temp_value
''').body
            return node
    repair=Repair();tree=repair.visit(tree)
    if repair.count!=4:raise RuntimeError('STOP: upstream append sites changed; review repair')
    ast.fix_missing_locations(tree)
    module=types.ModuleType('project_panpp_fixed')
    module.__file__=str(UPSTREAM)
    exec(compile(tree,str(UPSTREAM),'exec'),module.__dict__)
    return module.Pan_Tompkins_Plus_Plus()

def load(output=None):
    detector=_load_fixed()
    return lambda x:detector.rpeak_detection(x,360)
