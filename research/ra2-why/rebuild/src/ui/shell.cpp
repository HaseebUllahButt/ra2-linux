#include "ui.h"
#include "../fs/mix_bootstrap.h"
#include "../rules/ini_file.h"
#include "../pack/map_pack.h"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace ra2::ui {
namespace {
std::vector<std::uint8_t> required(const fs::MixFileSystem& files, const char* name) {
    auto bytes = files.read_file(name);
    if (!bytes) throw std::runtime_error(std::string("Missing shell asset: ") + name);
    return std::move(*bytes);
}

gfx::ShpImage shape(const fs::MixFileSystem& files, const char* name) {
    return gfx::ShpImage::parse(required(files, name));
}
std::optional<bool>* option(SkirmishSetup& setup, std::uint32_t id) {
    switch (id) {
    case 1358: return &setup.short_game;
    case 1683: return &setup.mcv_repacks;
    case 1686: return &setup.crates;
    case 1690: return &setup.super_weapons;
    case 1693: return &setup.build_off_ally;
    default: return nullptr;
    }
}
const std::optional<bool>* option(const SkirmishSetup& setup, std::uint32_t id) {
    switch (id) {
    case 1358: return &setup.short_game;
    case 1683: return &setup.mcv_repacks;
    case 1686: return &setup.crates;
    case 1690: return &setup.super_weapons;
    case 1693: return &setup.build_off_ally;
    default: return nullptr;
    }
}

struct SliderValue { std::optional<int> value; int minimum, maximum, increment; };
SliderValue slider_value(const SkirmishSetup& setup, std::uint32_t id) {
    if (id==SkirmishControls::speed)
        return {setup.game_speed ? std::optional<int>(6-*setup.game_speed) : std::nullopt,0,6,1};
    if (id==SkirmishControls::units) return {setup.unit_count,setup.min_units,setup.max_units,1};
    return {setup.credits,setup.min_credits,setup.max_credits,setup.credit_increment};
}
bool is_slider(std::optional<std::uint32_t> id) {
    return id==SkirmishControls::credits || id==SkirmishControls::speed || id==SkirmishControls::units;
}
void blit(gfx::RgbaImage& dst, const gfx::RgbaImage& src, int x, int y,
          int clip_width = 65535, int clip_height = 65535) {
    for (int sy = 0; sy < src.height && sy < clip_height; ++sy) {
        const int dy = y + sy;
        if (dy < 0 || dy >= dst.height) continue;
        for (int sx = 0; sx < src.width && sx < clip_width; ++sx) {
            const int dx = x + sx;
            if (dx < 0 || dx >= dst.width) continue;
            const auto si = (static_cast<std::size_t>(sy) * src.width + sx) * 4;
            if (!src.pixels[si + 3]) continue;
            const auto di = (static_cast<std::size_t>(dy) * dst.width + dx) * 4;
            std::copy_n(src.pixels.begin() + si, 4, dst.pixels.begin() + di);
        }
    }
}
bool contains(Rect r, int x, int y) {
    return x>=r.x && y>=r.y && std::int64_t(x)<std::int64_t(r.x)+r.width &&
           std::int64_t(y)<std::int64_t(r.y)+r.height;
}
void fill(gfx::RgbaImage& image, Rect r, gfx::Rgba color) {
    const int left=std::max(0,r.x), top=std::max(0,r.y);
    const int right=static_cast<int>(std::min<std::int64_t>(image.width,std::int64_t(r.x)+r.width));
    const int bottom=static_cast<int>(std::min<std::int64_t>(image.height,std::int64_t(r.y)+r.height));
    for (int y=top;y<bottom;++y) for(int x=left;x<right;++x) {
        const auto pos=(static_cast<std::size_t>(y)*image.width+x)*4;
        image.pixels[pos]=color.r; image.pixels[pos+1]=color.g;
        image.pixels[pos+2]=color.b; image.pixels[pos+3]=color.a;
    }
}
void blit_strip(gfx::RgbaImage& dst, const gfx::RgbaImage& src, int x, int y, int width) {
    if (!src.width || width<=0) return;
    const int start=src.width>width ? (src.width-width)/2 : 0;
    for (int sy=0;sy<src.height;++sy) for (int dx=0;dx<width;++dx) {
        const int xx=x+dx, yy=y+sy;
        if (xx<0 || yy<0 || xx>=dst.width || yy>=dst.height) continue;
        const int sx=src.width>width ? start+dx : dx%src.width;
        const auto s=(static_cast<std::size_t>(sy)*src.width+sx)*4;
        if (!src.pixels[s+3]) continue;
        const auto d=(static_cast<std::size_t>(yy)*dst.width+xx)*4;
        std::copy_n(src.pixels.begin()+s,4,dst.pixels.begin()+d);
    }
}
} // namespace

// 0x006040B0 identifies these controls on dialogs 226, 256 and 258.
Action MenuNavigation::activate(std::uint32_t id) noexcept {
    if (page_ == Page::Main && id == 1667) page_ = Page::SinglePlayer;
    else if (page_ == Page::SinglePlayer && id == 1401) page_ = Page::Skirmish;
    else if (page_ == Page::SinglePlayer && id == 1670) page_ = Page::Main;
    else if (page_ == Page::Skirmish && id == 1472) page_ = Page::SinglePlayer;
    else return Action::None;
    return Action::PageChanged;
}

