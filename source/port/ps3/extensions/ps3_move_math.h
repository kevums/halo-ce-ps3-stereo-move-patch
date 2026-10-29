#ifndef PS3_MOVE_MATH_H
#define PS3_MOVE_MATH_H
#include <math.h>
#include <stdint.h>
/* Gem inertial gyro is radians/sec: +X pitches up, +Y yaws left.
 * Timestamp gaps are discarded, not integrated into a reconnect/pause jump. */
static int PS3_MoveDelta(uint64_t previous, uint64_t now, float gx, float gy,
 float sensitivity, float deadzone, int invert, int clutch, float *yaw, float *pitch)
{
 float dt;
 *yaw = *pitch = 0;
 if (!previous || now <= previous || now - previous > 100000 || clutch) return 0;
 if (!isfinite(gx) || !isfinite(gy) || fabsf(gx)>64.f || fabsf(gy)>64.f) return 0;
 if (!isfinite(sensitivity)) sensitivity = 2;
 if (!isfinite(deadzone)) deadzone = 0.015f;
 sensitivity = fminf(8, fmaxf(0.25f, sensitivity));
 deadzone = fminf(0.2f, fmaxf(0, deadzone));
 dt = (float)(now - previous) * 0.000001f;
 /* Continuous deadzone avoids a step when crossing the drift threshold. */
 gx = copysignf(fmaxf(0, fabsf(gx) - deadzone), gx);
 gy = copysignf(fmaxf(0, fabsf(gy) - deadzone), gy);
 *yaw = gy * dt * 57.2957795f * sensitivity;
 *pitch = gx * dt * 57.2957795f * sensitivity * (invert ? 1 : -1);
 return 1;
}
#endif
