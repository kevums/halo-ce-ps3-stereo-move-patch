#include "extension_compat.h"
#include "ps3_input.h"
#include "ps3_move_camera.h"
#include <stdarg.h>
#include <stdlib.h>
#include <errno.h>

struct ext_clock host;
static ext_cvar *vars[128];
static int count, initialized, menu, selected;
static struct { const char *name; void (*run)(void); const char *description; } commands[24];
static int command_count;
static unsigned previous, previous_chord;
static double repeat_at;
static struct ps3_pad virtual_pad, hidden;
static int virtual_port=-1, navigation_port=-1;
#define EXT_SETTING(name,def,description) {name,0,FCVAR_ARCHIVE,description,def},
static ext_cvar settings[] = {
 {"move_status",0,FCVAR_READ_ONLY,"Move sensor status","Disabled"},
 {"move_sensor",0,FCVAR_READ_ONLY,"Motion samples and gameplay input route","No motion samples yet"},
 {"move_orb_status",0,FCVAR_READ_ONLY,"Optional blue orb status","Blue orb off"},
#include "settings.inc"
};
#undef EXT_SETTING

static ext_cvar *find(const char *name) {
 for(int i=0;i<count;i++)if(!strcmp(vars[i]->name,name))return vars[i];
 return NULL;
}
void Cvar_RegisterVariable(ext_cvar *var) {
 if(find(var->name) || count==128)return;
 var->value=strtof(var->string,NULL); vars[count++]=var;
}
void Cvar_FullSet(const char *name,const char *value,int flags) {
 (void)flags;ext_cvar *v=find(name);if(!v)return;
 snprintf(v->string,sizeof(v->string),"%s",value);v->value=strtof(value,NULL);
 if(!isfinite(v->value))v->value=0;
}
void Cvar_SetValue(const char *name,float value) {
 if(!isfinite(value))return;
 char s[40];snprintf(s,sizeof(s),"%.7g",value);Cvar_FullSet(name,s,0);
}
float Cvar_VariableValue(const char *name){ext_cvar *v=find(name);return v?v->value:0;}
float halo_ext_value(const char *name){
 halo_ext_init();float v=Cvar_VariableValue(name);
 if(!strcmp(name,"r_stereo_depth"))return (float)bound(0,v,3);
 if(!strcmp(name,"r_stereo_convergence"))return (float)bound(.5f,v,100);
 return v;
}
void halo_ext_set(const char *name,float value){Cvar_SetValue(name,value);}
void Cmd_AddCommand(const char *name,void (*run)(void),const char *description) {
 if(command_count<24){commands[command_count].name=name;commands[command_count].run=run;commands[command_count++].description=description;}
}
const char *va(const char *format,...) {
 static char buffers[4][256];static unsigned slot;char *b=buffers[slot++%4];
 va_list a;va_start(a,format);vsnprintf(b,256,format,a);va_end(a);return b;
}
static void save(void) {
 char path[512],temp[520];snprintf(path,sizeof(path),"%s/extensions.cfg",ps3_host_data_root());
 snprintf(temp,sizeof(temp),"%s.tmp",path);FILE *f=fopen(temp,"w");
 if(!f){Con_Printf("[extensions] cannot write %s: %d\n",temp,errno);return;}
 fprintf(f,"# Halo PS3 Stereo + Move extension. Select+Start opens settings.\n");
 for(int i=0;i<count;i++)if(vars[i]->flags&FCVAR_ARCHIVE)fprintf(f,"%s %s\n",vars[i]->name,vars[i]->string);
 int ok=!ferror(f);if(fclose(f))ok=0;
 if(!ok || rename(temp,path))Con_Printf("[extensions] save failed; previous settings kept\n");
}
void halo_ext_init(void) {
 if(initialized)return;
 initialized=1;
 for(unsigned i=0;i<sizeof(settings)/sizeof(settings[0]);i++)Cvar_RegisterVariable(&settings[i]);
 PS3_MoveCameraInit();
 Cvar_SetValue("move_camera_stability",2);
 Cmd_AddCommand("move_calibrate",halo_move_calibrate,"Hold wand still for drift calibration");
 {extern void halo_move_register_commands(void);halo_move_register_commands();}
 int have_enable=0,have_legacy=0;float legacy_enable=0;
 char path[512],line[512],key[96],value[160];
 snprintf(path,sizeof(path),"%s/extensions.cfg",ps3_host_data_root());FILE *f=fopen(path,"r");
 if(f){while(fgets(line,sizeof(line),f))if(line[0]!='#' && sscanf(line,"%95s %159s",key,value)==2){
   ext_cvar *v=find(key);char *end;float n=strtof(value,&end);
   if(!*end && isfinite(n)){
    if(!strcmp(key,"move_enable"))have_enable=1;
    if(!strcmp(key,"move_enabled")){have_legacy=1;legacy_enable=n;}
   }
   if(v && (v->flags&FCVAR_ARCHIVE) && !*end && isfinite(n))Cvar_FullSet(key,value,0);
  }fclose(f);}
 if(!have_enable && have_legacy)Cvar_SetValue("move_enable",legacy_enable);
 memset(&hidden,0,sizeof(hidden));memset(hidden.stick,128,sizeof(hidden.stick));
 Con_Printf("[extensions] Stereo + Move extension loaded; Select+Start for settings\n");
}
int halo_ext_stereo_mode(void){
 static int mode=-1;
 if(mode<0){mode=halo_ext_value("r_stereo")?(int)bound(1,halo_ext_value("r_stereo_mode"),4):0;}
 return mode;
}
int halo_ext_menu(void){return menu;}
int halo_ext_rows(void){return count+command_count;}
int halo_ext_selected(void){return selected;}
void halo_ext_row(int row,char *text,uint32_t size){
 if(row<0 || row>=count+command_count){if(size)text[0]=0;return;}
 if(row<count)snprintf(text,size,"%s: %s",vars[row]->name,vars[row]->string);
 else snprintf(text,size,"[ %s ]",commands[row-count].name);
}
static void adjust(int direction){
 if(selected>=count){if(direction>0)commands[selected-count].run();return;}
 ext_cvar *v=vars[selected];if(v->flags&FCVAR_READ_ONLY)return;
 float step=1,low=0,high=1;
 if(strstr(v->name,"sensitivity")){step=.05f;low=.25f;high=8;}
 else if(strstr(v->name,"deadzone")){step=.005f;high=.2f;}
 else if(strstr(v->name,"gain")){step=.25f;low=.5f;high=12;}
 else if(strstr(v->name,"range") || strstr(v->name,"zone")){step=.05f;low=.25f;high=.9f;}
 else if(strstr(v->name,"angle")){step=15;low=15;high=180;}
 else if(strstr(v->name,"turn_speed")){step=10;low=30;high=360;}
 else if(strstr(v->name,"limit")){step=.1f;low=.3f;high=8;}
 else if(strstr(v->name,"cooldown")){step=.1f;low=.3f;high=3;}
 else if(strstr(v->name,"steadiness")){step=.25f;high=3;}
 else if(strstr(v->name,"_port")){high=7;}
 else if(strstr(v->name,"_button")){high=8;}
 else if(strstr(v->name,"gesture_shake")){high=4;}
 else if(!strcmp(v->name,"r_stereo_mode")){low=1;high=4;}
 else if(!strcmp(v->name,"r_stereo_depth")){step=.05f;high=3;}
 else if(!strcmp(v->name,"r_stereo_convergence")){step=.5f;low=.5f;high=100;}
 else if(strstr(v->name,"move_bind_")){
  static const unsigned masks[]={0,512,32768,16384,8192,4096,1024,2048,256,8,1,2,4};
  int index=0;for(unsigned i=0;i<sizeof(masks)/sizeof(masks[0]);i++)if(masks[i]==(unsigned)v->value)index=i;
  index=(index+direction+13)%13;Cvar_SetValue(v->name,(float)masks[index]);return;
 }
 Cvar_SetValue(v->name,(float)bound(low,v->value+direction*step,high));
}
void halo_ext_poll(struct ps3_pad *pads){
 halo_ext_init();host.realtime=ps3_host_microseconds()*.000001;
 unsigned wand_buttons=0;halo_move_frame(&wand_buttons);
 unsigned mask=halo_move_pad_mask();int nav=-1;
 int requested=(int)Cvar_VariableValue("move_nav_port")-1;
 for(int p=0;p<PS3_PAD_PORTS;p++)if(pads[p].connected && !(mask&(1u<<p)) && (requested<0 || requested==p)){nav=p;break;}
 memset(&virtual_pad,0,sizeof(virtual_pad));memset(virtual_pad.stick,128,sizeof(virtual_pad.stick));
 virtual_port=-1;navigation_port=-1;
 if(halo_move_connected()){
  virtual_port=0;navigation_port=nav;
  if(nav>=0 && Cvar_VariableValue("move_nav"))virtual_pad=pads[nav];
  virtual_pad.connected=1;virtual_pad.buttons|=wand_buttons;
 }
 unsigned buttons=wand_buttons;
 for(int p=0;p<PS3_PAD_PORTS;p++)if(!(mask&(1u<<p)))buttons|=pads[p].buttons;
 unsigned chord=(buttons&(PS3_PAD_SELECT|PS3_PAD_START))==(PS3_PAD_SELECT|PS3_PAD_START);
 if(chord && !previous_chord){menu=!menu;previous=buttons;if(!menu)save();}
 previous_chord=chord;
 if(menu){
  unsigned press=buttons&~previous;
  if(buttons!=previous)repeat_at=host.realtime+.4;
  else if(host.realtime>=repeat_at){press=buttons;repeat_at=host.realtime+.12;}
  if(press&PS3_PAD_UP)selected=(selected+halo_ext_rows()-1)%halo_ext_rows();
  if(press&PS3_PAD_DOWN)selected=(selected+1)%halo_ext_rows();
  if(press&PS3_PAD_LEFT)adjust(-1);
  if(press&(PS3_PAD_RIGHT|PS3_PAD_CROSS))adjust(1);
  if(press&PS3_PAD_CIRCLE){menu=0;save();}
 }
 previous=buttons;
}
const struct ps3_pad *halo_ext_pad(int port,const struct ps3_pad *pad){
 if(menu){static struct ps3_pad quiet;quiet=port==virtual_port?virtual_pad:*pad;quiet.buttons=0;memset(quiet.stick,128,sizeof(quiet.stick));memset(quiet.pressure,0,sizeof(quiet.pressure));return &quiet;}
 if(port==virtual_port)return &virtual_pad;
 if((halo_move_pad_mask()&(1u<<port)) || port==navigation_port)return &hidden;
 return pad;
}
