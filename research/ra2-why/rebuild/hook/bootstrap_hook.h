#pragma once

#include "../src/fs/mix_bootstrap.h"
#include <cstddef>
#include <cstdint>

#if defined(_MSC_VER) && defined(_M_IX86)
#define RA2_FASTCALL __fastcall
#elif defined(__i386__)
#define RA2_FASTCALL __attribute__((fastcall))
#else
#define RA2_FASTCALL
#endif

namespace ra2hook {
using GamePtr = std::uint32_t;

// Wire layouts, not host C++ containers. GamePtr remains 32 bits on ARM64.
struct GameVector32 {
    GamePtr vtable;
    GamePtr items;
    std::int32_t capacity;
    std::uint8_t initialized, allocated, padding[2];
    std::int32_t count, increment;
};
struct GameGlobals32 {
    GameVector32 array;                   // 00884D90
    GameVector32 maps;                    // 00884DA8
    GameVector32 array_alt;               // 00884DC0
    GamePtr multimd, multi;               // 00884DD8, 00884DDC
    GameVector32 movies;                  // 00884DE0
    GamePtr generics[32];                 // 00884DF8 .. 00884E74
};
struct GameMix32 {
    GamePtr vtable, next, previous, filename;
    std::uint8_t blowfish, encryption, padding[2];
    std::int32_t count, file_size, file_start;
    GamePtr headers;
    std::int32_t field_24;
};
static_assert(sizeof(GameVector32) == 0x18);
static_assert(offsetof(GameVector32, count) == 0x10);
static_assert(offsetof(GameVector32, allocated) == 0x0d);
static_assert(offsetof(GameGlobals32, maps) == 0x18);
static_assert(offsetof(GameGlobals32, movies) == 0x50);
static_assert(offsetof(GameGlobals32, generics) == 0x68);
static_assert(sizeof(GameGlobals32) == 0xe8);
static_assert(sizeof(GameMix32) == 0x28);
static_assert(offsetof(GameMix32, count) == 0x14);

struct GenericSlot {
    const char* name;
    std::size_t index;
};
inline constexpr GenericSlot kBootstrapSlots[] = {
    {"RA2MD.MIX", 0}, {"RA2.MIX", 1}, {"CACHEMD.MIX", 20},
    {"CACHE.MIX", 21}, {"LOCALMD.MIX", 22}, {"LOCAL.MIX", 23}
};

// The constructor callback must register a real native object in MIXes.
// Neither shared_ptr nor a DLL-owned object may enter a game container.
struct BridgeApi {
    void* context;
    GameGlobals32* globals;
    GamePtr (*construct)(void*, const char*, const ra2::fs::MixArchive&);
    bool (*append)(void*, GameVector32&, GamePtr);
    void (*destroy)(void*, GamePtr); // Detach/delete an unpublished expansion.
    void (*pump)(void*);
    void (*log)(void*, const char*);
};

int bootstrap_bridge(const std::filesystem::path& directory, const BridgeApi& api) noexcept;

// Bootstrap is static and takes NO stack arguments. Absorb unspecified ECX
// and EDX in two unused fastcall registers; return 0/1 in EAX, plain RET.
extern "C" int RA2_FASTCALL bootstrap_hook(void* unused_ecx, void* unused_edx) noexcept;

#ifdef _WIN32
void configure_bootstrap_hook(std::uint8_t* image_base) noexcept;
#else
// Linux uses the very same entry and bridge, with explicit simulated memory.
void bind_bootstrap_test_target(const std::filesystem::path* directory,
                                const BridgeApi* api) noexcept;
#endif
} // namespace ra2hook
