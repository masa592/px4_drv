#include <windows.h>
#include "bon_driver.hpp"

HMODULE g_hModule = nullptr;

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        g_hModule = hModule;
        break;
    case DLL_PROCESS_DETACH:
        g_hModule = nullptr;
        break;
    }
    return TRUE;
}

extern "C" __declspec(dllexport) IBonDriver * CreateBonDriver()
{
    return new (std::nothrow) px_s1ud::BonDriverS1UD();
}
