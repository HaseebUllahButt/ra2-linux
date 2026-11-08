# Original UI resources

This is an incomplete UI library. It reads original menu resources and decodes
original images and font glyphs. It does not yet draw a complete main menu,
skirmish setup, or sidebar. It does not choose button skins, implement menu
navigation, or enable gameplay actions.

`load_dialog` reads the original `gamemd.exe` 32-bit PE resource directory.
It supports the extended dialog templates used by the supplied game. Page IDs
are 226 for main, 256 for single player, and 258 for skirmish. The ID and
control mappings are also visible at `0x006040B0`. Captions beginning `GUI:`
are resolved with the existing CSF reader. Control positions remain in Windows
dialog units from the executable. `dialog_to_pixels` accepts the host's actual
font base units; it does not assume a font size or screen resolution.
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

Run the tests:

    c++ -std=c++17 -O0 -Wall -Wextra -Werror -I rebuild/src/fs rebuild/src/ui/{ui,test_main}.cpp rebuild/src/text/csf.cpp rebuild/src/fs/{filename_hash,mix_archive,mix_bootstrap}.cpp -o /tmp/ui_test
    /tmp/ui_test /path/to/user/ra2/game

The test checks all three original dialog resources, main-menu text and hit
bounds, the title PCX, the original bitmap font, and damaged input.

To run bounded-input tests under sanitizers, pass the original executable, CSF,
TITLE.PCX, and GAME.FNT as four file arguments to `test_safety.cpp`. The test
never writes decoded assets. Those assets can be obtained through the existing
MIX reader for local testing.
