#ifndef PS3_MOVE_GESTURES_H
#define PS3_MOVE_GESTURES_H
#include <math.h>
#include <stdint.h>
#include <string.h>
/* GEM acceleration is in G; angular velocity is radians/second. Detection uses
 * fresh sensor timestamps, a gravity high-pass filter, and a neutral rearm. */
typedef struct {
 uint64_t previous;
 float gravity[3], angle;
 double started, quiet_since, cooldown_until;
 int mode, direction, axis, peaks, sign, peak_ready, armed;
} ps3_gesture_state_t;
typedef struct { int locked, turn, shake; } ps3_gesture_result_t;
static ps3_gesture_result_t PS3_GestureStep(ps3_gesture_state_t *s,uint64_t stamp,
 const float *acc,float gx,float gy,float gz,int allow,int shake,int flick,
 float shake_limit,float flick_limit,float cooldown)
{
 ps3_gesture_result_t out={0};
 if(!allow || !isfinite(gx) || !isfinite(gy) || !isfinite(gz) ||
    !isfinite(acc[0]) || !isfinite(acc[1]) || !isfinite(acc[2])) {memset(s,0,sizeof(*s));return out;}
 if(stamp==s->previous) {out.locked=s->mode!=0;return out;}
 if(!s->previous || stamp<s->previous || stamp-s->previous>100000) {
  memset(s,0,sizeof(*s));s->previous=stamp;
  for(int i=0;i<3;i++)s->gravity[i]=acc[i];
  return out;
 }
 float dt=(stamp-s->previous)*.000001f;s->previous=stamp;
 double now=stamp*.000001;
 shake_limit=isfinite(shake_limit)?fminf(2.5f,fmaxf(.3f,shake_limit)):.8f;
 flick_limit=isfinite(flick_limit)?fminf(8.f,fmaxf(1.5f,flick_limit)):3.f;
 cooldown=isfinite(cooldown)?fminf(3.f,fmaxf(.3f,cooldown)):.8f;
 float high[3],maximum=0;int axis=0;
 for(int i=0;i<3;i++) {
  high[i]=acc[i]-s->gravity[i];
  s->gravity[i]+=high[i]*(1-expf(-dt/.35f));
  if(fabsf(high[i])>maximum){maximum=fabsf(high[i]);axis=i;}
 }
 int quiet=fabsf(gx)<.6f && fabsf(gy)<.6f && fabsf(gz)<.6f && maximum<shake_limit*.45f;
 if(quiet) {if(!s->quiet_since)s->quiet_since=now;} else s->quiet_since=0;
 if(s->mode==3) {
  if((s->quiet_since && now-s->quiet_since>=.18) || now-s->started>1.) {
   s->mode=0;s->armed=0;s->quiet_since=0;
  }
  // Consume the release sample as well; optical neutral resets next frame.
  out.locked=1;return out;
 }
 if(!s->mode) {
  if(s->quiet_since && now-s->quiet_since>=.18 && now>=s->cooldown_until)s->armed=1;
  if(!s->armed)return out;
  if(shake && maximum>=shake_limit) {
   s->mode=2;s->axis=axis;s->sign=high[axis]>0?1:-1;s->peaks=1;s->peak_ready=0;s->started=now;
  } else if(flick && fabsf(gy)>=flick_limit && fabsf(gy)>fabsf(gx)*1.5f && fabsf(gy)>fabsf(gz)*1.5f && maximum<shake_limit) {
   s->mode=1;s->direction=gy>0?1:-1;s->angle=0;s->started=now;
  }
 }
 if(s->mode==1) {
  s->angle+=gy*dt*s->direction;
  if(s->angle>=.14f && now-s->started>=.02) {out.turn=s->direction;s->mode=3;}
  else if(gy*s->direction<0 || now-s->started>.35)s->mode=3;
 } else if(s->mode==2) {
  float value=high[s->axis];
  if(fabsf(value)<shake_limit*.4f)s->peak_ready=1;
  // Opposite peaks can cross the deadband between samples at low frame rates.
  if(fabsf(value)>=shake_limit && (value>0?1:-1)!=s->sign) {
   s->sign=value>0?1:-1;s->peaks++;s->peak_ready=0;
   if(s->peaks>=3){out.shake=1;s->mode=3;}
  }
  if(now-s->started>.6)s->mode=3;
 }
 if(s->mode) {
  out.locked=1;s->armed=0;
  if(s->mode==3) {s->cooldown_until=now+cooldown;s->quiet_since=0;s->started=now;}
 }
 return out;
}
#endif
