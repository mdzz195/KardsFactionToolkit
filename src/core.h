#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <cstddef>
#include <cstdint>

namespace KardsFaction {

struct FString { wchar_t* data; int32_t num; int32_t capacity; };
struct Enumerator { const char* name; int64_t value; };
struct EnumParams {
    void* outer;
    void* display;
    const char* name;
    const char* cppType;
    const Enumerator* entries;
    uint32_t flags;
    int16_t count;
    uint8_t enumFlags;
    uint8_t cppForm;
};
static_assert(sizeof(FString) == 16);
static_assert(sizeof(Enumerator) == 16);
static_assert(sizeof(EnumParams) == 48 && offsetof(EnumParams,count) == 44);
using Logger = void(*)(const char*);
using ParseFn = uint8_t(*)(void*, FString*);
using EncodeFn = FString*(*)(void*, FString*, uint8_t);
bool MatchesImage(const unsigned char* base);
bool Install(unsigned char* base, Logger log);
bool RemoveBeforeEnumConstruction(); // For the isolated test harness / failed startup only.

struct EntryRedirect { unsigned char* address{}; bool armed{}; };
bool ArmEntry(unsigned char* base, void* destination, EntryRedirect& state);
bool RestoreEntry(EntryRedirect& state);
bool WriteProtected(void* address, const void* data, size_t size);
}
