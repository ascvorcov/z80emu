/* z80emu.c
 * Z80 processor emulator. 
 * This code is free, do whatever you want with it.
 */

#include <stdio.h>
#include <stdlib.h>
#include "z80emu.h"
#include "z80user.h"
#include "z80rom.h"

#define READWORD(buffer, address) (buffer[(address) & 0xffff] | (buffer[((address) + 1) & 0xffff] << 8))

void InitContext(Z80_CONTEXT *context)
{
  Z80Reset(&context->state);
  memset(context->memory, 0, sizeof(context->memory));
  memset(context->keyboard, 0xFF, sizeof(context->keyboard));
  memcpy(context->memory, z80_rom, sizeof(z80_rom));
  context->border = 0;
  context->ear = 0;
  context->frame_counter = 0;
  context->is_done = 0;
}

void* CreateContext()
{
  Z80_CONTEXT *context = malloc(sizeof(Z80_CONTEXT));
  InitContext(context);
  return context;
}

void DestroyContext(void* ptr)
{
  free(ptr);
}

void KeyDown(void* ptr, int k)
{
  Z80_CONTEXT *context = (Z80_CONTEXT*)ptr;
  if ((k >> 8) >= 8) return;
  context->keyboard[k >> 8] &= (unsigned char)(~k & 0xFF);
}

void KeyUp(void* ptr, int k)
{
  Z80_CONTEXT *context = (Z80_CONTEXT*)ptr;
  if ((k >> 8) >= 8) return;
  context->keyboard[k >> 8] |= (unsigned char)(k & 0xFF);
}

int ReadMemory(void *ptr, int offset)
{
  Z80_CONTEXT *context = (Z80_CONTEXT*)ptr;
  return context->memory[offset & 0xFFFF];
}

int WriteMemory(void *ptr, int offset, unsigned char b)
{
  Z80_CONTEXT *context = (Z80_CONTEXT*)ptr;
  int old = context->memory[offset & 0xFFFF];
  context->memory[offset & 0xFFFF] = b;
  return old;
}

void GetPixels(unsigned char bits, unsigned char color, int flash, unsigned char *data)
{
  int isFlash = (color & 0x80) != 0;
  int isBright = (color & 0x40) != 0;
  int colorPaper = (color >> 3) & 7;
  int colorInk = color & 7;

  for (int bit = 7; bit >= 0; bit--)
  {
    int set = (bits & (1 << bit)) != 0;

    int selected = isFlash && flash 
      ? (set ? colorPaper : colorInk) 
      : (set ? colorInk : colorPaper);

    data[7-bit] = (unsigned char)(isBright ? selected | 8 : selected);
  }
}

void CopyScreenLine(Z80_CONTEXT *context, int y, unsigned char *frame_buf)
{
  const int lineSize = 352;
  const int borderLR = 48;

  unsigned char *memory = context->memory;

  int offset = y * lineSize;
  if (y < 48 || y >= 240)
  {
    // upper/lower border part
    memset(frame_buf + offset, context->border, lineSize);
    return;
  }

  memset(frame_buf + offset, context->border, borderLR);
  memset(frame_buf + offset + lineSize - borderLR, context->border, borderLR);

  offset += borderLR; // reposition from border to screen
  
  // screen Y is different from absolute bitmap Y, and does not include border
  int y0 = y - 48; 

  // compute vertical offset, encoded as [Y7 Y6] [Y2 Y1 Y0] [Y5 Y4 Y3] [X4 X3 X2 X1 X0]
  int newY = (y0 & 0xC0) | ((y0 << 3) & 0x38) | ((y0 >> 3) & 7);
  int bitmapOffset = 0x4000 + (newY << 5);

  int colorInfoOffset = 0x5800 + y0 / 8 * 32;
  int flash = (context->frame_counter & 16) != 0 ? 1 : 0; // bit 4 is toggled every 16 frames

  unsigned char outBuffer[8];
  for (int chx = 0; chx < 32; chx++)
  {
    unsigned char bitmap = memory[bitmapOffset + chx];
    unsigned char color = memory[colorInfoOffset + chx];

    GetPixels(bitmap, color, flash, outBuffer);
    memcpy(frame_buf + offset + chx*8, outBuffer, 8);
  }
}

int RenderFrame(void *ptr, unsigned char* frame, int length)
{
  const int FRAMEBUF_SZ = 352*312;

  Z80_CONTEXT *context = (Z80_CONTEXT*)ptr;

  if (length < FRAMEBUF_SZ)
    return FRAMEBUF_SZ - length; 

  // a frame is (64+192+56)*224=69888 T states long 
  // (224 tstates per line = 64/56 upper/lower border + 192 screen).
  // for simplicity, we copy 1 line every 224 ticks.
  // actual resolution is 256x192 main screen, l/r border is 48 pixels wide,
  // upper/lower border is 64/56 pixels high, giving total of 352x312

  // starts from v-sync interrupt,
  // reading memory line every 224 ticks
  Z80_STATE *state = &context->state;

  int diff = Z80Interrupt (state, 0, context);

  for (int y = 0; y < 312; ++y)
  {
    int ticksEmulated = Z80Emulate (state, 224 - diff, context);
    diff = ticksEmulated - 224; // if 225 ticks were emulated instead of 224 - next cycle will ask to emulate 223
    CopyScreenLine(context, y, frame);
  }

  context->frame_counter++;
  return 0;
}

