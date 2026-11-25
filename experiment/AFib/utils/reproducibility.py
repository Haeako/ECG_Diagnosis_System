"""Fixed randomness policy for benchmark entry points and lazy ML runtimes."""
import os
import random
import sys

SEED = 42


def lock_random_seed(*, restart_for_hash=False):
    """Set process hash seed before startup and seed explicit global RNGs.

    Entry points call this before importing numerical/model libraries. A single
    restart is necessary because PYTHONHASHSEED cannot change a running process.
    """
    desired = str(SEED)
    if restart_for_hash and os.environ.get('PYTHONHASHSEED') != desired:
        environment = os.environ.copy()
        environment['PYTHONHASHSEED'] = desired
        os.execve(sys.executable, [sys.executable, *sys.argv], environment)
    os.environ['PYTHONHASHSEED'] = desired
    os.environ['CUBLAS_WORKSPACE_CONFIG'] = ':4096:8'
    os.environ['TF_DETERMINISTIC_OPS'] = '1'
    os.environ['TF_CUDNN_DETERMINISTIC'] = '1'
    random.seed(SEED)
    import numpy as np
    np.random.seed(SEED)
    seed_loaded_frameworks()


def seed_loaded_frameworks():
    """Configure loaded runtimes without importing unused ML dependencies."""
    torch = sys.modules.get('torch')
    if torch is not None:
        torch.manual_seed(SEED)
        if torch.cuda.is_available():
            torch.cuda.manual_seed_all(SEED)
        torch.use_deterministic_algorithms(True)
        torch.backends.cudnn.benchmark = False
        torch.backends.cudnn.deterministic = True
    tf = sys.modules.get('tensorflow')
    if tf is not None:
        tf.keras.utils.set_random_seed(SEED)
        tf.config.experimental.enable_op_determinism()
