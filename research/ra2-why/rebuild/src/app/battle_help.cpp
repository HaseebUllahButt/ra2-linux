// Reference: CnC_Red_Alert/CODE/HELP.H uses gold help text.
// TODO-ORIGINAL: use original panel art when available.
#include "battle_help.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <string>
namespace ra2::app {
namespace {
using Color = std::array<unsigned char, 4>;
void fill(render::Bitmap& b, render::Rectangle r, Color c) {
    for (int y=std::max(0,r.y); y<std::min(b.height,r.y+r.height); ++y)
        for (int x=std::max(0,r.x); x<std::min(b.width,r.x+r.width); ++x) {
            auto i=(static_cast<std::size_t>(y)*b.width+x)*4;
            std::copy(c.begin(),c.end(),b.rgba.begin()+i);
        }
}
// TODO-ORIGINAL: replace the embedded 3x5 font with original game typography.
void text(render::Bitmap& b, int x, int y, const std::string& s, Color c, int limit,
          int scale=2) {
    const std::string chars="ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789$:/-?.";
    const char* glyphs[]={"010101111101101","110101110101110","011100100100011",
        "110101101101110","111100110100111","111100110100100","011100101101011",
        "101101111101101","111010010010111","001001001101010","101101110101101",
        "100100100100111","101111111101101","101111111111101","010101101101010",
        "110101110100100","010101101111011","110101110101101","011100010001110",
        "111010010010010","101101101101111","101101101101010","101101111111101",
        "101101010101101","101101010010010","111001010100111","111101101101111",
        "010110010010111","110001010100111","110001010001110","101101111001001",
        "111100110001110","011100111101111","111001010010010","111101111101111",
        "111101111001110","011110010011110","000010000010000","001001010100100",
        "000000111000000","110001010000010","000000000000010"};
    int end=x+limit;
    for (unsigned char raw:s) {
        if (x+3*scale>end) break;
        auto n=chars.find(static_cast<char>(std::toupper(raw)));
        if(n!=std::string::npos) for(int j=0;j<15;++j)
            if(glyphs[n][j]=='1') fill(b,{x+(j%3)*scale,y+(j/3)*scale,scale,scale},c);
        x+=4*scale;
    }
}

}
void BattleHelp::draw(render::Bitmap& b) const {
    if (b.width<=0 || b.height<=0 || static_cast<std::uint64_t>(b.width)*b.height*4>b.rgba.size()) return;
    const int x=(b.width-600)/2, y=(b.height-430)/2;
    constexpr Color edge{160,145,84,255}, bg{20,27,30,255}, ink{235,230,200,255}, gold{255,214,64,255};
    fill(b,{x,y,600,430},edge); fill(b,{x+3,y+3,594,424},bg);
    text(b,x+20,y+18,"SKIRMISH CONTROLS",gold,560,3);
    text(b,x+20,y+49,"OBJECTIVE: DESTROY THE ENEMY BASE.",ink,560);
    const char* lines[]={"LEFT CLICK: SELECT A UNIT", "DRAG LEFT MOUSE: BOX SELECT UNITS", "LEFT CLICK GROUND: MOVE SELECTED UNITS", "LEFT CLICK ENEMY: ATTACK", "RIGHT CLICK: CLEAR SELECTION", "DOUBLE CLICK MCV OR PRESS D: DEPLOY", "S: STOP SELECTED UNITS", "ARROWS OR SCREEN EDGES: MOVE CAMERA", "SIDEBAR: BUILD. WHEN READY, CLICK MAP TO PLACE", "ESC: RETURN TO MENU"};
    int line_y=y+83;
    for (auto line:lines) { text(b,x+20,line_y,line,ink,560); line_y+=29; }
    text(b,x+20,y+394,"CLICK TO CLOSE   /   F1: SHOW HELP",gold,560);
}
}
