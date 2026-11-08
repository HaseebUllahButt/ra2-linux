#include "bootstrap_hook.h"

#include <cstdio>
#include <exception>
#include <limits>
#ifdef _WIN32
#include <windows.h>
#endif

namespace ra2hook {
namespace {
void log_line(const BridgeApi& api, const char* line) {
    if (api.log) api.log(api.context, line);
}
void mounted(const BridgeApi& api, const char* name, GamePtr object) {
    char line[192];
    std::snprintf(line, sizeof(line), "[ra2hook] Bootstrap mount %s -> %08X\n", name, object);
    log_line(api, line);
}
} // namespace

int bootstrap_bridge(const std::filesystem::path& directory, const BridgeApi& api) noexcept {
    try {
        log_line(api, "[ra2hook] Bootstrap replacement entered\n");
        if (!api.globals || !api.construct || !api.append || !api.destroy)
            throw ra2::fs::MixError("Incomplete bridge binding");
        // A second initialization would leak native objects and change lookup
        // precedence. Normal startup enters with these six ownership slots empty.
        for (const auto& slot : kBootstrapSlots)
            if (api.globals->generics[slot.index])
                throw ra2::fs::MixError("Bootstrap ownership slot already populated");
        if (api.pump) api.pump(api.context); // Original 004790B0 entry call.

        // Preflight all archives before making any native object. This is the
        // rebuilt implementation of the descending loop and required mounts.
        ra2::fs::MixFileSystem fs(directory);
        fs.bootstrap(ra2::fs::GameVariant::YurisRevenge);
        for (const auto& archive : fs.array()) {
            const auto name = std::filesystem::path(archive->name()).filename().string();
            const GamePtr object = api.construct(api.context, name.c_str(), *archive);
            if (!object) throw ra2::fs::MixError("Native expansion construction failed: " + name);
            // Native constructor has already linked the object into MIXes.
            if (!api.append(api.context, api.globals->array, object)) {
                api.destroy(api.context, object);
                throw ra2::fs::MixError("Native expansion vector append failed");
            }
            mounted(api, name.c_str(), object);
        }
        for (const auto& slot : kBootstrapSlots) {
            const auto& archive = fs.generics().at(slot.name);
            const GamePtr object = api.construct(api.context, slot.name, *archive);
            api.globals->generics[slot.index] = object;
            if (!object) throw ra2::fs::MixError(std::string("Native construction failed: ") + slot.name);
            mounted(api, slot.name, object);
        }
        if (api.pump) api.pump(api.context); // Original success-only exit call.
        char line[192];
        std::snprintf(line, sizeof(line),
            "[ra2hook] Bootstrap success: expansions=%zu generics=6 movies=%d eax=1\n",
            fs.array().size(), api.globals->movies.count);
        log_line(api, line);
        return 1;
    } catch (const std::exception& error) {
        char line[512];
        std::snprintf(line, sizeof(line), "[ra2hook] Bootstrap failure: %s; eax=0\n", error.what());
        // Callbacks, including the logger, are required not to throw.
        log_line(api, line);
        return 0;
    } catch (...) {
        log_line(api, "[ra2hook] Bootstrap failure: unknown exception; eax=0\n");
        return 0;
    }
}

#ifdef _WIN32
static_assert(sizeof(void*) == 4, "The game requires a 32-bit DLL");
namespace {
std::uint8_t* image = nullptr;
template<class T> T at(std::uint32_t va) {
    return reinterpret_cast<T>(image + (va - 0x00400000u));
}
void native_log(void*, const char* line) {
    OutputDebugStringA(line);
    // Called during Bootstrap, outside DllMain / the loader lock.
    if (auto* file = std::fopen("ra2-bootstrap-hook.log", "ab")) {
        std::fputs(line, file);
        std::fclose(file);
    }
}
void native_destroy(void*, GamePtr pointer) {
    // fastcall with a dummy EDX is a thiscall adapter: ECX is `this`,
    // remaining arguments are on the stack and the callee pops them.
    using Delete = void (RA2_FASTCALL *)(void*, void*, unsigned);
    auto* object = reinterpret_cast<GameMix32*>(pointer);
    const auto* vtable = reinterpret_cast<const GamePtr*>(object->vtable);
    reinterpret_cast<Delete>(vtable[0])(object, nullptr, 1); // Native scalar deleting destructor.
}
GamePtr native_construct(void*, const char* name, const ra2::fs::MixArchive& rebuilt) {
    using Allocate = void* (__cdecl *)(std::size_t);
    using Construct = GameMix32* (RA2_FASTCALL *)(void*, void*, const char*, void*);
    void* storage = at<Allocate>(0x007C8E17)(sizeof(GameMix32));
    if (!storage) return 0;
    auto* object = at<Construct>(0x005B3C20)(storage, nullptr, name, at<void*>(0x00886980));
    // Constructor owns filename/header allocation and native list registration.
    // Compare metadata with the rebuilt parser before publishing ownership.
    if (object->count < 0 || static_cast<std::size_t>(object->count) != rebuilt.entries().size() ||
        object->file_size < 0 || static_cast<std::uint32_t>(object->file_size) != rebuilt.data_size()) {
        native_log(nullptr, "[ra2hook] Bootstrap native/rebuilt metadata mismatch\n");
        native_destroy(nullptr, reinterpret_cast<GamePtr>(object));
        return 0;
    }
    return reinterpret_cast<GamePtr>(object);
}
bool native_append(void*, GameVector32& vector, GamePtr object) {
    if (vector.count < 0 || vector.capacity < vector.count) return false;
    if (vector.count >= vector.capacity) {
        if ((!vector.allocated && vector.capacity != 0) || vector.increment <= 0 ||
            vector.capacity > std::numeric_limits<std::int32_t>::max() - vector.increment)
            return false;
        using Resize = bool (RA2_FASTCALL *)(GameVector32*, void*, int, void*);
        const auto* vtable = reinterpret_cast<const GamePtr*>(vector.vtable);
        // SetCapacity is vtable+8, exactly as in FUN_005301a0.
        if (!reinterpret_cast<Resize>(vtable[2])(&vector, nullptr, vector.capacity + vector.increment, nullptr))
            return false;
    }
    if (!vector.items || vector.count >= vector.capacity) return false;
    reinterpret_cast<GamePtr*>(vector.items)[vector.count++] = object;
    return true;
}
void native_pump(void*) {
    using Pump = void (__cdecl *)();
    at<Pump>(0x004790B0)();
}
} // namespace
void configure_bootstrap_hook(std::uint8_t* image_base) noexcept { image = image_base; }
#else
namespace {
const std::filesystem::path* test_directory = nullptr;
const BridgeApi* test_api = nullptr;
}
void bind_bootstrap_test_target(const std::filesystem::path* directory, const BridgeApi* api) noexcept {
    test_directory = directory;
    test_api = api;
}
#endif

extern "C" int RA2_FASTCALL bootstrap_hook(void*, void*) noexcept {
#ifdef _WIN32
    if (!image) return 0;
    const BridgeApi api{nullptr, at<GameGlobals32*>(0x00884D90),
                        native_construct, native_append, native_destroy, native_pump, native_log};
    // Creating a filesystem path may throw; never unwind through a game frame.
    try { return bootstrap_bridge(std::filesystem::current_path(), api); }
    catch (...) { native_log(nullptr, "[ra2hook] Bootstrap failure: current directory; eax=0\n"); return 0; }
#else
    if (!test_directory || !test_api) return 0;
    return bootstrap_bridge(*test_directory, *test_api);
#endif
}
} // namespace ra2hook
