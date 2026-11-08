# Original UI resources

This is an incomplete UI library. It reads original menu resources and decodes
original images and font glyphs. It does not yet draw a complete main menu,
skirmish setup, or sidebar. It does not enable gameplay.
`MenuNavigation` provides the four verified main, single-player and skirmish
page changes by control ID. It ignores Skirmish Start and unsupported IDs.
Exit opens the original two-button confirmation dialog at `0x0052DDBA`.
`Action::Quit` is returned only after OK. Cancel returns to the main page.
The dialog uses the original panel, button frames, palettes, and text.
Escape cancels the confirmation through `key_down(Key::Escape)`.

`load_dialog` reads the original `gamemd.exe` 32-bit PE resource directory.
It supports the extended dialog templates used by the supplied game. Page IDs
are 226 for main, 256 for single player, and 258 for skirmish. The ID and
control mappings are also visible at `0x006040B0`. Captions beginning `GUI:`
are resolved with the existing CSF reader. Control positions remain in Windows
dialog units from the executable. `dialog_to_pixels` accepts the host's actual
font base units. The original Windows base units are not yet recovered; Linux
desktop font metrics must not be substituted for them.
`hit_test` and `hit_test_pixels` return only controls that the caller explicitly
enables. They also respect the template's visible and disabled flags. Hit
testing currently covers only owner-drawn buttons. Combo-box template heights
include their drop-down lists and cannot be used as closed-control hit bounds.

`decode_pcx` reads the installed 8-bit, one-plane PCX format into opaque RGBA.
`GameFont` reads the original `GAME.FNT` `fonT` format and draws glyphs into an
RGBA image. `0x00433990` reads its Unicode table and fixed-size records;
`0x00434120` draws their bits. The other original font container, missing
glyph fallback, and supplementary Unicode code points are not implemented.
`TITLE.PCX` and `GAME.FNT` are original assets, but using them does not by
itself reconstruct the original dialog screens.

`Menu` draws the main and single-player shell buttons and captions at 800x600.
It uses original frames 2, 3, and 4 for normal, hover, and pressed states
(`0x00612EAA`). Unsupported actions are disabled. Skirmish Start is disabled.
Skirmish map, player and slider controls, other screen sizes, sounds, and
other keyboard input are not yet implemented. The menu uses the 300x163 calibration
of the 200x100 dialog-unit reference at `0x00775BC0`. For these button rows,
the result stays the same for reference font heights from 8 to 24 pixels.
The original Windows font base units remain unverified.

`Menu::skirmish_setup()` exposes saved player and opponent choices from the
original edition-specific INI file. `read_skirmish_setup` also applies original
built-in and rules defaults. The five original checkboxes update the setup
when their original dialog control is enabled. A change returns
`Action::SetupChanged`.
Start and Choose Map stay disabled. `0x00697F10` reads the
original saved skirmish settings; `0x006980C0` reads player preferences.
`load_skirmish_maps` reads the original `MultiMaps` packet in catalog order.
It resolves CSF names, checks files through the prepared filesystem, and adds
loose PKT and MMX/YRO packet entries. Local entries use sorted Linux order;
the original Windows enumeration order is not guaranteed. YRM entries and
fallback selection for the active game mode are not yet implemented. A saved
scenario index is therefore not guaranteed to resolve to the same map.

`draw_shell_background` draws the static 800x600 shell into RGBA, using a
prepared filesystem. Mount `NEUTRAL.MIX` for the base game and mount
`NTRLMD.MIX` followed by `NEUTRAL.MIX` for Yuri's Revenge. `0x00534E50`
mounts these archives in the expansion. `0x0072EC70`, `0x0072E450`, and
`0x0072E730` select the background pieces and their positions. Other screen
sizes and complete menu composition are not yet implemented.

Run the tests:

    c++ -std=c++17 -O0 -Wall -Wextra -Werror -I rebuild/src/fs rebuild/src/ui/{ui,shell,skirmish,test_main}.cpp rebuild/src/text/csf.cpp rebuild/src/rules/ini_file.cpp rebuild/src/fs/{filename_hash,mix_archive,mix_bootstrap}.cpp rebuild/src/gfx/{shp_decode,palette}.cpp -o /tmp/ui_test
    /tmp/ui_test /path/to/user/ra2/game

The test checks all three original dialog resources, main-menu text and hit
bounds, the title PCX, the original bitmap font, shell source pixels, page
changes, pointer press and release, both game executables and language archives,
checkbox toggles, and damaged input.

To run bounded-input tests under sanitizers, pass the original executable, CSF,
TITLE.PCX, and GAME.FNT as four file arguments to `test_safety.cpp`. The test
never writes decoded assets. Those assets can be obtained through the existing
MIX reader for local testing.
