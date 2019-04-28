/* z80context.h
 * Header with z80 context definition.
 * This code is free, do whatever you want with it.
 */

#ifndef __Z80CONTEXT_INCLUDED__
#define __Z80CONTEXT_INCLUDED__

#ifdef __cplusplus
extern "C" {
#endif

#include "z80emu.h"

typedef struct Z80_CONTEXT {

	Z80_STATE	state;
	unsigned char	memory[1 << 16];
	unsigned char	keyboard[8];
	unsigned char   border;
	unsigned char   ear;
	int             frame_counter;
	int 		is_done;
} Z80_CONTEXT;

__declspec(dllexport) void*  CreateContext ();
__declspec(dllexport) void   DestroyContext(void *context);
__declspec(dllexport) void   LoadZ80Format(void *context, unsigned char* data, int length);
__declspec(dllexport) void   KeyDown      (void *context, int key);
__declspec(dllexport) void   KeyUp        (void *context, int key);
__declspec(dllexport) int    RenderFrame  (void *context, unsigned char* frame, int length);
__declspec(dllexport) int    ReadMemory   (void *context, int offset);
__declspec(dllexport) int    WriteMemory  (void *context, int offset, unsigned char b);

#ifdef __cplusplus
}
#endif

#endif
