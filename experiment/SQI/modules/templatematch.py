"""NeuroKit templatematch, cleaned500Hz input, frozen preprocessing."""
METHOD='templatematch'
CACHE='outputs/SQI/survey_additional_full_20261118'
CACHE_METHOD=METHOD
def load(output=None):
    from SQI.modules._runtime import neurokit
    import numpy as np
    nk=neurokit()
    def score(window):
        x,p=window.clean()
        if len(p)<3:return np.nan
        if METHOD=='zhao_fuzzy':return {'Unacceptable':0.,'Barely acceptable':.5,'Excellent':1.}[nk.ecg_quality(x,rpeaks=p,sampling_rate=500,method='zhao2018',approach='fuzzy')]
        values=np.asarray(nk.ecg_quality(x,rpeaks=p,sampling_rate=500,method=METHOD),float)
        if values.size!=5000 or not np.isfinite(values).all():return np.nan
        return float(-np.abs(values).mean() if METHOD=='dissimilarity' else values.mean())
    return score
