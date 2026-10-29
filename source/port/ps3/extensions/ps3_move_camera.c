/* Optional, local-only PS Eye/GEM optical tracking. Camera frames are never
 * displayed, recorded or transmitted. Gyro-only input does not open a camera. */
#include "extension_compat.h"
#include "ps3_move_camera.h"

#include <stdint.h>
#include <string.h>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstrict-prototypes"
#pragma GCC diagnostic ignored "-Wold-style-definition"
#include <io/move.h>
#include <io/camera.h>
#include <sys/memory.h>
#include <sysmodule/sysmodule.h>
#include <lv2/systime.h>
#pragma GCC diagnostic pop

static CVAR_DEFINE_AUTO(move_camera, "0", FCVAR_ARCHIVE, "use PS Eye light-ball tracking");
static CVAR_DEFINE_AUTO(move_camera_sensitivity, "3.250000", FCVAR_ARCHIVE, "light-ball cursor and look sensitivity");
static CVAR_DEFINE_AUTO(move_camera_mirror, "0", FCVAR_ARCHIVE, "mirror camera X for natural screen pointing");
static CVAR_DEFINE_AUTO(move_camera_fallback, "1", FCVAR_ARCHIVE, "use gyro if the light ball is not tracked");
static CVAR_DEFINE_AUTO(move_camera_status, "Disabled - gyro only", FCVAR_READ_ONLY, "PS Eye tracking status");
static CVAR_DEFINE_AUTO(move_camera_edge_turn, "0", FCVAR_ARCHIVE, "continuous horizontal turning near optical aim edges");
static CVAR_DEFINE_AUTO(move_camera_edge_zone, "0.65", FCVAR_ARCHIVE, "central optical aim region, fraction of half-width");
static CVAR_DEFINE_AUTO(move_camera_turn_speed, "300", FCVAR_ARCHIVE, "maximum optical edge turn speed in degrees/sec");
static CVAR_DEFINE_AUTO(move_camera_stability, "0", FCVAR_ARCHIVE, "optical stability defaults revision");
static CVAR_DEFINE_AUTO(move_camera_steadiness, "1", FCVAR_ARCHIVE, "optical jitter filtering strength, 0 to 3");
static qboolean filtered_valid, edge_armed, clutch_held, recenter_on_release, center_pending;
static float filtered_x,filtered_y, center_sum_x,center_sum_y, center_start_x,center_start_y;
static int center_samples;
static double center_ready_at,center_still_at,last_poll;
static qboolean module_ready, camera_ready, container_ready, opened, started;
static sys_mem_container_t container;
static cameraInfoEx camera;
static int selected = -1;
static qboolean calibrating_ball, have_frame, have_point, centered;
static u32 previous_frame;
static float center_x=.5f, center_y=.5f, last_x, last_y, raw_x, raw_y;
static double retry_at, last_visible, last_image_update;
static double inertial_error_at;
static qboolean last_cursor, last_mirror=true;
static qboolean lost_edge_armed, optical_lost;
static double loss_started, next_tracking_report, worst_loss;
static unsigned tracking_losses, tracking_returns;
static void CameraReport(void) {
 if(host.realtime<next_tracking_report)return;
 if(tracking_losses || tracking_returns)
  Con_Printf("PS Eye: losses %u returns %u longest %.0fms; gyro bridge %s\n",
   tracking_losses,tracking_returns,worst_loss*1000,move_camera_fallback.value?"on":"off");
 tracking_losses=tracking_returns=0;worst_loss=0;next_tracking_report=host.realtime+10;
}

qboolean PS3_MoveCameraCalibrating(void){return calibrating_ball;}
qboolean PS3_MoveCameraTrackingReady(void){return started && have_frame && !calibrating_ball;}

