#include "core.h"
#include "generated/profile.h"
#include "generated/factions.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <vector>
#include <unordered_set>
#include <string>

namespace {
int checks{}, freed{};
std::unordered_set<void*> allocations;
void Check(bool ok, const char* message) {
    ++checks;
    if (!ok) { std::fprintf(stderr,"FAIL: %s\n",message); ExitProcess(1); }
}
void Log(const char* text) { std::printf("%s\n",text); }
unsigned char* MapImage(const wchar_t* filename) {
    FILE* file{}; _wfopen_s(&file,filename,L"rb"); Check(file!=nullptr,"open game image");
    _fseeki64(file,0,SEEK_END); const auto size=_ftelli64(file); rewind(file);
    std::vector<unsigned char> raw(static_cast<size_t>(size));
    Check(fread(raw.data(),1,raw.size(),file)==raw.size(),"read game image"); fclose(file);
    const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(raw.data());
    const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(raw.data()+dos->e_lfanew);
    auto* base=static_cast<unsigned char*>(VirtualAlloc(nullptr,nt->OptionalHeader.SizeOfImage,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));
    Check(base!=nullptr,"allocate isolated image");
    memcpy(base,raw.data(),nt->OptionalHeader.SizeOfHeaders);
    const auto* section=IMAGE_FIRST_SECTION(nt);
    for (unsigned i=0;i<nt->FileHeader.NumberOfSections;++i)
        memcpy(base+section[i].VirtualAddress,raw.data()+section[i].PointerToRawData,section[i].SizeOfRawData);
    const auto delta=reinterpret_cast<uintptr_t>(base)-nt->OptionalHeader.ImageBase;
    const auto& directory=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
    size_t offset=directory.VirtualAddress;
    while (offset<directory.VirtualAddress+directory.Size) {
        const auto* block=reinterpret_cast<const IMAGE_BASE_RELOCATION*>(base+offset);
        Check(block->SizeOfBlock>=sizeof(*block),"valid relocation block");
        const auto* values=reinterpret_cast<const WORD*>(block+1);
        for (size_t i=0;i<(block->SizeOfBlock-sizeof(*block))/2;++i) {
            const WORD type=values[i]>>12;
            if (type==IMAGE_REL_BASED_DIR64)
                *reinterpret_cast<uintptr_t*>(base+block->VirtualAddress+(values[i]&0xfff))+=delta;
            else Check(type==IMAGE_REL_BASED_ABSOLUTE,"supported relocation type");
        }
        offset+=block->SizeOfBlock;
    }
    return base;
}
KardsFaction::FString Text(const wchar_t* value) {
    const auto n=wcslen(value)+1;
    auto* memory=static_cast<wchar_t*>(malloc(n*sizeof(wchar_t)));
    memcpy(memory,value,n*sizeof(wchar_t)); allocations.insert(memory);
    return {memory,static_cast<int32_t>(n),static_cast<int32_t>(n)};
}
void MockFree(void* pointer) {
    Check(allocations.erase(pointer)==1,"native parser releases exactly its owned FString");
    free(pointer); ++freed;
}
KardsFaction::FString* MockConstruct(KardsFaction::FString* target, const char* ascii) {
    const auto n=strlen(ascii)+1;
    auto* memory=static_cast<wchar_t*>(malloc(n*sizeof(wchar_t)));
    for (size_t i=0;i<n;++i) memory[i]=static_cast<unsigned char>(ascii[i]);
    allocations.insert(memory);
    *target={memory,static_cast<int32_t>(n),static_cast<int32_t>(n)};
    return target;
}
void Redirect(void* address, void* destination) {
    unsigned char bytes[14]={0xff,0x25,0,0,0,0};
    memcpy(bytes+6,&destination,8);
    Check(KardsFaction::WriteProtected(address,bytes,sizeof bytes),"install isolated allocator stub");
}
KardsFaction::EntryRedirect redirect;
bool entered{};
void FakeEntry() { entered=true; Check(KardsFaction::RestoreEntry(redirect),"restore entry from bootstrap"); }
uint8_t Parse(KardsFaction::ParseFn function, const wchar_t* input) {
    auto value=Text(input); const int before=freed;
    const auto result=function(nullptr,&value);
    Check(freed==before+1,"parser frees input once"); return result;
}
std::wstring Encode(KardsFaction::EncodeFn function, uint8_t faction) {
    KardsFaction::FString output{};
    Check(function(nullptr,&output,faction)==&output,"encoder returns output address");
    Check(output.data && output.num==2 && output.capacity>=2 && output.data[1]==0,"encoder FString layout");
    std::wstring result(output.data); MockFree(output.data); return result;
}
}
int wmain(int argc,wchar_t** argv) {
    if (argc!=2) { std::fprintf(stderr,"usage: native_tests.exe <shipping.exe>\n"); return 2; }
    auto* base=MapImage(argv[1]);
    Check(KardsFaction::MatchesImage(base),"current EXE header and all function signatures");
    base[0x28]^=1; // Unused DOS-header field, unrelated to engine version/code.
    Check(KardsFaction::MatchesImage(base),"allow unrelated content differences within same version");
    base[0x28]^=1;
    base[Profile::NameToEnumRva+sizeof(Profile::NameToEnumBytes)-1]^=1;
    Check(!KardsFaction::MatchesImage(base),"reject changed critical function tail, not just prologue");
    base[Profile::NameToEnumRva+sizeof(Profile::NameToEnumBytes)-1]^=1;
    Check(KardsFaction::ArmEntry(base,reinterpret_cast<void*>(FakeEntry),redirect),"arm early startup redirect");
    reinterpret_cast<void(*)()>(base+Profile::EntryRva)(); // Only FakeEntry runs; never the game's entrypoint.
    Check(entered && !redirect.armed,"startup redirect executes bootstrap once");
    Check(!memcmp(base+Profile::EntryRva,Profile::EntryBytes,sizeof Profile::EntryBytes),"entry bytes restored exactly");
    auto* singleton=reinterpret_cast<void**>(base+Profile::SingletonRva);
    *singleton=reinterpret_cast<void*>(1);
    Check(!KardsFaction::Install(base,Log),"reject late enum extension"); *singleton=nullptr;
    base[Profile::NameToEnumRva]^=1;
    Check(!KardsFaction::Install(base,Log),"reject modified function signature"); base[Profile::NameToEnumRva]^=1;
    Redirect(base+Profile::TestFreeRva,reinterpret_cast<void*>(MockFree));
    Redirect(base+Profile::TestConstructRva,reinterpret_cast<void*>(MockConstruct));
    auto parse=reinterpret_cast<KardsFaction::ParseFn>(base+Profile::NameToEnumRva);
    auto encode=reinterpret_cast<KardsFaction::EncodeFn>(base+Profile::EncodeRva);
    auto decode=reinterpret_cast<KardsFaction::ParseFn>(base+Profile::DecodeRva);
    const wchar_t* names[]={L"NotAvailable",L"Germany",L"Britain",L"Japan",L"Soviet",L"USA",L"France",L"Italy",L"Poland",L"Finland",L"Anzac",L"Allies",L"Neutral",L"invalid name with spaces"};
    uint8_t beforeNames[14]{}; std::wstring beforeCodes[14];
    for (int i=0;i<14;++i) { beforeNames[i]=Parse(parse,names[i]); beforeCodes[i]=Encode(encode,static_cast<uint8_t>(i)); }
    for (const auto& faction:Config::Factions) {
        const wchar_t code[]={faction.code,0};
        Check(Parse(parse,faction.name)==0 && Parse(decode,code)==0,"new faction has no original mapping");
    }
    Check(KardsFaction::Install(base,Log),"install configured core against actual game functions");
    Check(KardsFaction::Install(base,Log),"installation is idempotent");
    const auto* params=reinterpret_cast<const KardsFaction::EnumParams*>(base+Profile::ParamsRva);
    Check(params->count==Config::OriginalCount+Config::FactionCount,"original entries plus configured factions");
    for (int i=0;i<13;++i) Check(params->entries[i].value==i,"original enum values preserved");
    for (int i=0;i<14;++i) Check(Parse(parse,names[i])==beforeNames[i],"all original name mappings unchanged");
    for (int i=0;i<13;++i) Check(Encode(encode,static_cast<uint8_t>(i))==beforeCodes[i],"all original codes unchanged");
    for (int i=1;i<=10;++i) Check(Parse(decode,beforeCodes[i].c_str())==i,"all old national deck codes roundtrip");
    for (size_t index=0;index<Config::FactionCount;++index) {
        const auto& faction=Config::Factions[index];
        const wchar_t code[]={faction.code,0};
        const wchar_t upperCode[]={static_cast<wchar_t>(faction.code-L'a'+L'A'),0};
        const auto& item=params->entries[Config::OriginalCount+index];
        Check(item.value==faction.value && !strcmp(item.name,faction.enumName),"new enum name and value");
        std::wstring upperName(faction.name);
        for (auto& c:upperName) if (c>=L'a' && c<=L'z') c-=L'a'-L'A';
        Check(Parse(parse,faction.name)==faction.value && Parse(parse,upperName.c_str())==faction.value,"case insensitive name parsing");
        Check(Parse(parse,(std::wstring(faction.name)+L" ").c_str())==0,"name boundary is exact");
        Check(Encode(encode,faction.value)==code && Parse(decode,code)==faction.value &&
            Parse(decode,upperCode)==faction.value,"configured deck code roundtrip");
        for (int i=0;i<4096;++i) {
            Check(Parse(parse,faction.name)==faction.value,"repeated name parsing");
            Check(Parse(decode,Encode(encode,faction.value).c_str())==faction.value,"repeated deck roundtrip");
        }
    }
    Check(Parse(decode,L"bb")==0 && Parse(decode,L"")==0,"invalid codes retain original result");
    KardsFaction::FString empty{}; Check(parse(nullptr,&empty)==0,"zero FString follows original behavior");
    Check(allocations.empty(),"no leaked or double-freed FString allocations");
    *singleton=reinterpret_cast<void*>(1);
    Check(!KardsFaction::RemoveBeforeEnumConstruction(),"refuse unloading enum storage after registration"); *singleton=nullptr;
    Check(KardsFaction::RemoveBeforeEnumConstruction(),"rollback before registration");
    Check(KardsFaction::MatchesImage(base),"all native function bytes restored");
    Check(params->count==13 && params->entries==reinterpret_cast<KardsFaction::Enumerator*>(base+Profile::EnumeratorsRva),"original enum registration restored");
    for (const auto& faction:Config::Factions) {
        const wchar_t code[]={faction.code,0};
        Check(Parse(parse,faction.name)==0 && Encode(encode,faction.value)==L"0" && Parse(decode,code)==0,"rollback restores original behavior");
    }
    Check(allocations.empty(),"final FString allocation balance");
    VirtualFree(base,0,MEM_RELEASE);
    std::printf("PASS: %d checks; original native conversion code executed in isolated image; game entrypoint not executed\n",checks);
    return 0;
}
