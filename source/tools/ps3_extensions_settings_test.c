#include "extension_compat.h"
#include "ps3_input.h"
#include <assert.h>
#include <stdlib.h>
static const char *folder;
const char *ps3_host_data_root(void){return folder;}
uint64_t ps3_host_microseconds(void){return 1000000;}
void ps3_host_log(const char *format,...){(void)format;}
void PS3_MoveCameraInit(void){}
void halo_move_register_commands(void){}
void halo_move_calibrate(void){}
void halo_move_frame(unsigned *buttons){*buttons=0;}
int halo_move_connected(void){return 0;}
unsigned halo_move_pad_mask(void){return 0;}
int main(int argc,char **argv){
 assert(argc==3);folder=argv[1];halo_ext_init();
 assert(halo_ext_value("move_enable")==strtof(argv[2],0));
 int switches=0;char row[256];
 for(int i=0;i<halo_ext_rows();i++){
  halo_ext_row(i,row,sizeof(row));
  if(!strncmp(row,"move_enable:",12))switches++;
  assert(strncmp(row,"move_enabled:",13));
 }
 assert(switches==1);puts("PASS: single Move enable switch and config migration");
}