static void CameraStatus(const char *message) {
 if(Q_strcmp(move_camera_status.string,message)) Cvar_FullSet("move_camera_status",message,FCVAR_READ_ONLY);
}
static void CameraLosePoint(void) { have_point=false; filtered_valid=false; edge_armed=false; }
void PS3_MoveCameraColorChanged(void) { CameraLosePoint(); last_poll=0; }
void PS3_MoveCameraReset(void) { optical_lost=lost_edge_armed=false; CameraLosePoint(); last_poll=0; clutch_held=recenter_on_release=false; }
void PS3_MoveCameraClutch(qboolean held) {
 if(clutch_held && !held) recenter_on_release=true;
 clutch_held=held; optical_lost=lost_edge_armed=false; CameraLosePoint();
}
static void CameraSetCenter(float x,float y) {
 center_x=x; center_y=y; centered=true; center_pending=false;
 filtered_x=x; filtered_y=y; filtered_valid=true;
 have_point=false; edge_armed=false;
}
static qboolean CameraLost(void) {
 if(!optical_lost) {
  optical_lost=true;loss_started=host.realtime;lost_edge_armed=edge_armed;
  tracking_losses++;
 }
 CameraLosePoint();
 // Gyro deltas are already bias/deadzone corrected, time bounded, and respect
 // clutch/gesture/UI ownership. Bridge immediately, without a half-second freeze.
 return !move_camera_fallback.value;
}
void PS3_MoveCameraShutdown(void) {
 if(started) cameraStop(0);
 if(opened) cameraClose(0);
 if(camera_ready) cameraEnd();
 if(container_ready) sysMemContainerDestroy(container);
 if(module_ready) sysModuleUnload(SYSMODULE_CAMERA);
 started=opened=camera_ready=container_ready=module_ready=false;
 have_frame=have_point=calibrating_ball=false; selected=-1; optical_lost=lost_edge_armed=false;
 retry_at=0; last_poll=0; last_image_update=0; CameraLosePoint(); center_pending=false; clutch_held=recenter_on_release=false;
}
/* GEM still needs an update cycle when there is no camera image. In particular,
 * closing PS Eye must not stop the inertial pipeline. NO_VIDEO (6) is an
 * informational result: UpdateStart still owns a matching UpdateFinish. */
void PS3_MoveCameraPumpInertial(void) {
 if(!move_camera.value && module_ready) PS3_MoveCameraShutdown();
 if(move_camera.value && started && last_image_update>0 &&
    host.realtime-last_image_update<=1.0/30.0)return;
 halo_move_trace_stage(20);
 int rc=gemUpdateStart(NULL,sysGetSystemTime());
 if(rc==0 || rc==6) {
  halo_move_trace_stage(21);
  int finish=gemUpdateFinish();
  rc=(finish==0 || finish==6)?0:finish;
 }
 if(rc && host.realtime>=inertial_error_at) {
  Con_Printf("PS Move: no-image GEM update failed 0x%x\n",rc);
  inertial_error_at=host.realtime+5;
 }
 halo_move_trace_stage(0);
}
static void CameraFailure(const char *stage,int rc) {
 Con_Printf("PS Eye: %s failed 0x%x\n",stage,rc);
 PS3_MoveCameraShutdown();
 CameraStatus(va("%s: 0x%x - check PS Eye / Retry",stage,rc));
 retry_at=host.realtime+5;
}
static void CameraRetry(void) { PS3_MoveCameraShutdown(); }
static void CameraCalibrate(void) {
 if(!started || selected<0) { CameraStatus("Connect PS Eye and Move, then enable tracking"); return; }
 int rc=gemCalibrate(selected);
 if(rc) { CameraStatus(va("Ball calibration failed: 0x%x",rc)); return; }
 calibrating_ball=true; have_point=false;
 CameraStatus("Hold lit ball still, facing the PS Eye");
}
static void CameraRecenter(void) {
 if(!move_camera.value) { CameraStatus("Enable PS Eye tracking to recenter optical aim"); return; }
 // Capture the next fresh visible image, never an old point from before loss.
 recenter_on_release=true; center_pending=false; CameraLosePoint();
 CameraStatus("Recenter requested - hold wand at neutral");
}
static void CameraCenter(void) {
 if(!started || selected<0) { CameraStatus("Enable tracking and show the ball first"); return; }
 center_pending=true; center_ready_at=host.realtime+.75; center_samples=0;
 CameraLosePoint();
 CameraStatus("Rest hand at neutral; hold still to set center");
}

