#ifndef HALO_EXTENSIONS_H
#define HALO_EXTENSIONS_H
#include <stdint.h>
struct ps3_pad;
void halo_ext_init(void);
void halo_ext_poll(struct ps3_pad *pads);
const struct ps3_pad *halo_ext_pad(int port, const struct ps3_pad *pad);
int halo_ext_menu(void);
int halo_ext_rows(void);
int halo_ext_selected(void);
void halo_ext_row(int row, char *text, uint32_t size);
float halo_ext_value(const char *name);
void halo_ext_set(const char *name, float value);
int halo_ext_stereo_mode(void);
void halo_move_shutdown(void);
void halo_move_frame(unsigned *buttons);
void halo_move_set_loading(int loading);
void halo_move_trace_stage(int stage);
void halo_move_trace_report(void);
int halo_move_connected(void);
unsigned halo_move_pad_mask(void);
int halo_move_look(float *yaw, float *pitch);
void halo_move_calibrate(void);
void halo_ext_draw_menu(void);
void halo_gpu_capture_eye(int eye, uint32_t offset, uint32_t pitch, uint32_t width, uint32_t height);
#endif
