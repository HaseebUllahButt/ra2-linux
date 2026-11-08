#include "bootstrap_hook.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>

using namespace ra2hook;
using Bytes = std::vector<std::uint8_t>;
namespace {
void put16(Bytes& bytes, std::uint16_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value));
    bytes.push_back(static_cast<std::uint8_t>(value >> 8));
}
void put32(Bytes& bytes, std::uint32_t value) {
    for (unsigned i = 0; i != 4; ++i) bytes.push_back(static_cast<std::uint8_t>(value >> (i * 8)));
}
Bytes archive(const std::vector<std::pair<std::string, Bytes>>& files) {
    Bytes result;
    put32(result, 0); // Unencrypted extended MIX, independent of rebuilt writer.
    put16(result, static_cast<std::uint16_t>(files.size()));
    std::uint32_t size = 0;
    for (const auto& file : files) size += static_cast<std::uint32_t>(file.second.size());
    put32(result, size);
    std::uint32_t offset = 0;
    // Native directories use signed hash order; fixtures reproduce that order.
    auto sorted = files;
    std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) {
        return static_cast<std::int32_t>(ra2::fs::crc_filename_hash(a.first)) <
               static_cast<std::int32_t>(ra2::fs::crc_filename_hash(b.first));
    });
    for (const auto& file : sorted) {
        put32(result, ra2::fs::crc_filename_hash(file.first));
        put32(result, offset);
        put32(result, static_cast<std::uint32_t>(file.second.size()));
        offset += static_cast<std::uint32_t>(file.second.size());
    }
    for (const auto& file : sorted) result.insert(result.end(), file.second.begin(), file.second.end());
    return result;
}
Bytes probe(char value) { return archive({{"probe.bin", Bytes{static_cast<std::uint8_t>(value)}}}); }
void write(const std::filesystem::path& path, const Bytes& bytes) {
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    assert(out.good());
}
void fixtures(const std::filesystem::path& directory, bool valid) {
    std::filesystem::create_directories(directory);
    write(directory / "expandmd99.mix", probe('9'));
    write(directory / "EXPANDMD02.MIX", probe('2'));
    write(directory / "expandmd00.mix", probe('0'));
    // Present on disk, but neither overlays nor movies belong to Bootstrap.
    write(directory / "ecache01.mix", probe('x'));
    write(directory / "movmd01.mix", probe('m'));
    write(directory / "ra2md.mix", archive({{"CACHEMD.MIX", probe('c')}, {"LOCALMD.MIX", probe('l')}}));
    // Both base generics must resolve through the rebuilt nested lookup too.
    write(directory / "ra2.mix", valid ? archive({{"CACHE.MIX", probe('C')}, {"LOCAL.MIX", probe('L')}})
                                        : Bytes{0, 0, 0});
}

struct Node32 { GamePtr vtable, next, previous; };
struct List32 { GamePtr vtable; Node32 first, last; };
static_assert(sizeof(List32) == 0x1c);
struct Record { std::string name; GameMix32 wire; std::vector<ra2::fs::MixEntry> headers; };
struct FakeGame {
    GameGlobals32 globals{};
    List32 mixes{};
    std::map<GamePtr, Record> records;
    std::vector<GamePtr> items;
    std::vector<std::string> logs;
    int pumps = 0, constructions = 0, fail_at = 0, destroyed = 0;
    bool fail_append = false;
    static constexpr GamePtr head = 0x00abefdc, tail = 0x00abefe8;
    FakeGame() {
        globals.array.vtable = 0x12345678;
        globals.array.initialized = 1;
        globals.array.increment = 2;
        globals.maps.count = 7;
        globals.movies.count = 4; // Preserve an existing movie state byte-for-byte.
        globals.generics[2] = 0x11112222; // LANGUAGE, owned by another stage.
        globals.generics[3] = 0x33334444; // LANGMD.
        globals.generics[31] = 0x55556666;
        mixes.first.next = tail;
        mixes.last.previous = head;
    }
    BridgeApi api() { return {this, &globals, construct, append, destroy, pump, log}; }
    static GamePtr construct(void* context, const char* name, const ra2::fs::MixArchive& rebuilt) {
        auto& self = *static_cast<FakeGame*>(context);
        if (++self.constructions == self.fail_at) return 0;
        const GamePtr handle = 0x10000u + static_cast<GamePtr>(self.constructions) * 0x100;
        Record record{name, {}, rebuilt.entries()};
        record.wire.count = static_cast<std::int32_t>(record.headers.size());
        record.wire.file_size = static_cast<std::int32_t>(rebuilt.data_size());
        record.wire.file_start = static_cast<std::int32_t>(rebuilt.backing_offset() + rebuilt.data_offset());
        record.wire.previous = self.mixes.last.previous;
        record.wire.next = tail;
        if (record.wire.previous == head) self.mixes.first.next = handle;
        else self.records.at(record.wire.previous).wire.next = handle;
        self.mixes.last.previous = handle;
        self.records.emplace(handle, std::move(record));
        // Demonstrate that the rebuilt layer opened nested archives and can read
        // their actual payloads; a non-null opaque pointer alone is insufficient.
        if (const auto* entry = rebuilt.find("probe.bin")) assert(rebuilt.read(*entry).size() == 1);
        return handle;
    }
    static bool append(void* context, GameVector32& wire, GamePtr object) {
        auto& self = *static_cast<FakeGame*>(context);
        assert(&wire == &self.globals.array);
        if (self.fail_append) return false;
        if (wire.count == wire.capacity) {
            wire.capacity += wire.increment;
            wire.allocated = 1;
            wire.items = 0x200000; // Simulated 32-bit allocation token.
        }
        self.items.push_back(object);
        ++wire.count;
        return true;
    }
    static void destroy(void* context, GamePtr object) {
        auto& self = *static_cast<FakeGame*>(context);
        const auto node = self.records.at(object).wire;
        if (node.previous == head) self.mixes.first.next = node.next;
        else self.records.at(node.previous).wire.next = node.next;
        if (node.next == tail) self.mixes.last.previous = node.previous;
        else self.records.at(node.next).wire.previous = node.previous;
        self.records.erase(object);
        ++self.destroyed;
    }
    static void pump(void* context) { ++static_cast<FakeGame*>(context)->pumps; }
    static void log(void* context, const char* line) {
        static_cast<FakeGame*>(context)->logs.emplace_back(line);
    }
};