void PS3_MoveCameraInit(void) {
 Cvar_RegisterVariable(&move_camera); Cvar_RegisterVariable(&move_camera_sensitivity);
 Cvar_RegisterVariable(&move_camera_mirror); Cvar_RegisterVariable(&move_camera_fallback);
 Cvar_RegisterVariable(&move_camera_status);
 Cvar_RegisterVariable(&move_camera_edge_turn); Cvar_RegisterVariable(&move_camera_edge_zone);
 Cvar_RegisterVariable(&move_camera_turn_speed);
 Cvar_RegisterVariable(&move_camera_stability); Cvar_RegisterVariable(&move_camera_steadiness);
 Cmd_AddCommand("move_camera_retry",CameraRetry,"retry PS Eye tracking");
 Cmd_AddCommand("move_camera_calibrate",CameraCalibrate,"calibrate the visible Move light ball");
 Cmd_AddCommand("move_camera_recenter",CameraRecenter,"recenter optical aim at the next visible wand position");
 Cmd_AddCommand("move_camera_center",CameraCenter,"set the current visible ball position as screen center");
}
static qboolean CameraStart(void) {
 cameraType type;
 int rc; const char *stage="Camera module";
 rc=sysModuleLoad(SYSMODULE_CAMERA); if(rc<0) goto failed; module_ready=true;
 stage="Camera initialization"; rc=cameraInit(); if(rc) goto failed; camera_ready=true;
 stage="PS Eye detection"; rc=cameraGetType(0,&type); if(rc) goto failed;
 if(type!=CAM_TYPE_PLAYSTATION_EYE) { rc=CAMERA_ERRO_NO_DEVICE_FOUND; goto failed; }
 stage="Camera memory"; rc=sysMemContainerCreate(&container,0x200000); if(rc) goto failed; container_ready=true;
 memset(&camera,0,sizeof(camera)); camera.format=CAM_FORM_RAW8;
 camera.resolution=CAM_RESO_VGA; camera.framerate=60; camera.info_ver=0x0200;
 camera.container=container; camera.readmode=0;
 stage="Camera open"; rc=cameraOpenEx(0,&camera); if(rc) goto failed; opened=true;
 stage="Camera prepare"; rc=gemPrepareCamera(128,.5f); if(rc) goto failed;
 stage="Camera start"; rc=cameraStart(0); if(rc) goto failed; started=true;
 CameraStatus("Show the lit ball to the PS Eye; hold still to calibrate");
 return true;
failed:
 CameraFailure(stage,rc); return false;
}
/* Rate input is integrated once per engine poll, even if the camera runs slower.
 * Relative aim still uses each image only once. No turn survives a clutch,
 * menu transition, missing ball, stale camera, or long frame gap. */
