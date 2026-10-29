/* Small cvar/command adapter for the NSPS3 optical tracker. No engine ABI types cross this boundary. */
#ifndef EXTENSION_COMPAT_H
#define EXTENSION_COMPAT_H
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "halo_extensions.h"
#include "ps3_host.h"
typedef int qboolean;
#define true 1
#define false 0
#define FCVAR_ARCHIVE 1
#define FCVAR_READ_ONLY 2
#define bound(lo,v,hi) fmin(fmax((v),(lo)),(hi))
#define Q_strcmp strcmp
#define Con_Printf ps3_host_log
typedef struct {const char *name; float value; int flags; const char *description; char string[160];} ext_cvar;
#define CVAR_DEFINE_AUTO(name,def,flags,description) ext_cvar name = {#name,0,flags,description,def}
extern struct ext_clock { double realtime; } host;
void Cvar_RegisterVariable(ext_cvar *var);
void Cvar_FullSet(const char *name, const char *value, int flags);
void Cvar_SetValue(const char *name, float value);
float Cvar_VariableValue(const char *name);
void Cmd_AddCommand(const char *name, void (*function)(void), const char *description);
const char *va(const char *format, ...);
int PS3_MoveFreeAimLocked(void);
void PS3_MoveCursorAbsolute(float x, float y);
#endif
