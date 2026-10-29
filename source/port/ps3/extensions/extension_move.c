/* GEM lifecycle and timestamp/gyro conventions adapted from NSPS3 ps3_move.c.
 * Camera and gesture algorithms are retained in their own reference-derived files. */
#include "extension_compat.h"
#include "ps3_input.h"
#include "ps3_move_camera.h"
#include "ps3_move_math.h"
#include "ps3_move_gestures.h"
#include <io/move.h>
#include <spurs/spurs.h>
#include <sys/spu.h>
#include <sys/thread.h>
#include <sysmodule/sysmodule.h>
#include <stddef.h>

typedef char gem_attr_abi[sizeof(gemAttribute)==24?1:-1];
typedef char gem_pad_abi[offsetof(gemInertialState,pad)==64?1:-1];
typedef char gem_time_abi[offsetof(gemInertialState,time)==88?1:-1];
static Spurs spurs;
static int gem_ready,spurs_ready,gem_module,spurs_module,connected,wand=-1,attempted;
static unsigned pad_mask;
static uint64_t previous_time;
static int previous_counter=-1;
static double previous_poll,retry_at,calibration_begin,calibration_until;
static unsigned calibration_samples;
static float bias_x,bias_y,sum_x,sum_y,pending_yaw,pending_pitch;
static unsigned previous_buttons;
static ps3_gesture_state_t gestures;
static float pending_turn;
static int clutch;
static int enabled_at_start=-1;
static int map_loading;
static int deferred_rumble=-1;
static double last_look=-100;
static float value(const char *name){return Cvar_VariableValue(name);}
static int orb_wand=-1,orb_owned,orb_mode=-1;
static float orb_saved_r,orb_saved_g,orb_saved_b;
static double orb_retry,sensor_report;
/* Diagnostic only: the display heartbeat can identify a blocked SDK call. */
static volatile int trace_stage;
static volatile uint64_t trace_at;
static volatile unsigned trace_sequence;
static unsigned trace_frames=8;
static const char *trace_names[]={"idle","load SPURS module","load GEM module",
 "SPURS attributes","SPURS initialize","GEM initialize","GEM reset",
 "inertial pump","GEM connection query","GEM latest sample","gyro history",
 "gesture history","camera frame","orb update","rumble","reserved",
 "map load paused","GEM shutdown","reserved","reserved",
 "no-image GEM update start","no-image GEM update finish"};
