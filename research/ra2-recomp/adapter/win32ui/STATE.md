# win32ui (J14) state

Read DESIGN.md first (how the DLL plugs in, file map, licences).
This file says what is done, what is open for stage 2, and how to run the tests.

## Done

- Step 1: all 97 dialog templates in `reports/dialogs.json`; import list and call sites in `reports/import-sites.txt`.
- Stage 1 (done, reported to the owner): the original game.exe under Wine runs menu, skirmish setup and a
  skirmish on our USER32/GDI32/COMCTL32/IMM32 code (`ra2ui.dll`, 130 import slots).
- Metrics, fonts, device caps and colours come from Wine measurements (`reports/wine-metrics.txt`,
  `tools/gen_metrics.py` makes `src/wine_metrics.inc`).
- Dialog geometry: all 97 dialogs and all controls have the same window and client rects as Wine
  (`reports/wine-dialog-rects.txt`), except 1 progress bar. Combo boxes were fixed last
  (Wine `COMBO_NCCreate`, `combo_get_text_height`, `CBGetTextAreaHeight`, `COMBO_Size` in `src/controls.c`).
  This fixes the combo box text position in the skirmish setup screen.
- Recorder (sub-helper, done): `src/win/record.c` logs every import call and every message to a game
  procedure. `tools/msgdiff.py` compares two logs. Report: `reports/msglog/report.txt`, `README.txt`.

## Open for stage 2 (logs must equal Wine's on 3 sessions: menu, skirmish, quit)

Sessions and logs: `reports/msglog/sessions.sh` (run from the repo root), `session<N>-wine.log.gz`,
`session<N>-ours.log.gz`, `session<N>-diff.txt`.
You can keep the Wine logs and record only `ours` again after each change.

Known differences, in log order:

1. `RegisterClassA` result: Wine gives atom 0xc038, we give 0xc000 (`next_atom` in `src/window.c`).
   Start our atoms at 0xc038, or mask atoms in `tools/msgdiff.py`.
2. `ShowWindow(main, SW_SHOWNORMAL)`. Wine sends, in this order (see the Wine log after `CreateWindowExA`):
   WM_SHOWWINDOW(1), WM_WINDOWPOSCHANGING (x=y=cx=cy=0, flags 0x43, after=HWND_TOP), WM_GETICON(1),
   WM_GETICON(0), WM_QUERYNEWPALETTE, WM_ACTIVATEAPP(1, 0), WM_NCACTIVATE(1), WM_ACTIVATE(1) ->
   DefWindowProc -> WM_IME_SETCONTEXT(1, 0xc000000f), WM_SETFOCUS, then WM_NCPAINT(1), WM_ERASEBKGND,
   WM_WINDOWPOSCHANGED (0,0,1024,768, flags 0x1847). Then UpdateWindow gives one WM_PAINT.
   Our code sends WM_WINDOWPOSCHANGING with cx=1024, cy=768, flags 0x47 and no WM_GETICON.
3. Messages that Wine's ddraw/wined3d cause (depth=0 lines in the Wine log, about 10 per session):
   WM_STYLECHANGING/WM_STYLECHANGED for GWL_STYLE and GWL_EXSTYLE, WM_WINDOWPOSCHANGING/WM_NCCALCSIZE/
   WM_WINDOWPOSCHANGED with HWND_TOPMOST 0,0,1024,768 flags 0x70, WM_DISPLAYCHANGE(16, 1024x768) at start,
   and the reverse at quit (WM_DISPLAYCHANGE 32). In `ours` mode wined3d calls the real user32 on the real
   SDL window, so the game procedure does not get them. To match, our side must cause them. Options:
   a) our ddraw adapter calls ra2ui functions (SetWindowLongA, SetWindowPos, send WM_DISPLAYCHANGE) the same
      way wined3d does (`dlls/wined3d/swapchain.c` `wined3d_swapchain_state_setup_fullscreen`,
      `dlls/wined3d/device.c` `wined3d_device_setup_fullscreen_window`); b) patch the user32 import slots of
      Wine's wined3d.dll for the game window. Option a) is also right for the Linux build.
4. Later differences are not known yet. Fix 1 and 2, record again, and read the next first difference.

Port plan (Wine 11 source in `../../../ref-community/wine`, read the functions, keep the order exactly):

- Window position (`dlls/win32u/window.c`): `set_window_pos`, `calc_winpos`, `fixup_swp_flags`,
  `swp_owner_popups`, `calc_ncsize`, `apply_window_pos` (WM_GETICON ICON_BIG then ICON_SMALL on the first
  show of a window whose GA_ROOT is itself; the icon of the class counts only after the message),
  `show_window` (if the parent is not visible, only toggle WS_VISIBLE and send no position messages;
  WIN_NEED_SIZE: overlapped windows get WM_SIZE/WM_MOVE at the first show, not at create),
  `create_window` (WM_GETMINMAXINFO for WS_THICKFRAME or overlapped windows; WM_NCCALCSIZE wParam 0 with
  screen coordinates; no WM_SIZE/WM_MOVE at create for overlapped windows), `user_destroy_window`,
  `send_destroy_message` (activate_other_window if the window is active), `destroy_window`.
- DefWindowProc WM_NCCALCSIZE must compute the client rect (`dlls/win32u/defwnd.c` `handle_nc_calc_size`:
  border from adjust_window_rect without WS_EX_CLIENTEDGE, then client edge 2 only if the rect is larger
  than 4, vertical scroll bar if width >= 17, horizontal if height > 17). Use the message result as the
  client rect, not `calc_client` in `src/window.c`.
