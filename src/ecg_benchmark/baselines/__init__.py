"""Small deterministic baselines used by the reference experiment."""

from .fir import fir_bandpass, fir_highpass
from .wavelet import wavelet_denoise

__all__ = ["fir_bandpass", "fir_highpass", "wavelet_denoise"]
