#!/usr/bin/env python3
"""Send XTest input to this process's hidden DISPLAY."""
import ctypes as C
import os
import sys
import time
x=C.CDLL('libX11.so.6'); t=C.CDLL('libXtst.so.6')
x.XOpenDisplay.argtypes=[C.c_char_p]; x.XOpenDisplay.restype=C.c_void_p
x.XFlush.argtypes=[C.c_void_p]; x.XCloseDisplay.argtypes=[C.c_void_p]
x.XStringToKeysym.argtypes=[C.c_char_p]; x.XStringToKeysym.restype=C.c_ulong
x.XKeysymToKeycode.argtypes=[C.c_void_p,C.c_ulong]; x.XKeysymToKeycode.restype=C.c_ubyte
t.XTestFakeMotionEvent.argtypes=[C.c_void_p,C.c_int,C.c_int,C.c_int,C.c_ulong]
t.XTestFakeButtonEvent.argtypes=[C.c_void_p,C.c_uint,C.c_int,C.c_ulong]
t.XTestFakeKeyEvent.argtypes=[C.c_void_p,C.c_uint,C.c_int,C.c_ulong]
d=x.XOpenDisplay(os.environ['DISPLAY'].encode())
if not d: sys.exit('No hidden display')
if sys.argv[1]=='click':
 t.XTestFakeMotionEvent(d,-1,int(sys.argv[2]),int(sys.argv[3]),0); x.XFlush(d); time.sleep(.2)
 button=int(sys.argv[4]) if len(sys.argv)>4 else 1
 t.XTestFakeButtonEvent(d,button,1,0); x.XFlush(d); time.sleep(.1); t.XTestFakeButtonEvent(d,button,0,0)
elif sys.argv[1]=='key':
 key=x.XKeysymToKeycode(d,x.XStringToKeysym(sys.argv[2].encode()))
 t.XTestFakeKeyEvent(d,key,1,0); x.XFlush(d); time.sleep(.1); t.XTestFakeKeyEvent(d,key,0,0)
x.XFlush(d); x.XCloseDisplay(d)
