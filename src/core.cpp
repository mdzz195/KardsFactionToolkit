#include "core.h"
#include "generated/profile.h"
#include "generated/factions.h"
#include "../vendor/minhook/include/MinHook.h"
#include <cstring>
#include <cwchar>
#include <cstdio>

namespace KardsFaction {
namespace {
constexpr const char* OriginalNames[] = {
    "EFactionEnum::NotAvailable", "EFactionEnum::Germany", "EFactionEnum::Britain",
    "EFactionEnum::Japan", "EFactionEnum::Soviet", "EFactionEnum::USA", "EFactionEnum::France",
    "EFactionEnum::Italy", "EFactionEnum::Poland", "EFactionEnum::Finland", "EFactionEnum::Anzac",
    "EFactionEnum::Allies", "EFactionEnum::Neutral"
};
static_assert(sizeof(OriginalNames)/sizeof(OriginalNames[0]) == Config::OriginalCount);
Enumerator expanded[Config::OriginalCount + Config::FactionCount]{};
EnumParams* parameters{};
const Enumerator* originalEntries{};
unsigned char* installedBase{};
ParseFn originalName{}, originalDecode{};
EncodeFn originalEncode{};
void* hooks[3]{};
bool ownsMinHook{};
Logger logger{};

bool IsText(const FString* text, const wchar_t* expected, int32_t length) {
    if (!text || !text->data || text->num != length + 1 || text->capacity < text->num) return false;
    for (int32_t i=0; i<length; ++i) {
        wchar_t c=text->data[i];
        if (c>=L'A' && c<=L'Z') c+=L'a'-L'A';
        if (c!=expected[i]) return false;
    }
    return text->data[length]==0;
}
uint8_t NameHook(void* context, FString* text) {
    const Config::FactionSpec* matched=nullptr;
    for (const auto& faction:Config::Factions)
        if (IsText(text,faction.lowerName,static_cast<int32_t>(wcslen(faction.lowerName)))) {
            matched=&faction; break;
        }
    // Both original parsers own and release this by-value FString. Always let
    // the real implementation consume it, including for the new branch.
    const uint8_t result=originalName(context,text);
    return matched ? matched->value : result;
}
uint8_t DecodeHook(void* context, FString* text) {
    const Config::FactionSpec* matched=nullptr;
    for (const auto& faction:Config::Factions) {
        const wchar_t code[]={faction.code,0};
        if (IsText(text,code,1)) { matched=&faction; break; }
    }
    const uint8_t result=originalDecode(context,text);
    return matched ? matched->value : result;
}
FString* EncodeHook(void* context, FString* output, uint8_t faction) {
    // Reuse the original allocator and its one-character output construction.
    // The verified build encodes ANZAC as 'a'; change only that owned character.
    const Config::FactionSpec* matched=nullptr;
    for (const auto& spec:Config::Factions) if (faction==spec.value) { matched=&spec; break; }
    FString* result=originalEncode(context,output,matched ? uint8_t{10} : faction);
    if (matched && result && result->data && result->num==2 && result->data[0]==L'a')
        result->data[0]=matched->code;
    return result;
}
bool NoEnumYet(const unsigned char* base) {
    return *reinterpret_cast<void* const*>(base+Profile::SingletonRva)==nullptr &&
           *reinterpret_cast<void* const*>(base+Profile::StaticSingletonRva)==nullptr;
}
void Say(const char* message) { if (logger) logger(message); }
void RemoveHooks() {
    for (void*& hook:hooks) if (hook) {
        MH_DisableHook(hook);
        MH_RemoveHook(hook);
        hook=nullptr;
    }
    if (ownsMinHook) { MH_Uninitialize(); ownsMinHook=false; }
}
}

bool WriteProtected(void* address, const void* data, size_t size) {
    DWORD old{};
    if (!VirtualProtect(address,size,PAGE_EXECUTE_READWRITE,&old)) return false;
    std::memcpy(address,data,size);
    FlushInstructionCache(GetCurrentProcess(),address,size);
    DWORD ignored{};
    return VirtualProtect(address,size,old,&ignored)!=FALSE;
}
bool MatchesImage(const unsigned char* base) {
    if (!base) return false;
    const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic!=IMAGE_DOS_SIGNATURE || dos->e_lfanew<=0 || dos->e_lfanew>0x100000) return false;
    const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(base+dos->e_lfanew);
    if (nt->Signature!=IMAGE_NT_SIGNATURE || nt->FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64 ||
        nt->FileHeader.TimeDateStamp!=Profile::Timestamp || nt->OptionalHeader.SizeOfImage!=Profile::ImageSize ||
        nt->OptionalHeader.AddressOfEntryPoint!=Profile::EntryRva) return false;
    return !memcmp(base+Profile::NameToEnumRva,Profile::NameToEnumBytes,sizeof Profile::NameToEnumBytes) &&
           !memcmp(base+Profile::EncodeRva,Profile::EncodeBytes,sizeof Profile::EncodeBytes) &&
           !memcmp(base+Profile::DecodeRva,Profile::DecodeBytes,sizeof Profile::DecodeBytes) &&
           !memcmp(base+Profile::EnumConstructorRva,Profile::EnumConstructorBytes,sizeof Profile::EnumConstructorBytes);
}
bool ArmEntry(unsigned char* base, void* destination, EntryRedirect& state) {
    if (state.armed || !MatchesImage(base) ||
        memcmp(base+Profile::EntryRva,Profile::EntryBytes,sizeof Profile::EntryBytes)) return false;
    unsigned char patch[sizeof Profile::EntryBytes];
    memset(patch,0x90,sizeof patch);
    patch[0]=0xff; patch[1]=0x25; memset(patch+2,0,4);
    memcpy(patch+6,&destination,sizeof destination);
    state.address=base+Profile::EntryRva;
    if (!WriteProtected(state.address,patch,sizeof patch)) return false;
    state.armed=true;
    return true;
}
bool RestoreEntry(EntryRedirect& state) {
    if (!state.armed) return false;
    if (!WriteProtected(state.address,Profile::EntryBytes,sizeof Profile::EntryBytes)) return false;
    state.armed=false;
    return true;
}
bool Install(unsigned char* base, Logger log) {
    logger=log;
    if (installedBase) return installedBase==base;
    if (!MatchesImage(base)) { Say("ERROR: executable header/code fingerprint mismatch; no core installed"); return false; }
    if (!NoEnumYet(base)) { Say("ERROR: EFactionEnum already constructed; refusing a late install"); return false; }
    auto* params=reinterpret_cast<EnumParams*>(base+Profile::ParamsRva);
    if (params->entries!=reinterpret_cast<Enumerator*>(base+Profile::EnumeratorsRva) || params->count!=13 ||
        params->outer!=base+Profile::OuterRva || params->display || params->flags!=0x45 ||
        params->enumFlags!=0 || params->cppForm!=2 ||
        params->name!=reinterpret_cast<const char*>(base+Profile::EnumNameRva) || params->cppType!=params->name ||
        strcmp(params->name,"EFactionEnum")) {
        Say("ERROR: original FEnumParams differs; no core installed"); return false;
    }
    for (int i=0;i<13;++i) {
        const auto& item=params->entries[i];
        const uintptr_t p=reinterpret_cast<uintptr_t>(item.name), lo=reinterpret_cast<uintptr_t>(base);
        if (p<lo || p+64>=lo+Profile::ImageSize || item.value!=i || strcmp(item.name,OriginalNames[i])) {
            Say("ERROR: original enum entry differs; no core installed"); return false;
        }
        expanded[i]=item;
    }
    for (size_t i=0;i<Config::FactionCount;++i)
        expanded[Config::OriginalCount+i]={Config::Factions[i].enumName,Config::Factions[i].value};
    const auto init=MH_Initialize();
    if (init!=MH_OK && init!=MH_ERROR_ALREADY_INITIALIZED) { Say("ERROR: MH_Initialize failed"); return false; }
    ownsMinHook=init==MH_OK;
    void* targets[]={base+Profile::NameToEnumRva,base+Profile::EncodeRva,base+Profile::DecodeRva};
    void* detours[]={reinterpret_cast<void*>(NameHook),reinterpret_cast<void*>(EncodeHook),reinterpret_cast<void*>(DecodeHook)};
    void** originals[]={reinterpret_cast<void**>(&originalName),reinterpret_cast<void**>(&originalEncode),reinterpret_cast<void**>(&originalDecode)};
    for (int i=0;i<3;++i) {
        if (MH_CreateHook(targets[i],detours[i],originals[i])!=MH_OK) {
            Say("ERROR: detour creation failed; rolling back"); RemoveHooks(); return false;
        }
        hooks[i]=targets[i];
    }
    EnumParams replacement=*params;
    replacement.entries=expanded;
    replacement.count=static_cast<int16_t>(Config::OriginalCount+Config::FactionCount);
    const EnumParams before=*params;
    if (!WriteProtected(params,&replacement,sizeof replacement)) {
        WriteProtected(params,&before,sizeof before); RemoveHooks(); Say("ERROR: enum registration write failed"); return false;
    }
    for (void* hook:hooks) {
        if (MH_EnableHook(hook)!=MH_OK) {
            RemoveHooks(); WriteProtected(params,&before,sizeof before);
            Say("ERROR: detour activation failed; rolled back"); return false;
        }
    }
    parameters=params; originalEntries=before.entries; installedBase=base;
    for (const auto& faction:Config::Factions) {
        char message[256]{};
        snprintf(message,sizeof message,"READY: %s=%u, deck code=%c; original faction values 0..12 preserved",
            faction.enumName,static_cast<unsigned>(faction.value),static_cast<char>(faction.code));
        Say(message);
    }
    Say("READY: early enum registration and all three native deck conversions installed");
    return true;
}
bool RemoveBeforeEnumConstruction() {
    if (!installedBase || !NoEnumYet(installedBase)) return false;
    RemoveHooks();
    EnumParams restored=*parameters; restored.entries=originalEntries; restored.count=13;
    if (!WriteProtected(parameters,&restored,sizeof restored)) return false;
    installedBase=nullptr; parameters=nullptr; originalEntries=nullptr;
    return true;
}
}
