// MiniArcade Color: the MPU6050 (GY-521) on I2C. Only the accelerometer is
// used: it shows which way the console is tilted.
#pragma once
#include <stdint.h>

bool gyroOpen(int8_t sda, int8_t scl);   // true: a sensor answers on these pins
void gyroClose();
bool gyroAccel(int16_t a[3]);            // x, y, z; 16384 = 1 g
