#!/usr/bin/env python3
"""xmouse.py: move and click the mouse on an X display (XTest). usage: xmouse.py x y [click|rclick]
   or: xmouse.py key <keysym>   (copied from ../../tools/xmouse.py, key support added)"""
import ctypes, os, sys, time
x11 = ctypes.CDLL('libX11.so.6'); xt = ctypes.CDLL('libXtst.so.6')
x11.XOpenDisplay.restype = ctypes.c_void_p
d = x11.XOpenDisplay(os.environ.get('DISPLAY', ':77').encode())
if not d: sys.exit('no display')
d = ctypes.c_void_p(d)
if sys.argv[1] == 'key':
    x11.XStringToKeysym.restype = ctypes.c_ulong
    ks = x11.XStringToKeysym(sys.argv[2].encode()); kc = x11.XKeysymToKeycode(d, ctypes.c_ulong(ks))
    xt.XTestFakeKeyEvent(d, kc, 1, 0); x11.XFlush(d); time.sleep(0.1)
    xt.XTestFakeKeyEvent(d, kc, 0, 0); x11.XFlush(d); x11.XCloseDisplay(d); sys.exit(0)
x, y = int(sys.argv[1]), int(sys.argv[2])
xt.XTestFakeMotionEvent(d, -1, x, y, 0); x11.XFlush(d); time.sleep(0.3)
if len(sys.argv) > 3:
    b = 3 if sys.argv[3] == 'rclick' else 1
    xt.XTestFakeButtonEvent(d, b, 1, 0); x11.XFlush(d); time.sleep(0.15)
    xt.XTestFakeButtonEvent(d, b, 0, 0); x11.XFlush(d)
x11.XCloseDisplay(d)
