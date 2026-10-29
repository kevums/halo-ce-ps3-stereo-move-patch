/* Independent aim changes the simulation's aim while counter-rotating the render camera.
 * Movement is rotated back into camera space. All values use radians here. */
#include "extension_compat.h"
static float aim_yaw,aim_pitch;
static double active_at;
static int active;
static float horizontal_fov=1.57079633f,vertical_fov=1.04719755f;
extern int halo_move_turning(void);
void halo_move_apply_aim(float *yaw,float *pitch){
 if(!halo_move_connected() || !halo_ext_value("move_freeaim") || halo_ext_menu()){
  *yaw-=aim_yaw;*pitch-=aim_pitch;aim_yaw=aim_pitch=0;active=0;return;
 }

 active=1;active_at=host.realtime;
 if(halo_move_turning()){
  float gain=(float)bound(.5f,halo_ext_value("move_freeaim_turn_gain"),12);
  *yaw*=gain;*pitch*=gain;
 }else{
  float range=(float)bound(.25f,halo_ext_value("move_freeaim_range"),.9f);
  float max_yaw=atanf(tanf(horizontal_fov*.5f)*range);
  float max_pitch=atanf(tanf(vertical_fov*.5f)*range);
  float y=(float)bound(-max_yaw,aim_yaw+*yaw,max_yaw);
  float p=(float)bound(-max_pitch,aim_pitch+*pitch,max_pitch);
  *yaw=y-aim_yaw;*pitch=p-aim_pitch;aim_yaw=y;aim_pitch=p;
 }
}
int halo_move_has_aim_offset(void){return active;}
static int enabled(void){return active;}
void halo_move_camera(float *forward,float *up,float fovy,float aspect){
 vertical_fov=fovy;horizontal_fov=2*atanf(tanf(fovy*.5f)*aspect);
 if(!enabled())return;
 /* Halo's world Z is up. Undo aim yaw, then pitch around the camera's right axis. */
 float cy=cosf(-aim_yaw),sy=sinf(-aim_yaw);
 float x=forward[0]*cy-forward[1]*sy,y=forward[0]*sy+forward[1]*cy;
 forward[0]=x;forward[1]=y;
 x=up[0]*cy-up[1]*sy;y=up[0]*sy+up[1]*cy;up[0]=x;up[1]=y;
 float c=cosf(-aim_pitch),s=sinf(-aim_pitch);
 for(int i=0;i<3;i++){float f=forward[i],u=up[i];forward[i]=f*c+u*s;up[i]=u*c-f*s;}
}
void halo_move_throttle(float *forward,float *strafe){
 if(!enabled())return;
 float c=cosf(aim_yaw),s=sinf(aim_yaw),f=*forward,r=*strafe;
 *forward=f*c+r*s;*strafe=r*c-f*s;
}
void halo_move_reticle(float *x,float *y,float width,float height){
 if(!enabled())return;
 *x-=tanf(aim_yaw)/tanf(horizontal_fov*.5f)*width*.5f;
 *y-=tanf(aim_pitch)/tanf(vertical_fov*.5f)*height*.5f;
}
/* Halo point2d contains signed 16-bit coordinates, not real_point2d floats.
 * Copy through aligned floats; never write a float through a point2d member. */
void halo_move_reticle_point(int16_t *x,int16_t *y,float width,float height){
 float px=*x,py=*y;
 halo_move_reticle(&px,&py,width,height);
 if(isfinite(px))*x=(int16_t)bound(-32768,px,32767);
 if(isfinite(py))*y=(int16_t)bound(-32768,py,32767);
}
