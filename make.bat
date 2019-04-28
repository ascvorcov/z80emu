call "C:\Program Files (x86)\Microsoft Visual Studio 14.0\VC\vcvarsall.bat" amd64

del *.obj
del *.lib
del *.dll
del *.exp

cl /LD z80user.c z80emu.c /Fe: z80native.dll

