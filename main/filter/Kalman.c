/*
 * Kafman.h
 *
 *  Created on: Jan 4, 2026
 *      Author: LENOVO
 *      reinplement for C
 */


/*
 * SimpleKalmanFilter - a Kalman Filter implementation for single variable models.
 * Created by Denys Sene, January, 1, 2017.
 * Released under MIT License - see LICENSE file for details.
 */

#include "Kalman.h"
#include <math.h>

float _err_measure;
float _err_estimate;
float _q;
float _current_estimate = 0;
float _last_estimate = 0;
float _kalman_gain = 0;

void KalmanFilter_init(float mea_e, float est_e, float q)
{
  _err_measure = mea_e;
  _err_estimate = est_e;
  _q = q;
}

float KFupdateEstimate(float mea)
{
  _kalman_gain = _err_estimate / (_err_estimate + _err_measure);
  _current_estimate = _last_estimate + _kalman_gain * (mea - _last_estimate);
  _err_estimate = (1.0f - _kalman_gain) * _err_estimate + fabsf(_last_estimate - _current_estimate) * _q;
  _last_estimate = _current_estimate;

  return _current_estimate;
}

void KFsetMeasurementError(float mea_e)
{
  _err_measure = mea_e;
}

void KFsetEstimateError(float est_e)
{
  _err_estimate = est_e;
}

void KFsetProcessNoise(float q)
{
  _q = q;
}

float KFgetKalmanGain()
{
  return _kalman_gain;
}

float KFgetEstimateError()
{
  return _err_estimate;
}