int SystemInput(Z80_CONTEXT *context, int port)
{
  if ((port & 0xFF) != 0xFE)
    return 0xFF;

  unsigned char ret = 0xFF;
  unsigned char highPart = (port >> 8) & 0xFF;

  if ((highPart & 1) == 0) ret &= context->keyboard[0];
  if ((highPart & 2) == 0) ret &= context->keyboard[1];
  if ((highPart & 4) == 0) ret &= context->keyboard[2];
  if ((highPart & 8) == 0) ret &= context->keyboard[3];
  if ((highPart & 16) == 0) ret &= context->keyboard[4];
  if ((highPart & 32) == 0) ret &= context->keyboard[5];
  if ((highPart & 64) == 0) ret &= context->keyboard[6];
  if ((highPart & 128) == 0) ret &= context->keyboard[7];
  return ret;
}

void SystemOutput(Z80_CONTEXT *context, int port, int value)
{
  if ((port & 0xFF) != 0xFE)
    return;

  context->border = value & 7;
  context->ear = value & 0x10;
}

unsigned short GetPage(unsigned char page)
{
  switch(page)
  {
    case 0: return 0; // rom
    case 4: return 0x8000;
    case 5: return 0xc000;
    case 8: return 0x4000;
    default: return 0; // not supported
  }
}

unsigned short UnpackMem(Z80_CONTEXT *context, unsigned short offset, unsigned char* data, int start, int end, int compressed)
{
  unsigned char* memory = context->memory;
  for (int i = start; i < end; ++i)
  {
    if (compressed && 
      data[i+0] == 0x00 && 
      data[i+1] == 0xED && 
      data[i+2] == 0xED && 
      data[i+3] == 0x00)
      break;

    if (data[i] == 0xED && data[i+1] == 0xED && compressed)
    {
      unsigned char repeat = data[i+2];
      unsigned char value = data[i+3];
      while(repeat-->0)
      {
        memory[offset++] = value;
      }

      i = i + 3;
    }
    else
    {
      memory[offset++] = data[i];
    }
  }
  return offset;
}

void ReadV2Format(Z80_CONTEXT *context, unsigned char* data, int length)
{
  Z80_STATE* state = &context->state;
  int len = READWORD(data, 30);
  state->pc = READWORD(data, 32);
  int i = 32 + len;

  while (i != length)
  {
    int datalen = READWORD(data, i);
    int page = GetPage(data[i + 2]);

    i = i + 3; // skip block header

    int compressed = 1;
    if (datalen == 0xFFFF)
    {
        datalen = 16384;
        compressed = 0;
    }

    UnpackMem(context, page, data, i, i + datalen, compressed);
    
    i += datalen;
  }
}

void LoadZ80Format(void* ptr, unsigned char* data, int length)
{
  Z80_CONTEXT *context = (Z80_CONTEXT *)ptr;
  Z80_STATE* state = &context->state;
  InitContext(context);

  state->registers.byte[Z80_A] = data[0];
  state->registers.byte[Z80_F] = data[1];
  state->registers.word[Z80_BC] = READWORD(data, 2);
  state->registers.word[Z80_HL] = READWORD(data, 4);
  state->registers.word[Z80_SP] = READWORD(data, 8);
  state->registers.word[Z80_DE] = READWORD(data, 13);
  state->pc = READWORD(data, 6);
  state->i = data[10];

  unsigned char bitinfo = data[12] == 255 ? 1 : data[12];

  state->r = (unsigned char)((data[11] & 127) | (bitinfo << 7));
  context->border = (bitinfo >> 1) & 7;

  state->alternates[Z80_BC] = READWORD(data, 15);
  state->alternates[Z80_DE] = READWORD(data, 17);
  state->alternates[Z80_HL] = READWORD(data, 19);
  state->alternates[Z80_AF] = (data[21] << 8) | data[22];

  state->registers.word[Z80_IY] = READWORD(data, 23);
  state->registers.word[Z80_IX] = READWORD(data, 25);

  state->iff1 = data[27] != 0 ? 1 : 0;
  state->iff2 = data[28] != 0 ? 1 : 0;
  state->im = data[29] & 3;

  if (state->pc == 0)
    ReadV2Format(context, data, length);
  else
    UnpackMem(context, 0x4000, data, 30, length, (bitinfo & 32) != 0 ? 1 : 0);
}

/*int main()
{
  Z80_CONTEXT* context = CreateContext();
  printf("success\r\n");
  unsigned char frame[352*312];
  while(!context->is_done)
  {
    int res = RenderFrame(context, frame, sizeof(frame));
    //printf("%d %X\r\n", res, context->state.pc);
    KeyDown(context, 0x501);
    res = RenderFrame(context, frame, sizeof(frame));
    //printf("%d %X\r\n", res, context->state.pc);
    KeyUp(context, 0x501);

    for (int y = 64; y < 312-56; y += 4)
    {
      printf("\r\n");
      for (int x = 48; x < 352-48; x += 4)
      {
        int color = (1+frame[y*312 + x])/4; // one of 16 colors
        printf("%d",color);
      }
    }
  }
  DestroyContext(context);
}*/