gfx::RgbaImage draw_shell_background(const fs::MixFileSystem& files, unsigned width, unsigned height) {
    if (width != 800 || height != 600)
        throw std::runtime_error("Shell background currently supports only 800x600");
    // 0x0072E5B9 uses SHELL2.PAL for the tiles. 0x0072E62E uses
    // SDBTNANM.PAL for the button cells. The other pieces use SHELL.PAL.
    const auto pal = gfx::Palette::from_bytes(required(files, "SHELL.PAL"));
    const auto tile_pal = gfx::Palette::from_bytes(required(files, "SHELL2.PAL"));
    const auto button_pal = gfx::Palette::from_bytes(required(files, "SDBTNANM.PAL"));
    const auto main = shape(files, "MNSCRNL.SHP").decode_frame(0, pal);
    const auto lower = shape(files, "LWSCRNL.SHP").decode_frame(0, pal);
    const auto top = shape(files, "SDTP.SHP").decode_frame(0, pal);
    const auto tile = shape(files, "SDBTNBKGD.SHP").decode_frame(0, tile_pal);
    const auto button = shape(files, "SDBTNANM.SHP").decode_frame(10, button_pal);
    const auto bottom = shape(files, "SDBTM.SHP").decode_frame(0, pal);
    if (top.width != tile.width || !tile.height || button.width > tile.width ||
        lower.width != main.width || main.width + top.width != width ||
        main.height + lower.height != height || top.height > height)
        throw std::runtime_error("Unsupported shell asset dimensions");
    gfx::RgbaImage out{static_cast<std::uint16_t>(width), static_cast<std::uint16_t>(height),
                       std::vector<std::uint8_t>(static_cast<std::size_t>(width) * height * 4)};
    for (std::size_t i = 3; i < out.pixels.size(); i += 4) out.pixels[i] = 255;
    // 0x0072EC70 builds the rectangles. 0x0072E450 draws the panel,
    // frame 10 of each button cell, and the clipped bottom piece.
    // 0x0072E730 draws the large main image. 0x0072E450 draws its lower edge.
    blit(out, main, 0, 0);
    blit(out, lower, 0, height - lower.height);
    const int panel_x = static_cast<int>(width) - top.width;
    blit(out, top, panel_x, 0);
    const int count = (height - top.height) / tile.height;
    for (int i = 0; i < count; ++i) {
        const int y = top.height + i * tile.height;
        blit(out, tile, panel_x, y);
        blit(out, button, width - button.width, y);
    }
    blit(out, bottom, width - bottom.width, top.height + count * tile.height);
    return out;
}
} // namespace ra2::ui