- Activation and focus (`dlls/win32u/input.c`): `set_active_window` (old: WM_NCACTIVATE(0), WM_ACTIVATE;
  new: WM_QUERYNEWPALETTE, WM_ACTIVATEAPP to all top-level windows when the app changes, WM_NCACTIVATE,
  WM_ACTIVATE), `set_focus_window` (focus changes first, then WM_KILLFOCUS, WM_IME_SETCONTEXT(0) to the old
  window, WM_IME_SETCONTEXT(1, 0xc000000f) and WM_SETFOCUS to the new one), `NtUserSetFocus`,
  `set_foreground_window`, `activate_other_window`, `can_activate_window` (window.c).
  SW_HIDE moves focus only if the hidden window itself has the focus (to the parent, or to none).
- Painting: Wine keeps a region per window, not a bounding rect. Port `server/window.c`
  (`redraw_window`, `crop_region_to_win_rect`, `get_window_visible_rect`, `set_update_region`,
  `validate_non_client`, `validate_parents`, `crop_children_update_region`, `expose_window`,
  `set_window_pos` of the server, `get_update_flags`, `get_child_update_flags`, `get_window_update_flags`,
  handler `get_update_region` (it validates the window when it returns UPDATE_PAINT),
  `find_window_to_repaint`, `get_visible_region`) and `dlls/win32u/dce.c` (`send_ncpaint`, `send_erase`,
  `BeginPaint` (rcPaint is the clip box), `erase_now`, `update_now`, `RedrawWindow`, `GetUpdateRect`,
  `ValidateRect`, `InvalidateRect`). UpdateWindow is RedrawWindow(RDW_UPDATENOW | RDW_ALLCHILDREN).
  After DispatchMessage of WM_PAINT, Wine calls GetUpdateRgn(erase=TRUE) (`dlls/win32u/message.c`).
  A rectangle-list region is enough (no GDI region calls are imported). Paint code is now in
  `src/message.c` (`ui_invalidate`, `find_paint`, `erase_now`, `update_now`).
- PeekMessage order in Wine (`server/queue.c` get_message): sent, posted, quit, hardware input,
  WM_PAINT (does not remove), timers. Ours is the same.

Other open items:

- Controls porter (sub-helper, Codex local session) ports Wine controls into `src/controls/`
  (button, static, edit, list box, combo box, trackbar, progress, hotkey), commit b32698a.
  It builds `build/ra2ui-port.dll` (`make build/ra2ui-port.dll`, `PORT_SRC` in the Makefile) and tests with
  `tests/run.sh` (its last result: hidden controls 14 of 15 sequences equal, visible 12 of 15).
  Its logs are in `build/ctltest/` and `build/ctltest-visible/`. It finished as a partial port: paint order,
  selection drawing, capture, scrolling and list view are still open. Its last game runs did not reach the
  skirmish setup screen every time, so test the game before you switch. The production build still uses
  `src/controls.c`. When the port is complete, replace `src/controls.c` with `src/controls/*.c` in `SRC`,
  then check `reports/wine-dialog-rects.txt` and the game again.
- The game cursor in menus (SetCursor/LoadCursorA give no visible cursor yet).
- The Linux build of the same source.

## How to build and test

- Build: `make -C adapter/win32ui` gives `build/ra2ui.dll`. `make OUT=build-dev` gives a second copy in
  `build-dev/` so a running test keeps its DLL. 32-bit only, uses `../../tools/wcc` and SDL2 2.30.12.
- Game run: `tools/run.sh <out-dir> "<steps>"`. Steps: `w <s>` wait, `c X Y` click, `r X Y` right click,
  `m X Y` move, `k KEYSYM` key, `s name` screenshot. Env: `SLOT=0..2` (display :78+SLOT, prefix
  `build/prefix<SLOT>`, lock `/tmp/ra2ui-slot<SLOT>.lock`), `UIBUILD=build-dev`, `UI=0` plain Wine user32,
  `DDRAW=n` the game folder ddraw.dll, `LIFT=1` also load rc.dll, `RA2UI_TRACE=1`.
  Example (menu to skirmish setup): `SLOT=0 UIBUILD=build-dev tools/run.sh /tmp/x "w 60; c 832 304; w 10; c 832 388; w 15; s setup"`.
- Every game must stay muted and hidden: run.sh sets the Wine audio driver off in the prefix,
  PULSE_SERVER=unix:/nonexistent, PIPEWIRE_REMOTE=/nonexistent, SDL_AUDIODRIVER=dummy, and a hidden Xvfb.
  It kills the game if PipeWire shows a "Red Alert" node.
- Message logs: `RA2UI_RECORD=Z:/tmp/x.log` records; `RA2UI_MODE=wine` records the real Wine functions.
  Compare with `python3 tools/msgdiff.py wine.log.gz ours.log.gz` (filters in `reports/msglog/README.txt`).
- Dialog geometry: `IDS=$(grep '^dialog' reports/wine-dialog-rects.txt | awk '{print $2}')`, then
  `RA2UI=1 UIBUILD=build-dev tools/probe/run_probe.sh probe_dialog.exe '/path/to/user\Projects\ra2\original\game\game.exe' $IDS | tr -d '\r'`
  and diff with `reports/wine-dialog-rects.txt`. Without `RA2UI=1` it measures Wine itself.
- Metrics: `tools/probe/run_probe.sh probe_metrics.exe` (Wine), then `tools/gen_metrics.py`.