static void CameraEdgeTurn(qboolean cursor, qboolean blocked, double dt, float *yaw) {
 if(PS3_MoveFreeAimLocked()) return;
 if(!move_camera_edge_turn.value || !edge_armed || cursor || blocked || !have_point || last_cursor ||
    dt<=0 || dt>.1 || host.realtime-last_visible>.1) return;
 float zone=isfinite(move_camera_edge_zone.value)?bound(.3f,move_camera_edge_zone.value,.9f):.65f;
 float speed=isfinite(move_camera_turn_speed.value)?bound(30.f,move_camera_turn_speed.value,360.f):180.f;
 float offset=(last_x-.5f)*2.f;
 float ramp=bound(0.f,(fabsf(offset)-zone)/(1.f-zone),1.f);
 // Smooth entry into the zone, full speed at the edge. Y stays relative only.
 ramp=ramp*ramp*(3.f-2.f*ramp);
 *yaw-=copysignf(ramp*speed*(float)dt,offset);
}
qboolean PS3_MoveCameraFrame(int wand,qboolean cursor,qboolean blocked,float *yaw,float *pitch) {
 cameraReadInfo frame; gemState state; gemImageState image;
 int rc;
 double dt=last_poll>0?host.realtime-last_poll:0;
 last_poll=host.realtime;
 // Reset before testing duplicate frames: opening a menu or holding the clutch
 // must stop rate turning even when there is no new camera image this poll.
 if(cursor || blocked) {have_point=false;edge_armed=false;lost_edge_armed=false;}
 CameraReport();
 if(move_camera_stability.value<2) {
  Cvar_SetValue("move_camera_fallback",1); Cvar_SetValue("move_camera_stability",2);
  Con_Printf("PS Eye: immediate gyro bridging enabled for optical loss\n");
 }
 if(!move_camera.value) {
  if(module_ready) PS3_MoveCameraShutdown();
  CameraStatus("Disabled - gyro only"); return false;
 }
 if(wand<0) { if(module_ready) PS3_MoveCameraShutdown(); have_point=false; CameraStatus("Waiting for Move wand"); return false; }
 if(!started && (host.realtime<retry_at || !CameraStart())) return CameraLost();
 if(wand!=selected) { selected=wand; CameraLosePoint(); calibrating_ball=false; centered=false; center_x=center_y=.5f; }
 memset(&frame,0,sizeof(frame)); frame.version=0x0100; frame.buffer=camera.buffer;
 rc=cameraReadEx(0,&frame);
 if(rc && rc!=CAMERA_ERRO_TIMEOUT) { CameraFailure("Camera read",rc); return CameraLost(); }
 if(rc || !frame.readcount || (have_frame && frame.frame==previous_frame)) {
  if(host.realtime-last_visible>1.0/30.0) { CameraStatus("Tracking lost - show ball to PS Eye"); return CameraLost(); }
  CameraEdgeTurn(cursor,blocked,dt,yaw);
  return true; // relative deltas never integrate the same optical frame twice
 }
 have_frame=true; previous_frame=frame.frame;
 if(!frame.buffer || (frame.buffer&127)) { CameraFailure("Camera buffer alignment",-1); return CameraLost(); }
 rc=gemUpdateStart((void *)(uintptr_t)frame.buffer,frame.timestamp);
 if(rc) { CameraFailure("Tracking update",rc); return CameraLost(); }
 rc=gemUpdateFinish();
 if(rc) { CameraFailure("Tracking finish",rc); return CameraLost(); }
 last_image_update=host.realtime;
 memset(&state,0,sizeof(state)); rc=gemGetState(wand,STATE_LATEST_IMAGE_TIME,0,&state);
 if(rc==2 && !calibrating_ball) CameraCalibrate();
 else if(rc==5) {
  u32 hues[MAX_MOVES]; for(int i=0;i<MAX_MOVES;i++) hues[i]=8u<<24;
  hues[wand]=4u<<24; int hue_rc=gemTrackHues(hues,NULL);
  if(hue_rc) CameraStatus(va("Tracking color failed: 0x%x",hue_rc));
 }
 if(rc) {
  if(rc==3 || rc==4) CameraStatus("Calibrating ball - hold still in camera view");
  else if(rc!=2 && rc!=5) CameraStatus(va("Waiting for optical tracking (state %d)",rc));
  return CameraLost();
 }
 calibrating_ball=false;
 memset(&image,0,sizeof(image)); rc=gemGetImageState(wand,&image);
 if(rc || !image.visible || !(state.tracking&GEM_TRACKING_VISIBLE) ||
  !isfinite(image.u) || !isfinite(image.v) || image.u<0 || image.u>640 || image.v<0 || image.v>480) {
  center_samples=0; CameraStatus("Ball hidden - gyro bridge active"); return CameraLost();
 }
 if(last_mirror != (move_camera_mirror.value!=0)) {
  last_mirror=move_camera_mirror.value!=0; center_x=center_y=.5f; centered=false; CameraLosePoint();
 }
 if(optical_lost) {
  double gap=host.realtime-loss_started;
  worst_loss=fmax(worst_loss,gap);tracking_returns++;
  // Rebase the relative optical delta: never replay motion supplied by gyro.
  // A brief dropout keeps an intentional edge turn armed. Longer loss, menus
  // and clutch changes still require neutral before continuous turning.
  edge_armed=lost_edge_armed && gap<=.25 && !cursor && !blocked;
  optical_lost=lost_edge_armed=false;
 }
 raw_x=image.u/640.f; raw_y=image.v/480.f;
 if(move_camera_mirror.value) raw_x=1.f-raw_x;
 // Filter in camera coordinates before sensitivity and before cursor clamping.
 // A small positional deadband absorbs stationary ball jitter without integrating
 // it into drift; larger intentional movement is smoothed over roughly 25ms.
 float steadiness=isfinite(move_camera_steadiness.value)?bound(0.f,move_camera_steadiness.value,3.f):1.f;
 if(!filtered_valid) {filtered_x=raw_x;filtered_y=raw_y;filtered_valid=true;}
 else {
  float alpha=steadiness>0 ? 1.f-expf(-(float)bound(0.,host.realtime-last_visible,.1)/(.025f*steadiness)) : 1.f;
  float dx=raw_x-filtered_x,dy=raw_y-filtered_y;
  float ex=steadiness*1.5f/640.f,ey=steadiness*1.5f/480.f;
  filtered_x+=copysignf(fmaxf(0,fabsf(dx)-ex)*alpha,dx);
  filtered_y+=copysignf(fmaxf(0,fabsf(dy)-ey)*alpha,dy);
 }
 if(!centered) CameraSetCenter(raw_x,raw_y); // begin at the user's hand, not camera center
 if(recenter_on_release && !blocked) {
  CameraSetCenter(raw_x,raw_y); recenter_on_release=false;
 }
 if(center_pending) {
  have_point=false; edge_armed=false; last_visible=host.realtime;
  CameraStatus("Rest hand at neutral; hold still to set center");
  if(host.realtime<center_ready_at) return true;
  if(!center_samples || fabsf(raw_x-center_start_x)>.015f || fabsf(raw_y-center_start_y)>.015f) {
   center_start_x=raw_x; center_start_y=raw_y; center_sum_x=center_sum_y=0;
   center_samples=0; center_still_at=host.realtime;
  }
  center_sum_x+=raw_x; center_sum_y+=raw_y; center_samples++;
  if(host.realtime-center_still_at>=.35 && center_samples>=8)
   CameraSetCenter(center_sum_x/center_samples,center_sum_y/center_samples);
  return true;
 }
 float gain=isfinite(move_camera_sensitivity.value)?bound(.25f,move_camera_sensitivity.value,4.f):1.f;
 float x=.5f+(filtered_x-center_x)*gain;
 float y=.5f+(filtered_y-center_y)*gain;
 qboolean continuous=have_point && !last_cursor;
 last_visible=host.realtime;
 CameraStatus(centered?"Light ball tracked - center calibrated":"Light ball tracked - Center pointer to align");
 if(blocked) have_point=false;
 else if(cursor) PS3_MoveCursorAbsolute(bound(0.f,x,1.f),bound(0.f,y,1.f));
 else if(have_point && !last_cursor) {
  // Reacquisition already clears the baseline. Do not discard large valid
  // tracked movements: discarding only the outward sweep makes the return
  // sweep change the view, which feels like an ever-moving neutral point.
  *yaw=-(x-last_x)*90.f; *pitch=(y-last_y)*60.f;
 }
 float zone=isfinite(move_camera_edge_zone.value)?bound(.3f,move_camera_edge_zone.value,.9f):.65f;
 if(!cursor && !blocked && fabsf((x-.5f)*2.f)<zone-.08f) edge_armed=true;
 last_x=x; last_y=y; last_cursor=cursor; have_point=!blocked;
 if(continuous) CameraEdgeTurn(cursor,blocked,dt,yaw);
 return true;
}