int call_entry(const std::filesystem::path& directory, const BridgeApi& api) {
    bind_bootstrap_test_target(&directory, &api);
    using Entry = int (RA2_FASTCALL *)(void*, void*) noexcept;
    const Entry entry = &bootstrap_hook;
    // On i386 these are ECX and EDX; on this ARM64 REFERENCE_SYSTEM this is a logical
    // simulation. Windows disassembly is a separate ABI compilation check.
    const int result = entry(reinterpret_cast<void*>(0xdeadbeefu), reinterpret_cast<void*>(0xcafebabeu));
    bind_bootstrap_test_target(nullptr, nullptr);
    return result;
}
void unchanged_except_owned(GameGlobals32 before, GameGlobals32 after) {
    after.array = before.array;
    for (const auto& slot : kBootstrapSlots) after.generics[slot.index] = before.generics[slot.index];
    assert(std::memcmp(&before, &after, sizeof(before)) == 0);
}
void success(const std::filesystem::path& directory) {
    FakeGame game;
    const auto before = game.globals;
    const auto api = game.api();
    assert(call_entry(directory, api) == 1);
    assert(game.pumps == 2 && game.constructions == 9);
    assert(game.globals.array.vtable == before.array.vtable);
    assert(game.globals.array.count == 3 && game.globals.array.capacity == 4);
    assert(game.globals.array.allocated && game.items.size() == 3);
    std::vector<std::string> order;
    auto previous = FakeGame::head;
    for (auto node = game.mixes.first.next; node != FakeGame::tail;) {
        const auto& record = game.records.at(node);
        assert(record.wire.previous == previous);
        order.push_back(record.name);
        previous = node;
        node = record.wire.next;
    }
    assert(game.mixes.last.previous == previous);
    assert((order == std::vector<std::string>{"expandmd99.mix", "EXPANDMD02.MIX", "expandmd00.mix",
                    "RA2MD.MIX", "RA2.MIX", "CACHEMD.MIX", "CACHE.MIX", "LOCALMD.MIX", "LOCAL.MIX"}));
    for (std::size_t i = 0; i != game.items.size(); ++i)
        assert(game.records.at(game.items[i]).name == order[i]);
    for (const auto& slot : kBootstrapSlots)
        assert(game.records.at(game.globals.generics[slot.index]).name == slot.name);
    for (const auto& slot : kBootstrapSlots)
        if (slot.index >= 20) assert(game.records.at(game.globals.generics[slot.index]).wire.file_start > 32);
    unchanged_except_owned(before, game.globals);
    assert(game.logs.back().find("expansions=3 generics=6 movies=4 eax=1") != std::string::npos);
    std::cout << game.logs.back();

    // Re-entry must not leak duplicates into the live game's list.
    assert(call_entry(directory, api) == 0);
    assert(game.constructions == 9 && game.records.size() == 9 && game.pumps == 2);
}
void failures(const std::filesystem::path& valid, const std::filesystem::path& invalid,
              const std::filesystem::path& missing) {
    for (const auto& directory : {invalid, missing}) {
        FakeGame game;
        const auto before = game.globals;
        assert(call_entry(directory, game.api()) == 0);
        assert(game.constructions == 0 && game.pumps == 1);
        assert(std::memcmp(&before, &game.globals, sizeof(before)) == 0);
        assert(game.logs.back().find("eax=0") != std::string::npos);
    }
    // Preserve published ownership on a later native failure, as the original
    // Bootstrap does. Do not publish later slots or report success.
    FakeGame game;
    game.fail_at = 6; // First required cache, after three expansions and RA2/RA2MD.
    assert(call_entry(valid, game.api()) == 0);
    assert(game.globals.generics[0] && game.globals.generics[1]);
    for (const auto& slot : kBootstrapSlots)
        if (slot.index >= 20) assert(game.globals.generics[slot.index] == 0);
    assert(game.constructions == 6 && game.pumps == 1);

    FakeGame full;
    full.fail_append = true;
    assert(call_entry(valid, full.api()) == 0);
    assert(full.constructions == 1 && full.destroyed == 1 && full.records.empty());
    assert(full.globals.array.count == 0 && full.mixes.first.next == FakeGame::tail);
    assert(bootstrap_hook(nullptr, nullptr) == 0); // No test binding.
}
} // namespace

int main(int argc, char** argv) {
    assert(argc == 2); // Keep every generated fixture under the hook build dir.
    const auto root = std::filesystem::absolute(argv[1]);
    const auto valid = root / "valid", invalid = root / "invalid", missing = root / "missing";
    fixtures(valid, true);
    fixtures(invalid, false);
    std::filesystem::create_directories(missing);
    success(valid);
    failures(valid, invalid, missing);
    std::cout << "Bridge tests passed (32-bit globals, native-list simulation, nested mounts, failures).\n";
}
