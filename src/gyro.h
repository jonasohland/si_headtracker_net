#pragma once

#include "net.h"

#ifdef SI_DETAIL
#include <MPU6050_6Axis_MotionApps20.h>
#else
#include <MPU6050.h>
#endif

void si_gy_prepare(si_device_state_t*);

void si_gyro_check(MPU6050* mpu, si_conf_t*, si_device_state* state);

void si_gy_run(MPU6050* mpu, EthernetUDP* socket, si_device_state_t* state, si_conf_t* conf);