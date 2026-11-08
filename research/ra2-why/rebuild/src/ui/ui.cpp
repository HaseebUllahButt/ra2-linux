#include "ui.h"
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace ra2::ui {
namespace {
using Bytes = std::vector<std::uint8_t>;
void require(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
std::uint32_t next_codepoint(std::string_view text, std::size_t& i) {
    const auto first=static_cast<unsigned char>(text[i++]);
    std::uint32_t cp=first; unsigned continuation=0;
    if (first>=0xc2 && first<=0xdf) {cp=first&31;continuation=1;}
    else if (first>=0xe0 && first<=0xef) {cp=first&15;continuation=2;}
    else if (first>=0xf0 && first<=0xf4) {cp=first&7;continuation=3;}
    else require(first<0x80, "invalid UTF-8 text");
    require(continuation<=text.size()-i, "truncated UTF-8 text");
    for (unsigned j=0;j<continuation;++j) {
        auto next=static_cast<unsigned char>(text[i++]);
        require((next&0xc0)==0x80, "invalid UTF-8 text"); cp=(cp<<6)|(next&63);
    }
    require((continuation!=1 || cp>=0x80) && (continuation!=2 || cp>=0x800) &&
            (continuation!=3 || cp>=0x10000) && cp<=0xffff && !(cp>=0xd800 && cp<=0xdfff),
            "unsupported Unicode character");
    return cp;
}
void range(const Bytes& b, std::size_t p, std::size_t n) {
    require(p <= b.size() && n <= b.size() - p, "truncated UI data");
}
std::uint16_t u16(const Bytes& b, std::size_t p) {
    range(b,p,2); return std::uint16_t(b[p] | unsigned(b[p+1])<<8);
}
std::int16_t i16(const Bytes& b, std::size_t p) {
    unsigned v=u16(b,p); return static_cast<std::int16_t>(v < 32768 ? int(v) : int(v)-65536);
}
std::uint32_t u32(const Bytes& b, std::size_t p) {
    range(b,p,4); return std::uint32_t(b[p]) | std::uint32_t(b[p+1])<<8 |
        std::uint32_t(b[p+2])<<16 | std::uint32_t(b[p+3])<<24;
}
std::size_t checked_add(std::size_t a, std::size_t b) {
    require(b <= std::numeric_limits<std::size_t>::max()-a, "UI offset overflow"); return a+b;
}
struct Resource {
    const Bytes& b;
    std::size_t raw, length;
    std::size_t at(std::uint32_t rel, std::size_t size) const {
        require(rel <= length && size <= length-rel, "invalid PE resource offset");
        return checked_add(raw,rel);
    }
    std::uint32_t child(std::uint32_t dir, std::uint32_t id, bool subdir) const {
        auto p=at(dir,16); unsigned count=u16(b,p+12)+u16(b,p+14);
        require(count <= (length-dir-16)/8, "invalid PE resource directory");
        for (unsigned i=0;i<count;++i) {
            auto e=at(dir+16+i*8,8); auto name=u32(b,e), target=u32(b,e+4);
            if (name==id) {
                require(bool(target & 0x80000000u)==subdir, "invalid PE resource tree");
                return target & 0x7fffffffu;
            }
        }
        throw std::runtime_error("dialog resource not found");
    }
};
struct Slice { std::size_t start, end; };
Slice dialog_data(const Bytes& b, std::uint16_t id) {
    require(u16(b,0)==0x5a4d, "invalid executable header");
    auto pe=u32(b,0x3c); range(b,pe,24);
    require(u32(b,pe)==0x4550, "invalid PE signature");
    auto sections=u16(b,pe+6), opt_size=u16(b,pe+20); auto opt=checked_add(pe,24);
    range(b,opt,opt_size); require(opt_size>=120 && u16(b,opt)==0x10b, "unsupported PE format");
    auto resource_rva=u32(b,opt+112), resource_size=u32(b,opt+116);
    require(resource_rva && resource_size>=16, "missing PE resources");
    auto section_table=checked_add(opt,opt_size); range(b,section_table,std::size_t(sections)*40);
    auto resolve=[&](std::uint32_t rva, std::uint32_t size)->std::size_t {
        for (unsigned i=0;i<sections;++i) {
            auto p=section_table+i*40; auto va=u32(b,p+12), raw_size=u32(b,p+16), raw=u32(b,p+20);
            if (rva>=va && std::uint64_t(rva-va)+size<=raw_size) {
                auto result=checked_add(raw,rva-va); range(b,result,size); return result;
            }
        }
        throw std::runtime_error("PE resource range outside file");
    };
    auto root=resolve(resource_rva,resource_size); Resource res{b,root,resource_size};
    auto dialog=res.child(0,5,true); auto item=res.child(dialog,id,true);
    // The shipped resources use US English, language ID 1033.
    auto data=res.child(item,1033,false); auto entry=res.at(data,16);
    auto addr=u32(b,entry), size=u32(b,entry+4); auto start=resolve(addr,size);
    return {start,checked_add(start,size)};
}
struct Reader {
    const Bytes& b; std::size_t p,end;
    void need(std::size_t n) const { require(p<=end && n<=end-p, "truncated dialog template"); }
    std::uint16_t word() { need(2); auto v=u16(b,p);p+=2;return v; }
    std::uint32_t dword() { need(4);auto v=u32(b,p);p+=4;return v; }
    std::int16_t signed_word() { need(2);auto v=i16(b,p);p+=2;return v; }
    void skip(std::size_t n) {need(n);p+=n;}
    void align4() { auto next=checked_add(p,3)&~std::size_t(3); skip(next-p); }
    std::string text_or_ordinal(std::uint16_t* ordinal=nullptr) {
        auto c=word(); if (!c) return {};
        if (c==0xffff) {auto id=word(); if (ordinal) *ordinal=id; return {};}
        std::string out;
        while(c) { require(c<128, "non-ASCII dialog resource string unsupported"); out.push_back(char(c)); c=word(); }
        return out;
    }
};
}
Rect dialog_to_pixels(Rect rect, unsigned base_width, unsigned base_height) {
    require(base_width>0 && base_width<=4096 && base_height>0 && base_height<=4096,
            "invalid dialog font metrics");
    const auto convert=[](int value, unsigned base, int divisor) {
        const auto product=std::int64_t(value)*base;
        const auto rounded=(product>=0 ? product+divisor/2 : product-divisor/2)/divisor;
        require(rounded>=std::numeric_limits<int>::min() && rounded<=std::numeric_limits<int>::max(),
                "dialog geometry overflow");
        return static_cast<int>(rounded);
    };
    return {convert(rect.x,base_width,4),convert(rect.y,base_height,8),
            convert(rect.width,base_width,4),convert(rect.height,base_height,8)};
}
namespace {
Dialog parse_dialog(const Bytes& exe, std::uint16_t id, const ra2text::CsfFile& strings) {
    auto data=dialog_data(exe,id); Reader r{exe,data.start,data.end};
    require(r.word()==1 && r.word()==0xffff, "unsupported dialog template");
    r.dword();r.dword();auto style=r.dword();auto count=r.word();
    Dialog out{id,{r.signed_word(),r.signed_word(),r.signed_word(),r.signed_word()},"",0,{}};
    r.text_or_ordinal(); r.text_or_ordinal(); r.text_or_ordinal();
    if(style & 0x40) {out.font_points=r.word();r.word();r.skip(2);out.font=r.text_or_ordinal();}
    require(count<=4096, "too many dialog controls");
    for(unsigned i=0;i<count;++i) {
        r.align4(); r.dword(); auto ex=r.dword();auto flags=r.dword();
        Rect rect{r.signed_word(),r.signed_word(),r.signed_word(),r.signed_word()};
        auto control_id=r.dword();std::uint16_t cls=0; r.text_or_ordinal(&cls);
        auto key=r.text_or_ordinal();auto extra=r.word();r.skip(extra);
        std::string label;
        if (!key.empty()) {auto text=strings.find(key);if(text)label=text->text;}
        out.controls.push_back({control_id,flags,ex,cls,rect,std::move(key),std::move(label)});
    }
    return out;
}
} // namespace
Dialog load_dialog(const Bytes& exe, Page page, const ra2text::CsfFile& strings) {
    // 0x006040B0 associates these original resource IDs with each page.
    switch (page) {
    case Page::Main: return parse_dialog(exe,226,strings);
    case Page::SinglePlayer: return parse_dialog(exe,256,strings);
    case Page::Skirmish: return parse_dialog(exe,258,strings);
    }
    throw std::runtime_error("unsupported dialog page");
}
Dialog load_map_selection_dialog(const Bytes& exe, const ra2text::CsfFile& strings) {
    // 0x005E68A0 loads resource 107 for the original map selector.
    return parse_dialog(exe,107,strings);
}

std::optional<std::uint32_t> Dialog::hit_test(int x,int y,const std::vector<std::uint32_t>& enabled) const {
    for(auto it=controls.rbegin();it!=controls.rend();++it) {
        const auto& c=*it; const auto& r=c.bounds;
        // The template's visible and disabled flags remain authoritative;
        // the caller also lists controls that the native app can actually use.
        if (c.class_id!=128 || (c.style & 15u)!=11u ||
            !(c.style & 0x10000000u) || (c.style & 0x08000000u) ||
            std::find(enabled.begin(),enabled.end(),c.id)==enabled.end() || r.width<=0 || r.height<=0) continue;
        if (std::int64_t(x)>=r.x && std::int64_t(y)>=r.y && std::int64_t(x)<std::int64_t(r.x)+r.width && std::int64_t(y)<std::int64_t(r.y)+r.height) return c.id;
    }
    return std::nullopt;
}
std::optional<std::uint32_t> Dialog::hit_test_pixels(int x, int y, unsigned base_width,
                                                       unsigned base_height,
                                                       const std::vector<std::uint32_t>& enabled) const {
    for (auto it=controls.rbegin();it!=controls.rend();++it) {
        const auto& c=*it;
        if (c.class_id!=128 || (c.style & 15u)!=11u ||
            !(c.style & 0x10000000u) || (c.style & 0x08000000u) ||
            std::find(enabled.begin(),enabled.end(),c.id)==enabled.end()) continue;
        auto r=dialog_to_pixels(c.bounds,base_width,base_height);
        if (r.width>0 && r.height>0 && std::int64_t(x)>=r.x && std::int64_t(y)>=r.y &&
            std::int64_t(x)<std::int64_t(r.x)+r.width && std::int64_t(y)<std::int64_t(r.y)+r.height)
            return c.id;
    }
    return std::nullopt;
}
GameFont GameFont::parse(Bytes b) {
    // 0x00433990 reads a 28-byte fonT header, 65536-entry Unicode map,
    // and fixed-size glyph records. Other original font variants are unsupported.
    require(b.size()>=28 && u32(b,0)==0x546e6f66, "unsupported game font");
    GameFont out;
    out.max_width_=u32(b,4); out.row_bytes_=u32(b,8); out.height_=u32(b,12);
    out.line_height_=u32(b,16); out.count_=u32(b,20); out.glyph_bytes_=u32(b,24);
    require(out.max_width_>0 && out.max_width_<=1024 && out.row_bytes_>0 &&
            out.row_bytes_<=128 && out.max_width_<=out.row_bytes_*8 &&
            out.height_>0 && out.height_<=1024 && out.line_height_>0 &&
            out.count_<=65535 && std::uint64_t(out.row_bytes_)*out.height_+1<=out.glyph_bytes_,
            "invalid game font header");
    constexpr std::size_t glyph_base=28+65536*2;
    require(b.size()>=glyph_base && std::uint64_t(out.count_)*out.glyph_bytes_<=b.size()-glyph_base,
            "truncated game font");
    for (std::size_t i=0;i<65536;++i)
        require(u16(b,28+i*2)<=out.count_, "invalid game font character map");
    for (std::size_t i=0;i<out.count_;++i)
        require(b[glyph_base+i*out.glyph_bytes_]<=out.max_width_, "invalid game font glyph width");
    out.bytes_=std::move(b); return out;
}
std::optional<gfx::RgbaImage> GameFont::glyph(char16_t codepoint, gfx::Rgba color) const {
    constexpr std::size_t glyph_base=28+65536*2;
    const auto index=u16(bytes_,28+std::size_t(codepoint)*2);
    if (!index) return std::nullopt;
    const auto start=glyph_base+std::size_t(index-1)*glyph_bytes_;
    const auto width=bytes_[start];
    gfx::RgbaImage out{width,static_cast<std::uint16_t>(height_),{}};
    out.pixels.resize(std::size_t(width)*height_*4);
    // 0x00434120 tests bitmap bits from 0x80 to 0x01, row by row.
    for (std::size_t y=0;y<height_;++y) for (std::size_t x=0;x<width;++x) {
        if (!(bytes_[start+1+y*row_bytes_+x/8] & (0x80u>>(x%8)))) continue;
        auto p=(y*width+x)*4;
        out.pixels[p]=color.r; out.pixels[p+1]=color.g; out.pixels[p+2]=color.b; out.pixels[p+3]=color.a;
    }
    return out;
}
void GameFont::draw_text(gfx::RgbaImage& image, std::string_view text, int x, int y, gfx::Rgba color) const {
    require(image.pixels.size()==std::size_t(image.width)*image.height*4, "invalid RGBA image");
    std::int64_t cursor=x, row=y;
    for (std::size_t i=0;i<text.size();) {
        auto cp=next_codepoint(text,i);
        if (cp=='\r') continue;
        if (cp=='\n') {cursor=x;row+=line_height_;continue;}
        auto bitmap=glyph(static_cast<char16_t>(cp),color);
        require(bool(bitmap), "character missing from original game font");
        for (std::size_t gy=0;gy<bitmap->height;++gy) for (std::size_t gx=0;gx<bitmap->width;++gx) {
            const auto dx=cursor+static_cast<std::int64_t>(gx), dy=row+static_cast<std::int64_t>(gy);
            if (dx<0 || dy<0 || dx>=image.width || dy>=image.height) continue;
            auto source=(gy*bitmap->width+gx)*4;
            if (!bitmap->pixels[source+3]) continue;
            auto dest=(std::size_t(dy)*image.width+std::size_t(dx))*4;
            std::copy_n(bitmap->pixels.data()+source,4,image.pixels.data()+dest);
        }
        cursor += bitmap->width + 1; // 0x00433880 sets one pixel of character spacing.
    }
}
std::uint32_t GameFont::text_width(std::string_view text) const {
    std::uint64_t width=0;
    for (std::size_t i=0;i<text.size();) {
        auto cp=next_codepoint(text,i);
        require(cp!='\n' && cp!='\r' && cp!='\t', "multiline text width is unsupported");
        auto bitmap=glyph(static_cast<char16_t>(cp),{});
        require(bool(bitmap), "character missing from original game font");
        width+=bitmap->width+1;
        require(width<=std::numeric_limits<std::uint32_t>::max(), "text width overflow");
    }
    return static_cast<std::uint32_t>(width);
}
ComboItemMetrics combo_item_metrics(const GameFont& font) {
    const auto height=font.line_height();
    require(height<=std::numeric_limits<std::uint32_t>::max()-6, "combo item height overflow");
    // 0x00618B17 and 0x00618B2B set selection and list item heights.
    // 0x00433880 obtains the font height from the GAME.FNT header.
    return {height+2,height+6};
}
gfx::RgbaImage decode_pcx(const Bytes& b) {
    require(b.size()>=897 && b[0]==10 && b[2]==1 && b[3]==8 && b[65]==1, "unsupported PCX format");
    auto x0=u16(b,4), y0=u16(b,6), x1=u16(b,8), y1=u16(b,10), stride=u16(b,66);
    require(x1>=x0 && y1>=y0, "invalid PCX dimensions");
    unsigned width=unsigned(x1)-x0+1, height=unsigned(y1)-y0+1;
    require(width<=65535 && height<=65535 && stride>=width && std::uint64_t(width)*height<=16*1024*1024, "invalid PCX dimensions");
    auto palette=b.size()-769;require(b[palette]==12,"missing PCX palette");
    gfx::RgbaImage out{static_cast<std::uint16_t>(width),static_cast<std::uint16_t>(height),{}};
    out.pixels.resize(std::size_t(width)*height*4);std::size_t p=128;
    for(unsigned y=0;y<height;++y) {
        unsigned x=0;
        while(x<stride) {
            require(p<palette,"truncated PCX scanline"); auto v=b[p++]; unsigned count=1;
            if((v&0xc0)==0xc0) {count=v&63;require(count && p<palette,"invalid PCX run");v=b[p++];}
            require(count<=stride-x,"PCX run crosses scanline");
            while(count--) {
                if(x<width) {auto dest=(std::size_t(y)*width+x)*4;auto c=palette+1+std::size_t(v)*3;
                    out.pixels[dest]=b[c];out.pixels[dest+1]=b[c+1];out.pixels[dest+2]=b[c+2];out.pixels[dest+3]=255;}
                ++x;
            }
        }
    }
    return out;
}
}
