#include "core.h"
#include "generated/profile.h"
#include "generated/dwm_exports.h"
#include "generated/factions.h"
#include <cstdio>
#include <cwchar>
#include <cstring>

namespace {
HMODULE selfModule{}, systemDwm{};
INIT_ONCE forwardingOnce=INIT_ONCE_STATIC_INIT;
FARPROC forwarding[sizeof(DwmExports)/sizeof(DwmExports[0])]{};
KardsFaction::EntryRedirect entry;
volatile LONG runtimeStatus{};

void Log(const char* message) {
    wchar_t path[32768]{};
    if (!GetModuleFileNameW(selfModule,path,32768)) return;
    wchar_t* name=wcsrchr(path,L'\\');
    if (!name) return;
    wcscpy_s(name+1,32768-static_cast<size_t>(name+1-path),L"KardsFactionToolkit.log");
    SYSTEMTIME time{}; GetLocalTime(&time);
    char line[1024]{};
    const int length=snprintf(line,sizeof line,"%04u-%02u-%02u %02u:%02u:%02u [KardsFactionToolkit] %s\r\n",
        time.wYear,time.wMonth,time.wDay,time.wHour,time.wMinute,time.wSecond,message);
    if (length<=0 || length>=static_cast<int>(sizeof line)) return;
    OutputDebugStringA(line);
    HANDLE file=CreateFileW(path,FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
        nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if (file==INVALID_HANDLE_VALUE) return;
    DWORD written{}; WriteFile(file,line,static_cast<DWORD>(length),&written,nullptr); CloseHandle(file);
}
BOOL CALLBACK LoadSystemDwm(PINIT_ONCE, PVOID, PVOID*) {
    wchar_t path[MAX_PATH]{};
    const UINT count=GetSystemDirectoryW(path,MAX_PATH);
    if (!count || count>=MAX_PATH-12) return FALSE;
    wcscat_s(path,L"\\dwmapi.dll");
    systemDwm=LoadLibraryExW(path,nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!systemDwm || systemDwm==selfModule) return FALSE;
    for (size_t i=0;i<sizeof(DwmExports)/sizeof(DwmExports[0]);++i) {
        const auto& item=DwmExports[i];
        forwarding[i]=GetProcAddress(systemDwm,item.name ? item.name : MAKEINTRESOURCEA(item.ordinal));
    }
    return TRUE;
}
void EntryBootstrap() {
    auto* originalEntry=entry.address;
    if (!KardsFaction::RestoreEntry(entry)) {
        Log("FATAL: failed to restore startup entry; stopping to prevent recursion");
        TerminateProcess(GetCurrentProcess(),0xe0524f01);
        return;
    }
    Log("Starting KardsFactionToolkit 0.2.0; native faction extension");
    Log(Profile::Id);
    Log("Compatibility: version + critical code + enum schema; whole-file SHA256 not required");
    // The enum's replacement name array and detours must remain alive for
    // the entire process, including after reflection finishes registering.
    HMODULE pinned{};
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(&EntryBootstrap),&pinned)) {
        runtimeStatus=-3; Log("ERROR: could not pin module; skipped installation");
    } else {
        runtimeStatus=KardsFaction::Install(reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr)),Log) ? 2 : -4;
    }
    // The game's CRT/static constructors and UE registration run only now.
    reinterpret_cast<void(*)()>(originalEntry)();
}
}

extern "C" FARPROC ResolveDwmExport(unsigned int index) {
    if (index>=sizeof(DwmExports)/sizeof(DwmExports[0]) ||
        !InitOnceExecuteOnce(&forwardingOnce,LoadSystemDwm,nullptr,nullptr) || !forwarding[index]) {
        Log("FATAL: could not resolve original system dwmapi export");
        RaiseFailFastException(nullptr,nullptr,0);
        return nullptr;
    }
    return forwarding[index];
}
extern "C" __declspec(dllexport) LONG KardsFactionStatus() {
    return InterlockedCompareExchange(&runtimeStatus,0,0);
}
extern "C" __declspec(dllexport) const char* KardsFactionVersion() {
    return "KardsFactionToolkit 0.2.0";
}
// Retain the original status API for existing diagnostic tools.
extern "C" __declspec(dllexport) LONG RomaniaNativeStatus() { return KardsFactionStatus(); }
extern "C" __declspec(dllexport) const char* RomaniaNativeVersion() { return KardsFactionVersion(); }
BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) {
    if (reason==DLL_PROCESS_ATTACH) {
        selfModule=module;
        // Only a bounded entry-byte replacement happens under the loader lock.
        // Logs, MinHook and engine-related writes run at the EXE entry.
        if (KardsFaction::ArmEntry(reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr)),
            reinterpret_cast<void*>(&EntryBootstrap),entry)) runtimeStatus=1;
    }
    return TRUE;
}
