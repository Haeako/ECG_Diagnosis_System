#include <stddef.h>
#include <stdint.h>
#ifdef REPLAY_0
#include "pan_pyecg.h"
#endif
#ifdef REPLAY_1
#include "elgendi_pyecg.h"
#endif
#ifdef REPLAY_2
#include "fast_qrs_port.h"
#endif
#ifdef REPLAY_3
#include "pan_tompkins_plus_plus_port.h"
#endif
#ifdef REPLAY_4
#include "wim_nguyen.h"
#endif
#ifdef REPLAY_5
#include "wim_elgendi.h"
#endif
#ifdef REPLAY_6
#include "wim_pantompkins.h"
#endif
#ifdef REPLAY_7
#include "wim_hamilton.h"
#endif
#ifdef REPLAY_8
#include "wim_arteaga_falconi.h"
#endif
#ifdef REPLAY_9
#include "wim_shaik.h"
#endif
#ifdef REPLAY_10
#include "wim_xu.h"
#endif
#ifdef REPLAY_11
#include "wim_zhai.h"
#endif
static size_t workspace_size(unsigned method) { switch(method) {
#ifdef REPLAY_0
case 0: return sizeof(pan_pyecg_workspace_t);
#endif
#ifdef REPLAY_1
case 1: return sizeof(elgendi_pyecg_workspace_t);
#endif
#ifdef REPLAY_2
case 2: return sizeof(fast_qrs_workspace_t);
#endif
#ifdef REPLAY_3
case 3: return sizeof(panpp_workspace_t);
#endif
#ifdef REPLAY_4
case 4: return sizeof(wim_nguyen_workspace_t);
#endif
#ifdef REPLAY_5
case 5: return sizeof(wim_elgendi_workspace_t);
#endif
#ifdef REPLAY_6
case 6: return sizeof(wim_pt_workspace_t);
#endif
#ifdef REPLAY_7
case 7: return sizeof(wim_hamilton_workspace_t);
#endif
#ifdef REPLAY_8
case 8: return sizeof(wim_af_workspace_t);
#endif
#ifdef REPLAY_9
case 9: return sizeof(wim_shaik_workspace_t);
#endif
#ifdef REPLAY_10
case 10: return sizeof(wim_xu_workspace_t);
#endif
#ifdef REPLAY_11
case 11: return sizeof(wim_zhai_workspace_t);
#endif
default:return 0;}}
static const char *method_name(unsigned method) {switch(method){
case 0: return "PanTompkins_pyecgdetectors";
case 1: return "Elgendi_pyecgdetectors";
case 2: return "FastQRS_0.3.0";
case 3: return "PanTompkinsPlusPlus_2022";
case 4: return "WIM_nguyen";
case 5: return "WIM_elgendi";
case 6: return "WIM_pantompkins";
case 7: return "WIM_hamilton";
case 8: return "WIM_arteagaFalconi";
case 9: return "WIM_shaik";
case 10: return "WIM_xu";
case 11: return "WIM_zhai";
default:return "invalid";}}
static size_t detect(unsigned method,const float *x,uint16_t *p,void *w) {switch(method){
#ifdef REPLAY_0
case 0:return pan_pyecg_detect_360(x,3600,p,128,(pan_pyecg_workspace_t *)w);
#endif
#ifdef REPLAY_1
case 1:return elgendi_pyecg_detect_360(x,3600,p,128,(elgendi_pyecg_workspace_t *)w);
#endif
#ifdef REPLAY_2
case 2:return fast_qrs_detect_360(x,3600,p,128,(fast_qrs_workspace_t *)w);
#endif
#ifdef REPLAY_3
case 3:return panpp_detect_360(x,3600,p,128,(panpp_workspace_t *)w);
#endif
#ifdef REPLAY_4
case 4:return wim_nguyen_detect_360(x,3600,p,128,(wim_nguyen_workspace_t *)w);
#endif
#ifdef REPLAY_5
case 5:return wim_elgendi_detect_360(x,3600,p,128,(wim_elgendi_workspace_t *)w);
#endif
#ifdef REPLAY_6
case 6:return wim_pt_detect_360(x,3600,p,128,(wim_pt_workspace_t *)w);
#endif
#ifdef REPLAY_7
case 7:return wim_hamilton_detect_360(x,3600,p,128,(wim_hamilton_workspace_t *)w);
#endif
#ifdef REPLAY_8
case 8:return wim_af_detect_360(x,3600,p,128,(wim_af_workspace_t *)w);
#endif
#ifdef REPLAY_9
case 9:return wim_shaik_detect_360(x,3600,p,128,(wim_shaik_workspace_t *)w);
#endif
#ifdef REPLAY_10
case 10:return wim_xu_detect_360(x,3600,p,128,(wim_xu_workspace_t *)w);
#endif
#ifdef REPLAY_11
case 11:return wim_zhai_detect_360(x,3600,p,128,(wim_zhai_workspace_t *)w);
#endif
default:return 128;}}
