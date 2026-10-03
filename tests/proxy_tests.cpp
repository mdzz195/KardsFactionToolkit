#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <dwmapi.h>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include "generated/dwm_exports.h"
int wmain() {
    HMODULE module=GetModuleHandleW(L"dwmapi.dll");
    if (!module) return 1;
    using Status=LONG(*)(); using Version=const char*(*)();
    auto status=reinterpret_cast<Status>(GetProcAddress(module,"KardsFactionStatus"));
    auto version=reinterpret_cast<Version>(GetProcAddress(module,"KardsFactionVersion"));
    if (!status || !version || status()!=0) return 2;
    for (const auto& item:DwmExports)
        if (!GetProcAddress(module,item.name ? item.name : MAKEINTRESOURCEA(item.ordinal))) return 3;
    BOOL enabled{};
    const HRESULT composition=DwmIsCompositionEnabled(&enabled);
    const HRESULT flushed=DwmFlush();
    DWM_TIMING_INFO timing{}; timing.cbSize=sizeof timing;
    const HRESULT timed=DwmGetCompositionTimingInfo(nullptr,&timing);
    const DWORD policy=DWMWA_NCRENDERING_POLICY;
    const DWMNCRENDERINGPOLICY rendering=DWMNCRP_ENABLED;
    const HRESULT attributed=DwmSetWindowAttribute(nullptr,policy,&rendering,sizeof rendering);
    wchar_t path[32768]{}; GetModuleFileNameW(module,path,32768);
    std::wprintf(L"Loaded app-local proxy: %ls\n",path);
    std::printf("%s\n",version());
    std::printf("PASS: %zu forwarded exports resolve; 4 game-imported DWM APIs called; status=%ld (non-game host unchanged)\n",sizeof(DwmExports)/sizeof(DwmExports[0]),status());
    std::printf("API results: composition=%08lx enabled=%d flush=%08lx timing=%08lx attribute=%08lx\n",composition,enabled,flushed,timed,attributed);
    return 0;
}
