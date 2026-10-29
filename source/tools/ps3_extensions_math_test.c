#include "extension_compat.h"
#include "ps3_move_math.h"
#include "ps3_move_gestures.h"
#include <assert.h>
#include <stdlib.h>
struct ext_clock host;
static int turning,connected=1,menu;
float halo_ext_value(const char *name){
 if(!strcmp(name,"move_freeaim"))return 1;
 if(!strcmp(name,"move_freeaim_range"))return .7f;
 if(!strcmp(name,"move_freeaim_turn_gain"))return 5.5f;
 return 0;
}
int halo_move_connected(void){return connected;}
int halo_ext_menu(void){return menu;}
int halo_move_turning(void){return turning;}
extern void halo_move_apply_aim(float *,float *);
extern void halo_move_camera(float *,float *,float,float);
extern void halo_move_throttle(float *,float *);
extern void halo_move_reticle(float *,float *,float,float);
extern void halo_move_reticle_point(int16_t *,int16_t *,float,float);
static void near(float a,float b){assert(fabsf(a-b)<.0001f);}
int main(void){
 float y,p;
 assert(!PS3_MoveDelta(0,10000,1,1,1.5,.05,0,0,&y,&p));near(y,0);near(p,0);
 assert(!PS3_MoveDelta(1,200000,1,1,1.5,.05,0,0,&y,&p));
 assert(!PS3_MoveDelta(1,10001,NAN,1,1.5,.05,0,0,&y,&p));
 assert(!PS3_MoveDelta(1,10001,1,1,1.5,.05,0,1,&y,&p));
 assert(PS3_MoveDelta(1,10001,0,1,1.5,.05,0,0,&y,&p));near(y,.95f*.01f*57.2957795f*1.5f);
 assert(PS3_MoveDelta(1,10001,.01,.01,1.5,.05,0,0,&y,&p));near(y,0);near(p,0);
 host.realtime=1;y=.1f;p=.05f;halo_move_apply_aim(&y,&p);near(y,.1);near(p,.05);
 float forward[3]={cosf(.05f)*cosf(.1f),cosf(.05f)*sinf(.1f),sinf(.05f)};
 float up[3]={-sinf(.05f)*cosf(.1f),-sinf(.05f)*sinf(.1f),cosf(.05f)};
 halo_move_camera(forward,up,1.04719755f,1.77777778f);near(forward[0],1);near(forward[1],0);near(forward[2],0);
 float f=1,s=0;halo_move_throttle(&f,&s);near(f*f+s*s,1);
 float x=0,z=0;halo_move_reticle(&x,&z,852,480);assert(x<0 && z<0);
 struct {int16_t before,x,y,after;} point={1234,0,0,2345};
 halo_move_reticle_point(&point.x,&point.y,852,480);
 assert(point.before==1234 && point.after==2345);
 assert(point.x==(int16_t)x && point.y==(int16_t)z);
 point.x=point.y=-32768;halo_move_reticle_point(&point.x,&point.y,852,480);
 assert(point.x==-32768 && point.y==-32768);
 assert(point.before==1234 && point.after==2345);

 turning=1;y=.01f;p=.02f;host.realtime+=.03;halo_move_apply_aim(&y,&p);near(y,.055);near(p,.11);
 turning=0;y=100;p=100;host.realtime+=.03;halo_move_apply_aim(&y,&p);assert(fabsf(y)<1 && fabsf(p)<1);
 menu=1;y=0;p=0;halo_move_apply_aim(&y,&p);assert(y<0 && p<0);x=z=0;halo_move_reticle(&x,&z,852,480);near(x,0);near(z,0);
 /* Parallel eye cameras + asymmetric projection agree at convergence, with no toe-in. */
 for(int eye=-1;eye<=1;eye+=2){float separation=eye*.0325f*.5f,convergence=10,tangent=1;
  float shift=-separation/(convergence*tangent),projected=-separation/(convergence*tangent)-shift;near(projected,0);}
 ps3_gesture_state_t state={0};float acceleration[3]={0,0,1};
 ps3_gesture_result_t g=PS3_GestureStep(&state,1,acceleration,0,0,0,1,1,1,.5,3,.8);assert(!g.shake && !g.turn);
 g=PS3_GestureStep(&state,1,acceleration,0,0,0,1,1,1,.5,3,.8);assert(!g.shake && !g.turn);
 acceleration[0]=NAN;g=PS3_GestureStep(&state,20000,acceleration,0,0,0,1,1,1,.5,3,.8);assert(!g.shake && !g.turn && !state.previous);
 memset(&state,0,sizeof(state));acceleration[0]=0;int turns=0;
 for(int i=0;i<24;i++)PS3_GestureStep(&state,1+i*10000,acceleration,0,0,0,1,0,1,.5,3,.8);
 for(int i=24;i<32;i++){g=PS3_GestureStep(&state,1+i*10000,acceleration,0,4,0,1,0,1,.5,3,.8);if(g.turn){assert(g.locked);turns+=g.turn;}}
 assert(turns==1);
 puts("PASS: gyro freshness, deadzone, clutch, freeaim/camera cancellation, reticle direction, movement magnitude, convergence, gesture invalid-data reset");
}
