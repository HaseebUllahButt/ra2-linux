#!/usr/bin/env python3
"""xmouse.py: move and click the mouse on an X display (XTest). usage: xmouse.py x y [click]"""
import ctypes, os, sys, time
x11 = ctypes.CDLL('libX11.so.6'); xt = ctypes.CDLL('libXtst.so.6')
x11.XOpenDisplay.restype = ctypes.c_void_p
d = x11.XOpenDisplay(os.environ.get('DISPLAY', ':77').encode())
if not d: sys.exit('no display')
d = ctypes.c_void_p(d)
x, y = int(sys.argv[1]), int(sys.argv[2])
xt.XTestFakeMotionEvent(d, -1, x, y, 0); x11.XFlush(d); time.sleep(0.3)
if len(sys.argv) > 3:
    xt.XTestFakeButtonEvent(d, 1, 1, 0); x11.XFlush(d); time.sleep(0.15)
    xt.XTestFakeButtonEvent(d, 1, 0, 0); x11.XFlush(d)
x11.XCloseDisplay(d)
