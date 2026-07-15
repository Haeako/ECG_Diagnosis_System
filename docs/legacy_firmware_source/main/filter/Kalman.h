/*
 * Kafman.h
 *
 *  Created on: Jan 4, 2026
 *      Author: LENOVO
 */

#ifndef SRC_KALMAN_H_
#define SRC_KALMAN_H_

void KalmanFilter_init(float mea_e, float est_e, float q);
float KFupdateEstimate(float mea);
void KFsetMeasurementError(float mea_e);
void KFsetEstimateError(float est_e);
void KFsetProcessNoise(float q);
float KFgetKalmanGain();
float KFgetEstimateError();

extern float _err_measure;
extern float _err_estimate;
extern float _q;
extern float _current_estimate;
extern float _last_estimate;
extern float _kalman_gain;



#endif /* SRC_KALMAN_H_ */
