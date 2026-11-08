#include "map_file.h"

namespace ra2map {

namespace {
using Ini = ra2::rules::IniFile;

// [Waypoints]/[Terrain] values are cell coords packed as X*1000 + Y.
// Verified from the game's own decoder (FUN_0068bdc0): value/1000 and
// value%1000, each stored as u16.
Cell parse_cell_number(std::string_view text) {
    Cell c{};
    const auto v = Ini::integer_value(Ini::trim(text));
    if (!v || *v < 0) return c;
    c.x = static_cast<std::uint16_t>(*v / 1000);
    c.y = static_cast<std::uint16_t>(*v % 1000);
    return c;
}

Cell parse_cell_pair(std::string_view a, std::string_view b) {
    Cell c{};
    if (const auto x = Ini::integer_value(Ini::trim(a))) {
        c.x = static_cast<std::uint16_t>(*x);
    }
    if (const auto y = Ini::integer_value(Ini::trim(b))) {
        c.y = static_cast<std::uint16_t>(*y);
    }
    return c;
}

std::string field(std::string_view value, std::size_t index) {
    const auto parts = Ini::split_list(value);
    return index < parts.size() ? parts[index] : std::string{};
}

std::int32_t field_int(std::string_view value, std::size_t index,
                       std::int32_t fallback) {
    const auto parts = Ini::split_list(value);
    if (index >= parts.size()) return fallback;
    const auto n = Ini::integer_value(Ini::trim(parts[index]));
    return n ? *n : fallback;
}
} // namespace

void MapFile::parse(std::string_view text) {
    structures_.clear();
    units_.clear();
    infantry_.clear();
    aircraft_.clear();
    terrain_.clear();
    waypoints_.clear();
    iso_pack_.clear();
    overlay_pack_.clear();

    Ini ini;
    ini.parse(text);
    stats_ = ini.statistics();

    // [Map]
    theater_ = ini.read_string("Map", "Theater", "TEMPERATE");
    const auto dims = Ini::split_list(ini.read_string("Map", "Size", "0,0,0,0"));
    const auto dim = [&](std::size_t i) {
        return i < dims.size() ? Ini::integer_value(Ini::trim(dims[i])).value_or(0) : 0;
    };
    size_left_ = dim(0);
    size_top_ = dim(1);
    size_right_ = dim(2);
    size_bottom_ = dim(3);

    // [Basic]
    next_scenario_ = ini.read_string("Basic", "NextScenario", "");
    alt_next_scenario_ = ini.read_string("Basic", "AltNextScenario", "");
    carry_over_cap_ = ini.read_bool("Basic", "CarryOverCap", false);

    // [Structures]: ID=House,Type,Health,CellX,CellY,Facing,...
    if (const Ini::Section* s = ini.find_section("Structures")) {
        for (const auto& e : s->entries) {
            Structure st;
            st.house = field(e.value, 0);
            st.type = field(e.value, 1);
            st.health = field_int(e.value, 2, 256);
            st.cell = parse_cell_pair(field(e.value, 3), field(e.value, 4));
            st.facing = field_int(e.value, 5, 0);
            st.tag = field(e.value, 6);
            structures_.push_back(std::move(st));
        }
    }

    // [Units]: house,type,health,cellX,cellY,facing,mission,...
    if (const Ini::Section* s = ini.find_section("Units")) {
        for (const auto& e : s->entries) {
            Unit u;
            u.house = field(e.value, 0);
            u.type = field(e.value, 1);
            u.health = field_int(e.value, 2, 256);
            u.cell = parse_cell_pair(field(e.value, 3), field(e.value, 4));
            u.facing = field_int(e.value, 5, 0);
            u.mission = field(e.value, 6);
            units_.push_back(std::move(u));
        }
    }

    // [Infantry]: house,type,health,cellX,cellY,subcell,facing,mission,...
    if (const Ini::Section* s = ini.find_section("Infantry")) {
        for (const auto& e : s->entries) {
            Infantry inf;
            inf.house = field(e.value, 0);
            inf.type = field(e.value, 1);
            inf.health = field_int(e.value, 2, 256);
            inf.cell = parse_cell_pair(field(e.value, 3), field(e.value, 4));
            inf.subcell = field_int(e.value, 5, 0);
            inf.facing = field_int(e.value, 6, 0);
            inf.mission = field(e.value, 7);
            infantry_.push_back(std::move(inf));
        }
    }

    // [Aircraft]: house,type,health,cellX,cellY,facing,mission,...
    if (const Ini::Section* s = ini.find_section("Aircraft")) {
        for (const auto& e : s->entries) {
            Aircraft a;
            a.house = field(e.value, 0);
            a.type = field(e.value, 1);
            a.health = field_int(e.value, 2, 256);
            a.cell = parse_cell_pair(field(e.value, 3), field(e.value, 4));
            a.facing = field_int(e.value, 5, 0);
            a.mission = field(e.value, 6);
            aircraft_.push_back(std::move(a));
        }
    }

    // [Terrain]: cellNumber=type
    if (const Ini::Section* s = ini.find_section("Terrain")) {
        for (const auto& e : s->entries) {
            TerrainObject t;
            t.cell = parse_cell_number(e.key);
            t.type = e.value;
            terrain_.push_back(std::move(t));
        }
    }

    // [Waypoints]: index=cellNumber
    if (const Ini::Section* s = ini.find_section("Waypoints")) {
        for (const auto& e : s->entries) {
            Waypoint wp;
            wp.index = Ini::integer_value(e.key).value_or(0);
            wp.cell = parse_cell_number(e.value);
            waypoints_.push_back(std::move(wp));
        }
    }

    // Raw pack sections: keep the base64 text as stored.
    if (const Ini::Section* s = ini.find_section("IsoMapPack5")) {
        for (const auto& e : s->entries) iso_pack_ += e.value;
    }
    if (const Ini::Section* s = ini.find_section("OverlayPack")) {
        for (const auto& e : s->entries) overlay_pack_ += e.value;
    }
}

} // namespace ra2map
