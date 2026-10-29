#ifndef PS3_MOVE_CAMERA_H
#define PS3_MOVE_CAMERA_H
qboolean PS3_MoveCameraCalibrating(void);
qboolean PS3_MoveCameraTrackingReady(void);
void PS3_MoveCameraColorChanged(void);
void PS3_MoveCameraInit(void);
void PS3_MoveCameraPumpInertial(void);
void PS3_MoveCameraShutdown(void);
void PS3_MoveCameraReset(void);
void PS3_MoveCameraClutch(qboolean held);
qboolean PS3_MoveCameraFrame(int wand, qboolean cursor, qboolean blocked, float *yaw, float *pitch);
void PS3_MoveCursorAbsolute(float x, float y);
#endif
