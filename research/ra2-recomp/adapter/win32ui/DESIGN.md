# win32ui: window and dialog code (J14)

Our own USER32, GDI32, COMCTL32 and IMM32 for game.exe 1.006, on SDL2 and C only.
The same source builds as `ra2ui.dll` (tested under the original game on Wine) and later as a Linux library.

## How it plugs in

- `rclaunch.exe game.exe ra2ui.dll` loads the DLL before the game entry point.
- `DllMain` points every USER32, GDI32, COMCTL32 and IMM32 import slot of game.exe at our functions (130 names).
  A name without an implementation gets a stub that logs the name and stops. No silent stub.
- Lifted code reads the same import slots, so it calls our functions too. `RA2UI_ALSO_LOAD=rc.dll` loads the lifted code after the patch.
- Only one real window exists: the SDL window for the game's main window. On Windows its handle is the real HWND,
  so DirectDraw, DirectSound and Bink (Wine's or ours) accept it. All other windows (dialogs, controls) are our objects
  with handles `0x00c00000 + 4*n`. They have no pixels: the game draws every dialog and control itself into DirectDraw
  surfaces (WM_PAINT, WM_DRAWITEM, GetUpdateRect, ValidateRect).
- The SDL window id is in the SDL hint `RA2UI_SDL_WINDOW_ID`. Our DirectDraw adapter should present into that window
  instead of calling SDL_CreateWindowFrom on the game window.

## Model

- `src/ui.h`: Win32 types for a 32-bit build (no windows.h), the window object, internal API.
- `src/ui_api.inc`: the 130 imports with their stdcall signatures. `wsprintfA` is cdecl and separate.
- `src/window.c`: handle table, classes, tree (z-order), create and destroy messages, SetWindowPos, long values, hit test.
- `src/message.c`: posted queue, PeekMessage order (posted, quit, input, paint, timer), paint regions (bounding rectangles),
  timers, focus, activation, capture, key state, mouse and key messages, TranslateMessage (US layout).
- `src/dialog.c`: template parser, CreateDialog*, DialogBox* modal loop, DefDlgProc, IsDialogMessage, tab order.
- `src/controls.c`: Button and Static (Wine behaviour), first versions of list box, combo box, edit, trackbar,
  progress, hotkey, list view.
- `src/defwnd.c`: DefWindowProc, device contexts without pixels, message boxes (logged, answered), help, IME, drag images.
- `src/platform_sdl.c`: SDL window, SDL events to raw input, scan codes, time, lock.
- `src/win/dllmain.c`: import patching for the Windows build.

Message order follows Wine 11 (`dlls/win32u/window.c`, `dlls/user32/dialog.c`, `defdlg.c`, `button.c`).

## Evidence

- `reports/dialogs.json`: all 97 dialog templates of game.exe (1473 controls), made by `tools/dump_dialogs.py`.
  Each entry keeps the raw bytes. The parser consumes every byte of every template (script check in the tool run).
- `reports/import-sites.txt`: every use of the 130 imports by game.exe, with the containing function
  (`tools/import_sites.py`; counts calls through registers and import thunks).
- `reports/msg-consts.txt`: message numbers at SendMessage, PostMessage, SendDlgItemMessage and CallWindowProc sites
  (`tools/msg_consts.py`, static guess). The recorder trace `../../reports/messages-used.txt` is the real list.
- Control styles in the templates: owner-draw buttons (263), auto check boxes (118), owner-draw drop-down combo boxes
  with strings (150), owner-draw list boxes (42), trackbars (57). The game paints all of them.

## Tests

- `tools/run.sh <out> "<steps>"`: original game, hidden Xvfb :77, muted (Wine audio driver off, PulseAudio and PipeWire
  blocked, SDL dummy audio), one game at a time with the shared lock `/tmp/ra2-game-why.lock`.
  `UI=0` runs the same game on Wine's user32 as the reference.

## References and licences

- Wine 11 source (LGPL-2.1-or-later), sparse checkout in `../../../ref-community/wine` (user32, comctl32, win32u).
  Used as the behaviour reference. Control logic follows Wine closely, so treat these files as LGPL-derived until a
  licence decision for release.
- pcrecomp `runtime/win32hle/user32.c` (MIT, Copyright (c) 2026 sp00nz), in `../../../ref-community/sp00nznet_pcrecomp`.
  It has the same scope for its own guest-memory calling convention (arguments read from a guest stack). This DLL needs
  native stdcall functions under Wine, so it is a reference here, not the code base. Any part copied from it keeps
  its MIT notice.
- isle-portable `miniwin` (LGPL-3.0): checked; no dialog or control code. Not used.