void halo_move_trace_stage(int stage){
 if(stage==trace_stage)return;
 trace_at=ps3_host_microseconds();trace_stage=stage;trace_sequence++;
 if(trace_frames && stage>=0 && stage<(int)(sizeof(trace_names)/sizeof(trace_names[0])))
  Con_Printf("[move call] #%u %s loading=%d gem=%d spurs=%d\n",trace_sequence,trace_names[stage],map_loading,gem_ready,spurs_ready);
}
void halo_move_trace_report(void){
 int stage=trace_stage;uint64_t now=ps3_host_microseconds(),since=trace_at;
 const char *name=stage>=0 && stage<(int)(sizeof(trace_names)/sizeof(trace_names[0]))?trace_names[stage]:"unknown";
 Con_Printf("[move trace] #%u stage=%s age=%llu ms enabled=%d loading=%d gem=%d spurs=%d connected=%d wand=%d packet=%d sample=%llu poll_age=%.0f ms\n",
  trace_sequence,name,(unsigned long long)(since && now>=since?(now-since)/1000:0),enabled_at_start,map_loading,
  gem_ready,spurs_ready,connected,wand,previous_counter,(unsigned long long)previous_time,
  host.realtime>0?now*.001-host.realtime*1000:0);
}
static int orb_track(int hue){
 u32 hues[MAX_MOVES];for(int i=0;i<MAX_MOVES;i++)hues[i]=8u<<24;
 hues[orb_wand]=(u32)hue;return gemTrackHues(hues,NULL);
}
static void orb_restore(void){
 if(orb_owned && gem_ready && orb_wand>=0){
  if(value("move_camera"))orb_track(4u<<24);
  else gemForceRGB(orb_wand,orb_saved_r,orb_saved_g,orb_saved_b);
  PS3_MoveCameraColorChanged();
 }
 orb_owned=0;orb_mode=-1;orb_wand=-1;orb_retry=0;
}
static void orb_frame(void){
 if(orb_wand!=wand){orb_restore();orb_wand=wand;}
 if(!value("move_blue_orb")){
  if(orb_owned){orb_restore();orb_wand=wand;}
  Cvar_FullSet("move_orb_status","Blue orb off",FCVAR_READ_ONLY);return;
 }
 if(PS3_MoveCameraCalibrating() || host.realtime<orb_retry)return;
 int optical=value("move_camera")!=0;
 if(orb_mode==(optical?2:1)){
  u32 hue;
  if(!optical || (!gemGetTrackerHue(wand,&hue) && hue>=200 && hue<=255))return;
 }
 orb_retry=host.realtime+1;
 if(!orb_owned){
  int rc=gemGetRGB(wand,&orb_saved_r,&orb_saved_g,&orb_saved_b);
  if(rc){Cvar_FullSet("move_orb_status",va("Read orb color failed: 0x%08x",(unsigned)rc),FCVAR_READ_ONLY);return;}
  orb_owned=1;
 }
 int rc;
 if(optical){
  u8 compatible[360];int blue=-1;rc=gemGetAllTrackableHues(compatible);
  if(!rc)for(int delta=0;delta<=40 && blue<0;delta++){
   int a=240-delta,b=240+delta;
   if(a>=200 && compatible[a])blue=a;
   else if(b<=255 && compatible[b])blue=b;
  }
  if(rc || blue<0){Cvar_FullSet("move_orb_status","No trackable blue - retaining tracking color",FCVAR_READ_ONLY);return;}
  rc=orb_track(blue);
 }else rc=gemForceRGB(wand,0,0,1);
 if(rc){Cvar_FullSet("move_orb_status",va("Set blue orb failed: 0x%08x",(unsigned)rc),FCVAR_READ_ONLY);return;}
 orb_mode=optical?2:1;PS3_MoveCameraColorChanged();
 Cvar_FullSet("move_orb_status",optical?"Blue tracking hue active":"Blue orb active (gyro)",FCVAR_READ_ONLY);
}
static void move_status(const char *message){Cvar_FullSet("move_status",message,FCVAR_READ_ONLY);}
void halo_move_set_loading(int loading){
 loading=loading!=0;if(loading==map_loading)return;
 map_loading=loading;
 trace_frames=8;halo_move_trace_stage(loading?16:0);
 previous_time=0;previous_counter=-1;previous_poll=0;
 pending_yaw=pending_pitch=pending_turn=0;previous_buttons=0;
 memset(&gestures,0,sizeof(gestures));PS3_MoveCameraReset();
 Con_Printf("[Move load] %s; controller assignment retained\n",loading?"pause GEM polling":"resume GEM polling with fresh history");
}
static void move_retry(void){if(map_loading){move_status("Map loading - retry after loading finishes");return;}halo_move_shutdown();retry_at=0;move_status("Retry requested");}
void halo_move_register_commands(void){Cmd_AddCommand("move_retry",move_retry,"Retry Move initialization and pairing query");}
void PS3_MoveCursorAbsolute(float x,float y){(void)x;(void)y;}
int PS3_MoveFreeAimLocked(void){return value("move_freeaim") && !(clutch != (value("move_freeaim_invert")!=0));}
int halo_move_turning(void){return clutch != (value("move_freeaim_invert")!=0);}
void halo_move_calibrate(void){
 calibration_begin=host.realtime;calibration_until=host.realtime+8;
 sum_x=sum_y=0;calibration_samples=0;previous_time=0;
}
int halo_move_connected(void){return connected;}
unsigned halo_move_pad_mask(void){return pad_mask;}
void halo_move_rumble(int intensity){
 deferred_rumble=(int)bound(0,intensity,255);
 if(!map_loading && gem_ready && connected && wand>=0){halo_move_trace_stage(14);gemSetRumble(wand,(unsigned)deferred_rumble);halo_move_trace_stage(0);deferred_rumble=-1;}
}
void halo_move_shutdown(void){
 halo_move_trace_stage(17);
 orb_restore();
 PS3_MoveCameraShutdown();
 if(gem_ready){if(wand>=0)gemSetRumble(wand,0);gemEnd();}
 if(spurs_ready)spursFinalize(&spurs);
 if(gem_module)sysModuleUnload(SYSMODULE_GEM);
 if(spurs_module)sysModuleUnload(SYSMODULE_SPURS);
 gem_ready=spurs_ready=gem_module=spurs_module=connected=0;
 pad_mask=0;wand=-1;previous_time=0;pending_yaw=pending_pitch=pending_turn=0;previous_counter=-1;
 memset(&gestures,0,sizeof(gestures));halo_move_trace_stage(0);
}
static int start(void){
 gemAttribute attr;SpursAttribute sa;int rc;const char *stage="SPURS module";
 sys_ppu_thread_t caller;int ppu_priority=1001;
 attempted=1;
 halo_move_trace_stage(1);rc=sysModuleLoad(SYSMODULE_SPURS);if(rc<0)goto fail;spurs_module=1;
 stage="GEM module";halo_move_trace_stage(2);rc=sysModuleLoad(SYSMODULE_GEM);if(rc<0)goto fail;gem_module=1;
 /* Halo initializes all six SPUs itself before this optional SPURS group. */
 /* Use the calling game's priority, not the higher-priority display class.
  * Halo's blocking cache waits yield to peers; a runnable higher-priority
  * SPURS handler can prevent that yield from returning. Keep GEM a peer so
  * both its handler and Halo's cache/input threads get scheduled. */
 if(sysThreadGetId(&caller)==0)sysThreadGetPriority(caller,&ppu_priority);
 Con_Printf("[extensions] Move SPURS PPU priority %d (game peer)\n",ppu_priority);
 stage="SPURS attributes";halo_move_trace_stage(3);rc=spursAttributeInitialize(&sa,1,250,ppu_priority,1);if(rc)goto fail;
 stage="SPURS initialization";halo_move_trace_stage(4);rc=spursInitializeWithAttribute(&spurs,&sa);if(rc)goto fail;spurs_ready=1;
 memset(&attr,0,sizeof(attr));attr.version=MOVE_VERSION;attr.max=MAX_MOVES;
 attr.spurs=&spurs;attr.spu_priorities[0]=1;
 stage="GEM initialization";halo_move_trace_stage(5);rc=gemInit(&attr);if(rc)goto fail;gem_ready=1;
 halo_move_trace_stage(6);for(int i=0;i<MAX_MOVES;i++)gemReset(i);halo_move_trace_stage(0);
 move_status("Waiting for Move - pair USB, then press PS");return 1;
fail:
 Con_Printf("[extensions] Move %s failed 0x%x; pad controls available\n",stage,rc);
 halo_move_shutdown();move_status(va("%s failed: 0x%08x - move_retry",stage,(unsigned)rc));retry_at=host.realtime+10;return 0;
}
static void gyro(const gemInertialState *sample,float dt,int blocked,float *yaw,float *pitch){
 if(blocked || !previous_time)return;
 if(sample->counter==previous_counter && sample->time==previous_time)return;
 uint64_t stamp=sample->time,previous=previous_time;
 if((int64_t)stamp<=0 || stamp<=previous){
  if(dt<=0 || dt>.1f || sample->counter==previous_counter)return;
  PS3_MoveDelta(1,1+(uint64_t)(dt*1000000),sample->gyro[0]-sample->gyro_bias[0]-bias_x,
   sample->gyro[1]-sample->gyro_bias[1]-bias_y,value("move_sensitivity"),value("move_deadzone"),
   value("move_invert_y")!=0,0,yaw,pitch);
  return;
 }
 if(stamp-previous>250000)return;
 gemInertialState history[32];int count=0;
 uint64_t before=stamp,at=previous;
 while(count<32){
  gemInertialState *old=&history[count];
  if(gemGetInertialState(wand,GEM_INERTIAL_PREVIOUS,before-1,old))break;
  if(old->time>=before || old->time<=previous)break;
  before=old->time;count++;
 }
 for(int i=count-1;i>=-1;i--){
  const gemInertialState *old=i>=0?&history[i]:sample;
  float dy=0,dp=0;
  PS3_MoveDelta(at,old->time,old->gyro[0]-old->gyro_bias[0]-bias_x,
   old->gyro[1]-old->gyro_bias[1]-bias_y,value("move_sensitivity"),value("move_deadzone"),
   value("move_invert_y")!=0,0,&dy,&dp);
  *yaw+=dy;*pitch+=dp;at=old->time;
 }
}
static ps3_gesture_result_t gesture_step(const gemInertialState *sample,int blocked){
 float accel[3];for(int i=0;i<3;i++)accel[i]=sample->accelerometer[i]-sample->accelerometer_bias[i];
 return PS3_GestureStep(&gestures,sample->time,accel,
  sample->gyro[0]-sample->gyro_bias[0]-bias_x,sample->gyro[1]-sample->gyro_bias[1]-bias_y,
  sample->gyro[2]-sample->gyro_bias[2],!blocked && value("move_gestures"),value("move_gesture_shake")!=0,
  value("move_gesture_flick")!=0,value("move_gesture_shake_limit"),value("move_gesture_flick_limit"),value("move_gesture_cooldown"));
}
static ps3_gesture_result_t gesture_history(const gemInertialState *sample,int blocked){
 ps3_gesture_result_t result={0};
 if(!blocked && gestures.previous && sample->time>gestures.previous && sample->time-gestures.previous<=250000){
  gemInertialState history[32];int count=0;uint64_t before=sample->time;
  while(count<32){
   gemInertialState *old=&history[count];
   if(gemGetInertialState(wand,GEM_INERTIAL_PREVIOUS,before-1,old))break;
   if(old->time>=before || old->time<=gestures.previous)break;
   before=old->time;count++;
  }
  for(int i=count-1;i>=0;i--){ps3_gesture_result_t g=gesture_step(&history[i],blocked);result.shake|=g.shake;result.turn+=g.turn;}
 }
 ps3_gesture_result_t g=gesture_step(sample,blocked);
 result.locked=g.locked;result.shake|=g.shake;result.turn+=g.turn;return result;
}
void halo_move_frame(unsigned *buttons){
 *buttons=0;
 /* Do not remove/reassign a controller from a running match when its setting
  * changes. Start the merged Move/Nav route on the next game launch instead. */
 if(enabled_at_start<0)enabled_at_start=value("move_enable")!=0;
 if(!enabled_at_start){pending_yaw=pending_pitch=pending_turn=0;move_status(value("move_enable")?"Enable saved - restart game":"Disabled - set move_enable to 1, then restart");return;}
 if(map_loading){
  move_status("Map loading - motion polling paused");
  Cvar_FullSet("move_sensor","Paused during map loading; history resets on resume",FCVAR_READ_ONLY);
  return;
 }
 if(!gem_ready && (host.realtime<retry_at || !start()))return;
 (void)attempted;
 halo_move_trace_stage(7);PS3_MoveCameraPumpInertial();halo_move_trace_stage(0);
 gemInfo info;memset(&info,0,sizeof(info));
 halo_move_trace_stage(8);int info_rc=gemGetInfo(&info);halo_move_trace_stage(0);
 if(info_rc){move_status(va("Connection query failed: 0x%08x",(unsigned)info_rc));connected=0;pad_mask=0;previous_time=0;pending_yaw=pending_pitch=pending_turn=0;return;}
 int selected=wand;pad_mask=0;
 for(int i=0;i<MAX_MOVES;i++)if(info.status[i]==1 && info.port[i]>=1 && info.port[i]<=7)pad_mask|=1u<<(info.port[i]-1);
 if(selected<0 || info.status[selected]!=1){selected=-1;for(int i=0;i<MAX_MOVES;i++)if(info.status[i]==1){selected=i;break;}}
 if(selected<0){
  orb_restore();
  float y=0,p=0;PS3_MoveCameraFrame(-1,0,1,&y,&p);move_status("Waiting for Move - pair USB, then press PS");connected=0;wand=-1;previous_time=0;pending_yaw=pending_pitch=pending_turn=0;return;
 }
 if(selected!=wand){pending_yaw=pending_pitch=pending_turn=0;wand=selected;previous_time=0;previous_counter=-1;bias_x=bias_y=0;PS3_MoveCameraReset();}
 gemInertialState sample;memset(&sample,0,sizeof(sample));
 halo_move_trace_stage(9);int sensor_rc=gemGetInertialState(wand,GEM_INERTIAL_LATEST,0,&sample);halo_move_trace_stage(0);
 if(sensor_rc){move_status(va("Wand %d sensor read failed: 0x%08x",wand+1,(unsigned)sensor_rc));connected=0;previous_time=0;pending_yaw=pending_pitch=pending_turn=0;return;}
 connected=1;
 if(deferred_rumble>=0)halo_move_rumble(deferred_rumble);
 if(host.realtime>=sensor_report){
  Cvar_FullSet("move_sensor",va("W%d pkt %d gyro %.2f %.2f | %s",wand+1,sample.counter,
   sample.gyro[0]-sample.gyro_bias[0],sample.gyro[1]-sample.gyro_bias[1],
   host.realtime-last_look<=.2?"gameplay look active":"waiting for player 1 gameplay"),FCVAR_READ_ONLY);
  sensor_report=host.realtime+1;
 }
 Cvar_FullSet("move_status",!value("move_enable")?"Connected - disable saved for next launch":calibration_until?"Calibrating - hold still":"Connected",FCVAR_READ_ONLY);
 float dt=previous_poll>0?(float)(host.realtime-previous_poll):0;previous_poll=host.realtime;
 unsigned raw=sample.pad.buttons;
 if(sample.pad.ANA_T>=32)raw|=2;else if(sample.pad.ANA_T>16 && (previous_buttons&2))raw|=2;
 static const unsigned masks[]={2,64,128,32,16,8,1,4};
 static const char *bindings[]={"move_bind_t","move_bind_cross","move_bind_square","move_bind_circle","move_bind_triangle"};
 for(int i=0;i<5;i++)if(raw&masks[i])*buttons|=(unsigned)bound(0,value(bindings[i]),65535);
 if(raw&8)*buttons|=PS3_PAD_START;
 if(raw&1)*buttons|=PS3_PAD_SELECT;
 if(raw&4)*buttons|=(unsigned)bound(0,value("move_bind_move"),65535);
 int turn_button=(int)value("move_freeaim_turn_button")-1;
 clutch=turn_button>=0 && turn_button<8 && (raw&masks[turn_button]);
 if(clutch && turn_button<5)*buttons&=~(unsigned)value(bindings[turn_button]);
 if((raw&1) && !(previous_buttons&1) && !(raw&8))halo_move_calibrate();
 previous_buttons=raw;
 float gx=sample.gyro[0]-sample.gyro_bias[0],gy=sample.gyro[1]-sample.gyro_bias[1];
 if(calibration_until && (sample.counter!=previous_counter || sample.time!=previous_time)){
  if(host.realtime>=calibration_until)calibration_until=0;
  else if(!isfinite(gx) || !isfinite(gy) || fabsf(gx)>.15f || fabsf(gy)>.15f){
   calibration_begin=host.realtime;sum_x=sum_y=0;calibration_samples=0;
  }else{
   sum_x+=gx;sum_y+=gy;calibration_samples++;
   if(host.realtime-calibration_begin>=1 && calibration_samples>=10){
    bias_x=sum_x/calibration_samples;bias_y=sum_y/calibration_samples;calibration_until=0;
    Con_Printf("[extensions] Move drift calibrated: %.5f %.5f\n",bias_x,bias_y);
   }
  }
 }
 int blocked=halo_ext_menu() || host.realtime-last_look>.2 || calibration_until>0 || (clutch && !value("move_freeaim"));
 float yaw=0,pitch=0,optical_yaw=0,optical_pitch=0;
 halo_move_trace_stage(10);gyro(&sample,dt,blocked,&yaw,&pitch);halo_move_trace_stage(0);
 halo_move_trace_stage(11);ps3_gesture_result_t gesture=gesture_history(&sample,blocked);halo_move_trace_stage(0);
 if(gesture.shake){
  int action=(int)value("move_gesture_shake");
  *buttons|=action==1?PS3_PAD_SQUARE:action==2?PS3_PAD_TRIANGLE:action==3?PS3_PAD_SQUARE:action==4?PS3_PAD_CROSS:0;
 }
 halo_move_trace_stage(12);if(PS3_MoveCameraFrame(wand,0,blocked || gesture.locked,&optical_yaw,&optical_pitch)){
  yaw=optical_yaw;pitch=optical_pitch;
 }
 halo_move_trace_stage(13);orb_frame();halo_move_trace_stage(0);

 if(gesture.locked || blocked)yaw=pitch=0;
 if(blocked)pending_yaw=pending_pitch=pending_turn=0;
 else if(gesture.turn)pending_turn+=(float)gesture.turn*(float)bound(15,value("move_gesture_angle"),180)*.01745329252f;
 if(isfinite(yaw) && isfinite(pitch)){
  pending_yaw=(float)bound(-3.14159265f,pending_yaw+(float)bound(-180,yaw,180)*.01745329252f,3.14159265f);
  pending_pitch=(float)bound(-1.57079633f,pending_pitch-(float)bound(-90,pitch,90)*.01745329252f,1.57079633f);
 }
 previous_time=sample.time;previous_counter=sample.counter;halo_move_trace_stage(0);if(trace_frames)trace_frames--;
}
int halo_move_look(float *yaw,float *pitch){
 last_look=host.realtime;
 *yaw=pending_yaw;*pitch=pending_pitch;pending_yaw=pending_pitch=0;
 extern int halo_move_has_aim_offset(void);
 return connected || halo_move_has_aim_offset();
}

float halo_move_take_turn(void){float turn=pending_turn;pending_turn=0;return turn;}
