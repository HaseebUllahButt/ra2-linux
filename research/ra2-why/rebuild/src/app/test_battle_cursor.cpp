#include "battle_cursor.h"
#include <cassert>
#include <iostream>
int main(int argc, char** argv) {
    assert(argc == 2);
    ra2::fs::MixFileSystem files(argv[1]);
    files.mount_language(ra2::fs::GameVariant::RedAlert2);
    files.bootstrap(ra2::fs::GameVariant::RedAlert2);
    files.mount_overlays(); files.mount_resources();
    ra2::app::BattleCursor cursor(files);
    for (auto kind : {ra2::app::CursorKind::Normal, ra2::app::CursorKind::Select,
        ra2::app::CursorKind::Move, ra2::app::CursorKind::Attack,
        ra2::app::CursorKind::Deploy, ra2::app::CursorKind::NoGo}) {
        for (unsigned tick=0; tick<60; ++tick) {
            ra2::render::Bitmap b{80,80,std::vector<std::uint8_t>(80*80*4)};
            cursor.draw(b,kind,40,40,tick);
            bool drawn=false; for (auto c:b.rgba) drawn |= c != 0;
            assert(drawn);
            cursor.draw(b,kind,0,0,tick);
        }
    }
    std::cout << "cursor assets and frames OK\n";
}