namespace ra2::ui {
Menu Menu::load(const fs::MixFileSystem& files, const std::vector<std::uint8_t>& exe,
                const ra2text::CsfFile& strings) {
    Menu result;
    result.preview_files_=&files;
    result.background_ = draw_shell_background(files, 800, 600);
    result.main_ = load_dialog(exe, Page::Main, strings);
    result.single_ = load_dialog(exe, Page::SinglePlayer, strings);
    result.skirmish_ = load_dialog(exe, Page::Skirmish, strings);
    result.map_dialog_ = load_map_selection_dialog(exe,strings);
    // 0x0072D120 loads this expansion-specific selector background.
    if (files.variant()==fs::GameVariant::YurisRevenge)
    if (const auto pixels=files.read_file("MnScrnLCustomizeBattle.shp")) {
        if (const auto palette=files.read_file("MnScrnLCustomizeBattle.pal"))
            result.map_background_=gfx::ShpImage::parse(*pixels).decode_frame(0,gfx::Palette::from_bytes(*palette));
    }
    result.font_ = GameFont::parse(required(files, "GAME.FNT"));
    result.arrow_up_ = decode_pcx(required(files, "uparrowr.pcx"));
    result.arrow_normal_ = decode_pcx(required(files, "dnarrowr.pcx"));
    result.arrow_pressed_ = decode_pcx(required(files, "dnarrowp.pcx"));
    result.check_on_ = decode_pcx(required(files, "cce_i.pcx"));
    result.check_off_ = decode_pcx(required(files, "cue_i.pcx"));
    result.slider_grip_ = decode_pcx(required(files, "trakgrip.pcx"));
    result.slider_left_ = decode_pcx(required(files, "trofl.pcx"));
    result.slider_middle_ = decode_pcx(required(files, "trofm.pcx"));
    result.slider_right_ = decode_pcx(required(files, "trofr.pcx"));
    rules::IniFile preferences;
    auto saved = files.read_file(files.variant() == fs::GameVariant::RedAlert2 ? "RA2.INI" : "RA2MD.INI");
    if (saved) preferences.parse(std::string_view(reinterpret_cast<const char*>(saved->data()), saved->size()));
    rules::IniFile base_rules, expansion_rules;
    const auto base_data=files.read_file("RULES.INI");
    if (base_data) base_rules.parse(std::string_view(reinterpret_cast<const char*>(base_data->data()),base_data->size()));
    const auto expansion_data=files.variant() == fs::GameVariant::YurisRevenge ?
                              files.read_file("RULESMD.INI") : std::nullopt;
    if (expansion_data) expansion_rules.parse(std::string_view(reinterpret_cast<const char*>(expansion_data->data()),expansion_data->size()));
    result.setup_ = read_skirmish_setup(preferences, base_data ? &base_rules : nullptr,
                                       expansion_data ? &expansion_rules : nullptr);
    result.countries_ = load_skirmish_countries(files, strings);
    result.colors_ = load_skirmish_colors(strings);
    result.opponent_choices_ = load_skirmish_opponents(strings);
    const auto colors = gfx::Palette::from_bytes(required(files, "SDBTNANM.PAL"));
    const auto button = shape(files, "SDBTNANM.SHP");
    result.normal_ = button.decode_frame(2, colors);
    result.hover_ = button.decode_frame(3, colors);
    result.pressed_ = button.decode_frame(4, colors);
    const auto popup_colors = gfx::Palette::from_bytes(required(files,
        files.variant() == fs::GameVariant::RedAlert2 ? "DIALOG.PAL" : "DIALOGN.PAL"));
    const auto popup_button_colors = gfx::Palette::from_bytes(required(files, "MAINBTTN.PAL"));
    result.popup_ = shape(files, "PUDLGBGN.SHP").decode_frame(0, popup_colors);
    const auto popup_button = shape(files, "MNBTTN.SHP");
    result.popup_normal_ = popup_button.decode_frame(0, popup_button_colors);
    result.popup_pressed_ = popup_button.decode_frame(1, popup_button_colors);
    result.popup_hover_ = popup_button.decode_frame(2, popup_button_colors);
    auto label = [&](const char* key) {
        const auto* value = strings.find(key);
        if (!value) throw std::runtime_error(std::string("Missing UI text: ") + key);
        return value->text;
    };
    result.exit_prompt_ = label("GUI:ExitAreYouSure");
    result.exit_ok_ = label("TXT_OK");
    result.exit_cancel_ = label("GUI:Cancel");
    if (result.normal_.width != 156 || result.normal_.height != 42)
        throw std::runtime_error("Unsupported shell button dimensions");
    return result;
}

std::vector<Menu::Button> Menu::buttons() const {
    // Resource 288 has OK (1454) and Cancel (2). At the 800x600 baseline,
    // 0x00775BC0 normalizes their geometry and 0x00777080 centers the dialog.
    if (confirming_exit_)
        return {{1454, {486,389,126,25}, exit_ok_, true},
                {2, {486,422,126,25}, exit_cancel_, true}};
    const Dialog* dialog = &main_;
    if (page() == Page::SinglePlayer) dialog = &single_;
    if (page() == Page::Skirmish) dialog = choosing_map_ ? &map_dialog_ : &skirmish_;
    std::vector<Button> result;
    for (const auto& control : dialog->controls) {
        const auto id = control.id;
        const bool main_button = page() == Page::Main &&
            (id == 1667 || id == 1668 || id == 1400 || id == 1670 || id == 1372 || id == 1006);
        const bool single_button = page() == Page::SinglePlayer &&
            (id == 1672 || id == 1673 || id == 1401 || id == 1670);
        const bool skirmish_button = page() == Page::Skirmish && !choosing_map_ &&
                                      (id == 1559 || id == 1450 || id == 1472);
        const bool map_button = choosing_map_ && (id==1472 || id==1733 || id==1411);
        if (!main_button && !single_button && !skirmish_button && !map_button) continue;
        const bool back = (page() == Page::Main && id == 1006) ||
                          (page() == Page::SinglePlayer && id == 1670) ||
                          (page() == Page::Skirmish && id == 1472);
        // 0x00775BC0 normalizes a 200x100 DLU reference to 300x163 pixels.
        // For these button Y values, normalization followed by cell snapping
        // gives the same rows for reference font heights 8 through 24 pixels.
        // 0x0060B000 snaps to 42-pixel cells; 0x0060B350 places Back last.
        const int pixels_y = control.bounds.y * 163 / 100;
        int row = (pixels_y - 199 + 21) / 42;
        row = std::max(0, std::min(8, row));
        const int y = back ? 535 : 199 + row * 42;
        const bool enabled = (page() == Page::Main && (id == 1667 || id == 1006)) ||
                             (page() == Page::SinglePlayer && (id == 1401 || id == 1670)) ||
                             (page() == Page::Skirmish && !choosing_map_ &&
                              (id == 1472 || (id==1559 && prototype_start_) ||
                               (id==1450 && !maps_.empty() && !modes_.empty()))) ||
                             (choosing_map_ && (id==1472 || (id==1733 && pending_map_ && pending_mode_)));
        result.push_back({id, {644, y, 156, 42}, control.label, enabled});
    }
    return result;
}

std::vector<Menu::Button> Menu::checks() const {
    std::vector<Button> result;
    if (confirming_exit_ || choosing_map_ || page()!=Page::Skirmish) return result;
    for (const auto& control : skirmish_.controls) {
        const auto id=control.id;
        if (id!=1358 && id!=1683 && id!=1686 && id!=1690 && id!=1693) continue;
        const auto* value = option(setup_, id);
        const bool adjusted = id!=1693; // 0x0060B950 moves these four left one pixel.
        const int x=(control.bounds.x*3+1)/2-(adjusted ? 1 : 0);
        const int y=(control.bounds.y*163+50)/100;
        const int w=(control.bounds.width*3+1)/2+1;
        const int h=(control.bounds.height*163+50)/100+1;
        const bool enabled=value && value->has_value() &&
                           (control.style & 0x10000000u) && !(control.style & 0x08000000u);
        result.push_back({id,{x,y,w,h},control.label,enabled});
    }
    return result;
}

std::optional<Rect> Menu::map_list_bounds(std::uint32_t id) const {
    if (!choosing_map_) return std::nullopt;
    for (const auto& c : map_dialog_.controls) if (c.id==id) {
        return Rect{(c.bounds.x*3+1)/2,(c.bounds.y*163+50)/100,
                    (c.bounds.width*3+1)/2+1,(c.bounds.height*163+50)/100+1};
    }
    return std::nullopt;
}
std::vector<std::size_t> Menu::visible_map_indices() const {
    std::vector<std::size_t> result;
    if (!pending_mode_) return result;
    const auto mode=std::find_if(modes_.begin(),modes_.end(),[&](const auto& m){return m.id==*pending_mode_;});
    if (mode==modes_.end()) return result;
    for (std::size_t i=0;i<maps_.size();++i)
        if (maps_[i].available && skirmish_map_matches_mode(maps_[i],*mode)) result.push_back(i);
    return result;
}
std::vector<Menu::Dropdown> Menu::dropdowns() const {
    std::vector<Dropdown> result;
    if (confirming_exit_ || choosing_map_ || page()!=Page::Skirmish) return result;
    for (const auto& c : skirmish_.controls) {
        int kind=-1;
        std::size_t slot=0;
        if (c.id==SkirmishControls::player_country) kind=1;
        if (c.id==SkirmishControls::player_color) kind=2;
        for (std::size_t i=0;i<setup_.opponents.size();++i) {
            if (c.id==SkirmishControls::opponents[i]) {kind=0;slot=i+1;}
            if (c.id==SkirmishControls::countries[i]) {kind=1;slot=i+1;}
            if (c.id==SkirmishControls::colors[i]) {kind=2;slot=i+1;}
        }
        if (kind<0 || !(c.style&0x10000000u)) continue;
        const bool active=slot==0 || (setup_.opponents[slot-1].opponent &&
                                      *setup_.opponents[slot-1].opponent!=Opponent::Closed);
        const bool enabled=!(c.style&0x08000000u) && (kind==0 || active);
        // The item is 19 pixels. Linux adds a two-pixel border on each side.
        const int height=static_cast<int>(combo_item_metrics(font_).selection_height)+4;
        Dropdown d{c.id,slot,kind,{(c.bounds.x*3+1)/2,(c.bounds.y*163+50)/100,
                                  (c.bounds.width*3+1)/2+1,height},enabled,{},-999};
        if (kind==0) {
            if (setup_.opponents[slot-1].opponent)
                d.current=static_cast<int>(*setup_.opponents[slot-1].opponent);
            for (const auto& choice : opponent_choices_)
                d.choices.push_back({static_cast<int>(choice.value),choice.name,std::nullopt});
        } else if (kind==1) {
            if (slot) d.current=setup_.opponents[slot-1].country;
            for (const auto& choice : countries_) {
                if (!slot && setup_.country==choice.id) d.current=choice.index;
                d.choices.push_back({choice.index,choice.name,std::nullopt});
            }
        } else {
            d.current=slot ? setup_.opponents[slot-1].color : setup_.color.value_or(-999);
            for (const auto& choice : colors_) {
                bool used=false;
                if (choice.index!=-2) {
                    if (slot && setup_.color==choice.index) used=true;
                    for (std::size_t i=0;i<setup_.opponents.size();++i) {
                        const auto& other=setup_.opponents[i];
                        if (slot==i+1 || !other.opponent || *other.opponent==Opponent::Closed) continue;
                        if (other.color==choice.index) used=true;
                    }
                }
                if (!used) d.choices.push_back({choice.index,choice.name,choice.swatch});
            }
        }
        result.push_back(std::move(d));
    }
    return result;
}
Rect Menu::dropdown_list_bounds(const Dropdown& d) const {
    const auto row=static_cast<int>(combo_item_metrics(font_).list_height);
    const int height=static_cast<int>(d.choices.size())*row+4;
    // Linux keeps the popup inside the 800x600 client area.
    const int y=std::min(d.bounds.y+d.bounds.height,600-height);
    return {d.bounds.x,std::max(0,y),d.bounds.width,height};
}
std::optional<Rect> Menu::slider_bounds(std::uint32_t id) const {
    const auto v=slider_value(setup_,id);
    if (confirming_exit_ || choosing_map_ || page()!=Page::Skirmish || !v.value ||
        v.minimum<0 || v.maximum>65535 || v.minimum>=v.maximum || v.increment<=0) return std::nullopt;
    for (const auto& control : skirmish_.controls) {
        if (control.id!=id || !(control.style&0x10000000u) || (control.style&0x08000000u)) continue;
        return Rect{(control.bounds.x*3+1)/2, (control.bounds.y*163+50)/100,
                    (control.bounds.width*3+1)/2+1, (control.bounds.height*163+50)/100+1};
    }
    return std::nullopt;
}
void Menu::move_slider(std::uint32_t id, int x) {
    const auto bounds=slider_bounds(id);
    if (!bounds) return;
    const auto v=slider_value(setup_,id);
    const auto local=std::clamp<std::int64_t>(std::int64_t(x)-bounds->x,
        std::numeric_limits<int>::min(),std::numeric_limits<int>::max());
    const auto next=credit_from_slider_pointer(static_cast<int>(local),bounds->width,
                                              v.minimum,v.maximum,v.increment);
    if (next && v.value!=next) {
        if (id==SkirmishControls::speed) setup_.game_speed=6-*next;
        else if (id==SkirmishControls::units) setup_.unit_count=next;
        else setup_.credits=next;
        slider_changed_=true;
    }
}

void Menu::pointer_move(int x, int y) {
    if (is_slider(held_)) move_slider(*held_,x);
    hovered_.reset();
    for (const auto& b : buttons()) {
        if (b.enabled && x >= b.bounds.x && y >= b.bounds.y &&
            x < b.bounds.x + b.bounds.width && y < b.bounds.y + b.bounds.height) {
            hovered_ = b.id;
            break;
        }
    }
    if (!hovered_) for (const auto& b : checks()) {
        if (b.enabled && x>=b.bounds.x && y>=b.bounds.y &&
            x<b.bounds.x+b.bounds.width && y<b.bounds.y+b.bounds.height) {
            hovered_=b.id;
            break;
        }
    }
}
void Menu::pointer_down(int x, int y) {
    if (choosing_map_) {
        for (const auto id : {1771u,1363u}) if (const auto list=map_list_bounds(id)) {
            if (!contains(*list,x,y) || y<list->y+2 || y>=list->y+list->height-2) continue;
            const auto count=id==1771 ? modes_.size() : visible_map_indices().size();
            const auto rows=static_cast<std::size_t>(std::max(1,(list->height-4)/23));
            if (count>rows && x>=list->x+list->width-20) {
                held_map_scroll_=true;
                if (y<list->y+24) pointer_scroll(x,y,1);
                else if (y>=list->y+list->height-24) pointer_scroll(x,y,-1);
                return;
            }
            const auto row=static_cast<std::size_t>((y-list->y-2)/23);
            held_map_list_=id;
            held_map_row_=row+(id==1771 ? mode_scroll_ : map_scroll_);
            return;
        }
    }
    const auto drops=dropdowns();
    if (open_dropdown_) {
        held_dropdown_item_=-1;
        for (const auto& d : drops) if (d.id==*open_dropdown_) {
            const auto list=dropdown_list_bounds(d);
            if (contains(list,x,y)) {
                const auto row=static_cast<int>(combo_item_metrics(font_).list_height);
                const int item=(y-list.y-2)/row;
                if (y>=list.y+2 && item>=0 && static_cast<std::size_t>(item)<d.choices.size())
                    held_dropdown_item_=item;
            }
            break;
        }
        return;
    }
    for (const auto& d : drops) if (d.enabled && contains(d.bounds,x,y)) {
        held_dropdown_=d.id;
        return;
    }
    pointer_move(x, y);
    held_ = hovered_;
    for (auto id : {SkirmishControls::credits,SkirmishControls::speed,SkirmishControls::units}) {
        const auto slider=slider_bounds(id);
        if (slider && x>=slider->x && x<slider->x+slider->width-50 &&
            y>slider->y+slider->height-18 && y<slider->y+slider->height) {
            held_=id;
            slider_changed_=false;
            move_slider(id,x);
        }
    }
}
void Menu::pointer_scroll(int x, int y, int delta) {
    if (!choosing_map_ || !delta) return;
    for (const auto id : {1771u,1363u}) if (const auto list=map_list_bounds(id)) {
        if (!contains(*list,x,y)) continue;
        auto& offset=id==1771 ? mode_scroll_ : map_scroll_;
        const auto count=id==1771 ? modes_.size() : visible_map_indices().size();
        const auto rows=static_cast<std::size_t>(std::max(1,(list->height-4)/23));
        const auto maximum=count>rows ? count-rows : 0;
        // Do not negate INT_MIN in int arithmetic.
        const auto next=std::int64_t(offset)-std::int64_t(delta);
        offset=static_cast<std::size_t>(std::clamp<std::int64_t>(next,0,static_cast<std::int64_t>(maximum)));
        return;
    }
}
Action Menu::pointer_up(int x, int y) {
    if (held_map_scroll_) {held_map_scroll_=false;return Action::None;}
    if (held_map_list_) {
        const auto id=*held_map_list_;
        const auto held=held_map_row_;
        held_map_list_.reset(); held_map_row_.reset();
        const auto list=map_list_bounds(id);
        if (!list || !held || !contains(*list,x,y) || y<list->y+2 || y>=list->y+list->height-2)
            return Action::None;
        const auto row=static_cast<std::size_t>((y-list->y-2)/23)+(id==1771 ? mode_scroll_ : map_scroll_);
        if (row!=*held) return Action::None;
        if (id==1771 && row<modes_.size()) {
            if (pending_mode_!=modes_[row].id) {pending_mode_=modes_[row].id;pending_map_.reset();map_scroll_=0;}
        } else if (id==1363) {
            const auto indices=visible_map_indices();
            if (row<indices.size()) pending_map_=indices[row];
        }
        return Action::None;
    }
    if (held_dropdown_item_) {
        const int held=*held_dropdown_item_;
        held_dropdown_item_.reset();
        for (const auto& d : dropdowns()) if (open_dropdown_ && d.id==*open_dropdown_) {
            const auto list=dropdown_list_bounds(d);
            open_dropdown_.reset();
            if (held<0 || !contains(list,x,y) || y<list.y+2) return Action::None;
            const int row=static_cast<int>(combo_item_metrics(font_).list_height);
            const int item=(y-list.y-2)/row;
            if (item!=held || static_cast<std::size_t>(item)>=d.choices.size()) return Action::None;
            const int value=d.choices[item].value;
            if (d.kind==0) return select_opponent(d.slot,static_cast<Opponent>(value));
            if (d.kind==1) return select_country(d.slot,value);
            return select_color(d.slot,value);
        }
        open_dropdown_.reset();
        return Action::None;
    }
    if (held_dropdown_) {
        const auto held=*held_dropdown_;
        held_dropdown_.reset();
        for (const auto& d : dropdowns()) if (d.id==held && d.enabled && contains(d.bounds,x,y)) {
            open_dropdown_=held;
            return Action::None;
        }
        return Action::None;
    }
    pointer_move(x, y);
    if (is_slider(held_)) {
        held_.reset();
        const bool changed=slider_changed_;
        slider_changed_=false;
        return changed ? Action::SetupChanged : Action::None;
    }
    if (!held_ || !hovered_ || *held_ != *hovered_) {
        held_.reset();
        return Action::None;
    }
    const auto activated = *held_;
    held_.reset();
    if (choosing_map_) {
        if (activated==1472) {choosing_map_=false;hovered_.reset();return Action::None;}
        if (activated==1733 && pending_mode_ && pending_map_) {
            const auto mode=select_mode(*pending_mode_);
            const auto map=select_map(*pending_map_);
            choosing_map_=false;hovered_.reset();
            return mode==Action::SetupChanged || map==Action::SetupChanged ? Action::SetupChanged : Action::None;
        }
        return Action::None;
    }
    if (!confirming_exit_ && page()==Page::Skirmish && activated==1450) {
        choosing_map_=true;
        pending_mode_.reset(); pending_map_.reset(); mode_scroll_=0;map_scroll_=0;
        const auto saved=std::find_if(modes_.begin(),modes_.end(),[&](const auto& mode){return setup_.game_mode==mode.id;});
        if (saved!=modes_.end()) pending_mode_=saved->id;
        else if (!modes_.empty()) pending_mode_=modes_.front().id;
        const auto indices=visible_map_indices();
        for (auto index : indices) if (setup_.map_filename==maps_[index].filename &&
                                        setup_.map_archive==maps_[index].source_archive) {
            pending_map_=index;break;
        }
        hovered_.reset();return Action::None;
    }
    if (!confirming_exit_ && !choosing_map_ && page()==Page::Skirmish) {
        auto* value=option(setup_,activated);
        if (value && value->has_value()) {
            *value = !**value;
            return Action::SetupChanged;
        }
    }
    if (confirming_exit_) {
        confirming_exit_ = false;
        hovered_.reset();
        return activated == 1454 ? Action::Quit : Action::None;
    }
    // 0x00532037 requests exit. 0x0052DDBA opens confirmation resource 288.
    if (page() == Page::Main && activated == 1006) {
        confirming_exit_ = true;
        hovered_.reset();
        return Action::None;
    }
    if (prototype_start_ && page()==Page::Skirmish && activated==SkirmishControls::start)
        return Action::StartMatch;
    auto result = navigation_.activate(activated);
    if (result == Action::PageChanged) hovered_.reset();
    return result;
}
void Menu::return_from_match() noexcept {
    navigation_=MenuNavigation{};
    confirming_exit_=choosing_map_=held_map_scroll_=slider_changed_=false;
    pending_mode_.reset();pending_map_.reset();held_map_list_.reset();held_map_row_.reset();
    hovered_.reset();held_.reset();open_dropdown_.reset();held_dropdown_.reset();held_dropdown_item_.reset();
}
Action Menu::key_down(Key key) noexcept {
    // 0x005D4D50 routes dialog keys through IsDialogMessageA; Escape sends
    // IDCANCEL (2), which 0x005D3711 maps to the cancel result.
    if (choosing_map_ && key==Key::Escape) {
        choosing_map_=false; held_map_scroll_=false; held_map_list_.reset(); held_map_row_.reset(); hovered_.reset(); held_.reset();
        return Action::None;
    }
    if (open_dropdown_ && key == Key::Escape) {
        open_dropdown_.reset(); held_dropdown_.reset(); held_dropdown_item_.reset();
        return Action::None;
    }
    if (confirming_exit_ && key == Key::Escape) {
        confirming_exit_ = false;
        hovered_.reset(); held_.reset();
    }
    return Action::None;
}
void Menu::configure_maps(std::vector<SkirmishMap> maps, std::vector<SkirmishMode> modes) {
    choosing_map_=false; pending_map_.reset(); pending_mode_.reset();
    maps_=std::move(maps);
    modes_=std::move(modes);
    setup_.map_filename.reset();
    setup_.map_archive.reset();
    // Default to an available map from the mounted base catalog.
    if(prototype_start_) {
        const auto mode=std::find_if(modes_.begin(),modes_.end(),[](const auto& item){return item.id==1;});
        if(mode==modes_.end())return;
        const auto playable=[&](const auto& item) {
            return item.built_in && item.available && item.source_archive.empty() &&
                skirmish_map_matches_mode(item,*mode);
        };
        auto map=std::find_if(maps_.begin(),maps_.end(),[&](const auto& item) {
            return playable(item) && item.filename=="MP02T2.MAP";
        });
        if(map==maps_.end())map=std::find_if(maps_.begin(),maps_.end(),playable);
        if(map!=maps_.end()) {
            setup_.game_mode=1;setup_.map_filename=map->filename;setup_.map_archive=map->source_archive;
        }
    }
}
Action Menu::select_mode(int id) {
    if (page()!=Page::Skirmish || confirming_exit_) return Action::None;
    const auto mode=std::find_if(modes_.begin(),modes_.end(),[id](const auto& item){return item.id==id;});
    if (mode==modes_.end() || setup_.game_mode==id) return Action::None;
    setup_.game_mode=id;
    // Original fallback selection is not yet recovered. Require an explicit map.
    setup_.map_filename.reset();
    setup_.map_archive.reset();
    return Action::SetupChanged;
}
Action Menu::select_map(std::size_t index) {
    if (page()!=Page::Skirmish || confirming_exit_ || index>=maps_.size() || !setup_.game_mode)
        return Action::None;
    const auto mode=std::find_if(modes_.begin(),modes_.end(),[&](const auto& item){return item.id==*setup_.game_mode;});
    const auto& map=maps_[index];
    if (mode==modes_.end() || !map.available || map.filename.empty() ||
        !skirmish_map_matches_mode(map,*mode)) return Action::None;
    const auto archive=map.source_archive;
    if (setup_.map_filename==map.filename && setup_.map_archive==archive) return Action::None;
    setup_.map_filename=map.filename;
    setup_.map_archive=archive;
    // The saved scenario index uses an unresolved filtered and sorted list.
    // Preserve it until its original mapping is recovered.
    return Action::SetupChanged;
}
Action Menu::select_opponent(std::size_t slot, Opponent value) noexcept {
    if (page()!=Page::Skirmish || confirming_exit_ || slot<1 || slot>setup_.opponents.size())
        return Action::None;
    if (value!=Opponent::Closed && value!=Opponent::Easy && value!=Opponent::Medium && value!=Opponent::Hard)
        return Action::None;
    auto& target=setup_.opponents[slot-1];
    bool changed=target.opponent!=value;
    target.opponent=value;
    // 0x006ADC20 resets the closed slot and disables its other controls.
    if (value==Opponent::Closed) {
        changed=changed || target.country!=-2 || target.color!=-2;
        target.country=-2;
        target.color=-2;
    }
    return changed ? Action::SetupChanged : Action::None;
}
Action Menu::select_country(std::size_t slot, int index) {
    if (page()!=Page::Skirmish || confirming_exit_ || slot>setup_.opponents.size()) return Action::None;
    const auto choice=std::find_if(countries_.begin(),countries_.end(),
                                   [index](const auto& country){return country.index==index;});
    if (choice==countries_.end()) return Action::None;
    if (slot==0) {
        if (setup_.country==choice->id) return Action::None;
        setup_.country=choice->id;
        return Action::SetupChanged;
    }
    auto& target=setup_.opponents[slot-1];
    if (!target.opponent || *target.opponent==Opponent::Closed || target.country==index) return Action::None;
    target.country=index;
    return Action::SetupChanged;
}
Action Menu::select_color(std::size_t slot, int index) noexcept {
    if (page()!=Page::Skirmish || confirming_exit_ || slot>setup_.opponents.size() ||
        (index!=-2 && (index<0 || index>7))) return Action::None;
    if (slot && (!setup_.opponents[slot-1].opponent ||
                 *setup_.opponents[slot-1].opponent==Opponent::Closed)) return Action::None;
    // 0x004E4C20 reserves each selected color for one active player.
    if (index!=-2) {
        if (slot!=0 && setup_.color==index) return Action::None;
        for (std::size_t i=0;i<setup_.opponents.size();++i) {
            const auto& other=setup_.opponents[i];
            if (slot==i+1 || !other.opponent || *other.opponent==Opponent::Closed) continue;
            if (other.color==index) return Action::None;
        }
    }
    if (slot==0) {
        if (setup_.color==index) return Action::None;
        setup_.color=index;
    } else {
        auto& target=setup_.opponents[slot-1];
        if (target.color==index) return Action::None;
        target.color=index;
    }
    return Action::SetupChanged;
}

// PreviewPack stores the original map thumbnail as RGB bytes.
const gfx::RgbaImage& Menu::map_preview() const {
    const auto key=setup_.map_archive.value_or("")+":"+setup_.map_filename.value_or("");
    if(key==preview_key_)return preview_;
    preview_key_=key;preview_={};
    if(!preview_files_ || !setup_.map_filename)return preview_;
    try {
        auto bytes=preview_files_->read_file(*setup_.map_filename);
        if(setup_.map_archive && !setup_.map_archive->empty()) {
            auto archive=fs::MixArchive::open(preview_files_->directory()/ *setup_.map_archive);
            if(const auto* entry=archive->find(*setup_.map_filename))bytes=archive->read(*entry);
        }
        if(!bytes)return preview_;
        rules::IniFile ini;
        ini.parse(std::string_view(reinterpret_cast<const char*>(bytes->data()),bytes->size()));
        const auto size=ini.read_list("Preview","Size");
        if(size.size()!=4)return preview_;
        const auto width=rules::IniFile::integer_value(size[2]);
        const auto height=rules::IniFile::integer_value(size[3]);
        if(!width || !height || *width<=0 || *height<=0 || *width>2048 || *height>2048)return preview_;
        const auto rgb=pack::read_section(ini,"PreviewPack",pack::Codec::Lzo);
        if(rgb.size()!=std::size_t(*width)* *height*3)return preview_;
        preview_.width=std::uint16_t(*width);preview_.height=std::uint16_t(*height);
        preview_.pixels.reserve(std::size_t(*width)* *height*4);
        for(std::size_t i=0;i<rgb.size();i+=3) {
            preview_.pixels.insert(preview_.pixels.end(),{rgb[i],rgb[i+1],rgb[i+2],255});
        }
    } catch(const std::exception&) {preview_={};}
    return preview_;
}

gfx::RgbaImage Menu::render() const {
    auto image = background_;
    // Draw the main page first, then the original confirmation panel.
    // A copy clears the modal flag only while drawing the underlying page.
    if (confirming_exit_) {
        Menu behind = *this;
        behind.confirming_exit_ = false;
        behind.hovered_.reset(); behind.held_.reset();
        image = behind.render();
        blit(image, popup_, 175, 137, 451, 326);
        // Resource 288 uses a left-aligned static control at (40,40).
        // 0x00615A91 selects left alignment and 0x00615DF7 selects yellow.
        font_.draw_text(image, exit_prompt_, 235, 202, {255,255,0,255});
    } else {
        if (choosing_map_ && map_background_.width) blit(image,map_background_,0,0);
        const Dialog* dialog = &main_;
        if (page() == Page::SinglePlayer) dialog = &single_;
        if (page() == Page::Skirmish) dialog = choosing_map_ ? &map_dialog_ : &skirmish_;
        for (const auto& control : dialog->controls) {
            if (control.id != 1684 || control.label.empty()) continue;
            // 0x0060B1D0 centers the heading in the 168-pixel panel;
            // 0x0060B950 adds seven pixels to its normalized Y.
            const int width = static_cast<int>(font_.text_width(control.label));
            font_.draw_text(image, control.label, 716 - width / 2, 8,
                            {255,255,0,255});
            break;
        }
    }
    for (const auto& b : buttons()) {
        const bool hot = b.enabled && hovered_ == b.id;
        const bool down = hot && held_ == b.id;
        const auto& frame = confirming_exit_ ? (down ? popup_pressed_ : (hot ? popup_hover_ : popup_normal_)) :
                                                (down ? pressed_ : (hot ? hover_ : normal_));
        blit(image, frame, b.bounds.x, b.bounds.y);
        // 0x0060F9A0 sets the normal text color to COLORREF 0x00ffff.
        // 0x0072A8C0 sets the disabled shell color to RGB (72,0,0).
        // 0x006135D1 centers the caption; pressed text moves by (2,4).
        const auto color = b.enabled ? gfx::Rgba{255,255,0,255} : gfx::Rgba{72,0,0,255};
        const int width = static_cast<int>(font_.text_width(b.label));
        const int x = b.bounds.x + (b.bounds.width - width) / 2 + (down ? 2 : 0);
        const int y = b.bounds.y + 1 + (b.bounds.height - 1 - static_cast<int>(font_.line_height())) / 2 + (down ? 4 : 0);
        font_.draw_text(image, b.label, x, y, color);
    }
    for (const auto& c : checks()) {
        const auto* value=option(setup_,c.id);
        auto icon=value && value->value_or(false) ? check_on_ : check_off_;
        if (!c.enabled) {
            // 0x00616623 blends disabled checkbox art toward black with 0x60.
            for (std::size_t p=0;p<icon.pixels.size();p+=4)
                for (int channel=0;channel<3;++channel)
                    icon.pixels[p+channel]=static_cast<std::uint8_t>(icon.pixels[p+channel]*159/256);
        }
        blit(image,icon,c.bounds.x,c.bounds.y);
        // 0x00616641 places the label 26 pixels after the checkbox.
        const auto color=c.enabled ? gfx::Rgba{255,255,0,255} : gfx::Rgba{159,0,0,255};
        const int y=c.bounds.y+(c.bounds.height-static_cast<int>(font_.line_height()))/2;
        font_.draw_text(image,c.label,c.bounds.x+26,y,color);
    }
    if (!confirming_exit_ && !choosing_map_ && page()==Page::Skirmish) {
        const auto& preview=map_preview();
        if(preview.width && preview.height) {
            const int width=std::min(280,155*int(preview.width)/int(preview.height));
            const int height=width*int(preview.height)/int(preview.width);
            for(int y=0;y<height;++y)for(int x=0;x<width;++x) {
                const auto from=(std::size_t(y*preview.height/height)*preview.width+x*preview.width/width)*4;
                const auto to=(std::size_t(y+365)*image.width+x+44)*4;
                std::copy_n(preview.pixels.begin()+from,4,image.pixels.begin()+to);
            }
        }
        for (const auto& control : skirmish_.controls) {
            // Base 0x005C34B0 writes the selected map name to control 0x5A8.
            if (control.id!=1448 || !setup_.map_filename) continue;
            const auto map=std::find_if(maps_.begin(),maps_.end(),[&](const auto& m) {
                return m.filename==setup_.map_filename && m.source_archive==setup_.map_archive.value_or("");
            });
            if (map==maps_.end()) continue;
            const int width=(control.bounds.width*3+1)/2;
            const int height=(control.bounds.height*163+50)/100;
            gfx::RgbaImage name{static_cast<std::uint16_t>(width),static_cast<std::uint16_t>(height),
                               std::vector<std::uint8_t>(width*height*4)};
            font_.draw_text(name,map->name,0,0,{255,255,0,255});
            blit(image,name,(control.bounds.x*3+1)/2,(control.bounds.y*163+50)/100);
        }
        for (const auto& control : skirmish_.controls) {
            const auto id=control.id;
            if (id!=1689 && id!=1691 && id!=1692 &&
                id!=1937 && id!=1938 && id!=1939 && id!=1940 && id!=1942) continue;
            if (!(control.style&0x10000000u) || control.label.empty()) continue;
            // 0x00615A91 applies the static control's alignment bits.
            const int x=(control.bounds.x*3+1)/2;
            const int y=(control.bounds.y*163+50)/100;
            font_.draw_text(image,control.label,x,y,{255,255,0,255});
        }
        for (auto id : {SkirmishControls::credits,SkirmishControls::speed,SkirmishControls::units})
        if (const auto slider=slider_bounds(id)) {
            const auto v=slider_value(setup_,id);
            // 0x0061DE9E draws the original value trough. 0x0061E042
            // draws the 12-pixel grip at the scaled current value.
            const int value_x=slider->x+slider->width-49;
            blit_strip(image,slider_middle_,slider->x,slider->y-1,slider->width-50);
            blit(image,slider_left_,slider->x,slider->y-1);
            blit(image,slider_right_,value_x-1-slider_right_.width,slider->y-1);
            blit_strip(image,slider_middle_,value_x,slider->y-1,50);
            blit(image,slider_left_,value_x,slider->y-1);
            blit(image,slider_right_,slider->x+slider->width-slider_right_.width+1,slider->y-1);
            const auto value=std::clamp(*v.value,v.minimum,v.maximum);
            const auto travel=slider->width-63;
            const auto position=static_cast<int>(std::int64_t(travel)*(value-v.minimum)/
                                                  (v.maximum-v.minimum));
            blit(image,slider_grip_,slider->x+1+position,slider->y);
            const auto text=std::to_string(value/v.increment*v.increment);
            const int text_x=value_x+(50-static_cast<int>(font_.text_width(text)))/2;
            const int text_y=slider->y+(slider->height-static_cast<int>(font_.line_height()))/2;
            font_.draw_text(image,text,text_x,text_y,{255,255,0,255});
        }
    }
    if (!confirming_exit_ && !choosing_map_ && page()==Page::Skirmish) {
        const auto drops=dropdowns();
        auto draw_choice=[&](Rect bounds,const DropChoice& choice,bool enabled) {
            if (choice.swatch) {
                fill(image,{bounds.x+2,bounds.y+3,std::max(0,bounds.width-4),bounds.height-6},*choice.swatch);
            } else if (bounds.width>4 && bounds.height>0) {
                // Clip long names to the control, as the original text painter does.
                gfx::RgbaImage text{static_cast<std::uint16_t>(bounds.width-4),
                                    static_cast<std::uint16_t>(bounds.height),
                                    std::vector<std::uint8_t>(static_cast<std::size_t>(bounds.width-4)*bounds.height*4)};
                font_.draw_text(text,choice.label,0,std::max(0,(bounds.height-static_cast<int>(font_.line_height()))/2),
                                enabled ? gfx::Rgba{255,255,0,255} : gfx::Rgba{159,0,0,255});
                blit(image,text,bounds.x+2,bounds.y);
            }
        };
        for (const auto& d : drops) {
            // Native two-pixel frame. 0x00617886 draws the original frame;
            // its Windows outer bounds are not recovered.
            fill(image,d.bounds,{104,122,128,255});
            fill(image,{d.bounds.x+2,d.bounds.y+2,d.bounds.width-4,d.bounds.height-4},{0,0,0,255});
            const auto current=std::find_if(d.choices.begin(),d.choices.end(),[&](const auto& c){return c.value==d.current;});
            if (current!=d.choices.end())
                draw_choice({d.bounds.x+2,d.bounds.y+2,d.bounds.width-24,d.bounds.height-4},*current,d.enabled);
            // 0x0061791D places the original down-arrow image at the right edge.
            blit(image,held_dropdown_==d.id ? arrow_pressed_ : arrow_normal_,
                 d.bounds.x+d.bounds.width-19,d.bounds.y+1);
        }
        if (open_dropdown_) for (const auto& d : drops) if (d.id==*open_dropdown_) {
            const auto list=dropdown_list_bounds(d);
            const int row=static_cast<int>(combo_item_metrics(font_).list_height);
            fill(image,list,{104,122,128,255});
            fill(image,{list.x+2,list.y+2,list.width-4,list.height-4},{0,0,0,255});
            for (std::size_t i=0;i<d.choices.size();++i) {
                Rect r{list.x+2,list.y+2+static_cast<int>(i)*row,list.width-4,row};
                if (d.choices[i].value==d.current) fill(image,r,{80,80,80,255});
                draw_choice(r,d.choices[i],d.enabled);
            }
            break;
        }
    }
    if (choosing_map_) {
        for (const auto& c : map_dialog_.controls) {
            if (c.label_key!="GUI:SelectEngagement" && c.label_key!="GUI:GameType" &&
                c.label_key!="GUI:GameMap") continue;
            const int x=(c.bounds.x*3+1)/2, y=(c.bounds.y*163+50)/100;
            const int width=(c.bounds.width*3+1)/2;
            font_.draw_text(image,c.label,x+(width-static_cast<int>(font_.text_width(c.label)))/2,y,{255,255,0,255});
        }
        const auto map_indices=visible_map_indices();
        for (const auto id : {1771u,1363u}) if (const auto list=map_list_bounds(id)) {
            // Windows list borders and scrollbars are platform-owned. Linux
            // uses a two-pixel frame and a 23-pixel native row.
            fill(image,*list,{104,122,128,255});
            fill(image,{list->x+2,list->y+2,list->width-4,list->height-4},{0,0,0,255});
            const auto offset=id==1771 ? mode_scroll_ : map_scroll_;
            const auto count=id==1771 ? modes_.size() : map_indices.size();
            const auto rows=static_cast<std::size_t>(std::max(0,(list->height-4)/23));
            const bool scrollbar=count>rows;
            if (scrollbar) {
                const int bar_x=list->x+list->width-20;
                fill(image,{bar_x,list->y+2,18,list->height-4},{48,48,48,255});
                blit(image,arrow_up_,bar_x,list->y+2);
                blit(image,arrow_normal_,bar_x,list->y+list->height-24);
            }
            for (std::size_t row=0;row<rows && offset+row<count;++row) {
                const auto index=offset+row;
                const bool selected=id==1771 ? pending_mode_==modes_[index].id : pending_map_==map_indices[index];
                const auto& label=id==1771 ? modes_[index].name : maps_[map_indices[index]].name;
                Rect r{list->x+2,list->y+2+static_cast<int>(row)*23,list->width-(scrollbar ? 22 : 4),23};
                if (selected) fill(image,r,{80,80,80,255});
                if (r.width<=4) continue;
                gfx::RgbaImage text{static_cast<std::uint16_t>(r.width-4),23,
                    std::vector<std::uint8_t>(static_cast<std::size_t>(r.width-4)*23*4)};
                font_.draw_text(text,label,0,3,{255,255,0,255});
                blit(image,text,r.x+2,r.y);
            }
        }
    }
    return image;
}
} // namespace ra2::ui
