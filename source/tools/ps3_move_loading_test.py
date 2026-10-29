"""Exercise production Move polling against a fake SDK: no GEM calls while loading."""
from pathlib import Path
import re,subprocess,tempfile
root=Path(__file__).resolve().parents[1]
sdk=r'''
#ifndef TEST_GEM_H
#define TEST_GEM_H
#include <stdint.h>
#include <string.h>
typedef uint64_t sys_ppu_thread_t;
typedef uint8_t u8;typedef uint16_t u16;typedef uint32_t u32;
typedef struct {char data[128];} Spurs;
typedef struct {char data[128];} SpursAttribute;
typedef struct {u32 version,max;Spurs *spurs;u8 spu_priorities[8];} gemAttribute;
typedef struct {u32 status[4],port[4];} gemInfo;
typedef struct {u16 buttons,ANA_T;char reserved[20];} test_pad;
typedef struct {float accelerometer[4],gyro[4],accelerometer_bias[4],gyro_bias[4];test_pad pad;uint64_t time;int counter;} gemInertialState;
#define MAX_MOVES 4
#define MOVE_VERSION 2
#define SYSMODULE_SPURS 1
#define SYSMODULE_GEM 2
#define GEM_INERTIAL_LATEST 0
#define GEM_INERTIAL_PREVIOUS 1
extern int sdk_calls,last_rumble,spurs_ppu_priority;
static inline int sysThreadGetId(sys_ppu_thread_t *t){*t=1;return 0;}
static inline int sysThreadGetPriority(sys_ppu_thread_t t,int *p){(void)t;*p=1001;return 0;}
static inline int gemTrackHues(u32 *h,void *p){(void)h;(void)p;sdk_calls++;return 0;}
static inline int gemGetRGB(int n,float *r,float *g,float *b){(void)n;sdk_calls++;*r=*g=*b=1;return 0;}
static inline int gemForceRGB(int n,float r,float g,float b){(void)n;(void)r;(void)g;(void)b;sdk_calls++;return 0;}
static inline int gemGetTrackerHue(int n,u32 *h){(void)n;sdk_calls++;*h=240;return 0;}
static inline int gemGetAllTrackableHues(u8 *h){sdk_calls++;memset(h,1,360);return 0;}
static inline int gemSetRumble(int n,unsigned value){(void)n;sdk_calls++;last_rumble=(int)value;return 0;}
static inline int gemEnd(void){sdk_calls++;return 0;}
static inline int spursFinalize(Spurs *p){(void)p;sdk_calls++;return 0;}
static inline int sysModuleUnload(int n){(void)n;sdk_calls++;return 0;}
static inline int sysModuleLoad(int n){(void)n;sdk_calls++;return 0;}
static inline int spursAttributeInitialize(SpursAttribute *p,int a,int b,int c,int d){(void)p;(void)a;(void)b;(void)d;spurs_ppu_priority=c;sdk_calls++;return 0;}
static inline int spursInitializeWithAttribute(Spurs *p,SpursAttribute *a){(void)p;(void)a;sdk_calls++;return 0;}
static inline int gemInit(gemAttribute *p){(void)p;sdk_calls++;return 0;}
static inline int gemReset(int n){(void)n;sdk_calls++;return 0;}
static inline int gemGetInfo(gemInfo *p){sdk_calls++;memset(p,0,sizeof(*p));p->status[0]=1;p->port[0]=1;return 0;}
static inline int gemGetInertialState(int n,int mode,uint64_t time,gemInertialState *p){
 (void)n;(void)time;sdk_calls++;if(mode==GEM_INERTIAL_PREVIOUS)return 1;
 static int packet;memset(p,0,sizeof(*p));p->counter=++packet;p->time=packet*10000;p->accelerometer[2]=1;return 0;
}
#endif
'''
harness=r'''
#include "extension_compat.h"
#include "test_gem.h"
#include <assert.h>
#include <stdarg.h>
int sdk_calls,last_rumble,spurs_ppu_priority;
struct ext_clock host;
static char status[160];
float Cvar_VariableValue(const char *n){return !strcmp(n,"move_enable")?1:0;}
void Cvar_FullSet(const char *n,const char *v,int flags){(void)flags;if(!strcmp(n,"move_status"))snprintf(status,sizeof(status),"%s",v);}
uint64_t ps3_host_microseconds(void){return (uint64_t)(host.realtime*1000000);}
void ps3_host_log(const char *s,...){(void)s;}
void Cmd_AddCommand(const char *n,void (*f)(void),const char *d){(void)n;(void)f;(void)d;}
const char *va(const char *s,...){static char b[256];va_list a;va_start(a,s);vsnprintf(b,sizeof(b),s,a);va_end(a);return b;}
int halo_ext_menu(void){return 0;}
int halo_move_has_aim_offset(void){return 0;}
void PS3_MoveCameraReset(void){}
void PS3_MoveCameraColorChanged(void){}
void PS3_MoveCameraShutdown(void){sdk_calls++;}
int PS3_MoveCameraCalibrating(void){return 0;}
void PS3_MoveCameraPumpInertial(void){sdk_calls++;}
int PS3_MoveCameraFrame(int n,int cursor,int blocked,float *y,float *p){(void)n;(void)cursor;(void)blocked;(void)y;(void)p;sdk_calls++;return 0;}
extern void halo_move_rumble(int);
int main(void){
 unsigned buttons;float yaw,pitch;host.realtime=1;
 halo_move_set_loading(1);halo_move_frame(&buttons);halo_move_rumble(80);
 assert(sdk_calls==0);assert(strstr(status,"loading"));assert(!halo_move_connected());
 halo_move_set_loading(0);halo_move_frame(&buttons);assert(halo_move_connected());assert(last_rumble==80);assert(spurs_ppu_priority==1001);
 unsigned mask=halo_move_pad_mask();assert(mask==1);halo_move_look(&yaw,&pitch);
 int before=sdk_calls;
 halo_move_set_loading(1);halo_move_frame(&buttons);halo_move_rumble(120);halo_move_frame(&buttons);
 assert(sdk_calls==before);assert(halo_move_connected());assert(halo_move_pad_mask()==mask);
 halo_move_set_loading(0);host.realtime+=1;halo_move_frame(&buttons);
 assert(sdk_calls>before);assert(last_rumble==120);halo_move_look(&yaw,&pitch);assert(yaw==0 && pitch==0);
 puts("PASS: no SDK calls during loading; controller preserved; deferred rumble and fresh-history resume");
}
'''
with tempfile.TemporaryDirectory(prefix='halo-move-loading-') as temporary:
    work=Path(temporary)
    production=(root/'port/ps3/extensions/extension_move.c').read_text()
    production=re.sub(r'#include <(?:io/move|spurs/spurs|sys/spu|sys/thread|sysmodule/sysmodule)\.h>','',production)
    (work/'move.c').write_text('#include "test_gem.h"\n'+production)
    (work/'test_gem.h').write_text(sdk);(work/'test.c').write_text(harness)
    binary=work/'test'
    subprocess.run(['gcc','-std=c99','-Wall','-Wextra','-fsanitize=address,undefined','-I'+str(work),
        '-I'+str(root/'port/ps3/extensions'),'-I'+str(root/'port/ps3/src'),str(work/'move.c'),str(work/'test.c'),'-lm','-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
