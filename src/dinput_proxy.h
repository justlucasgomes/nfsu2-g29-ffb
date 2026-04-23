#pragma once
#define WIN32_LEAN_AND_MEAN
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>

// Exported function — intercepts the game's call to DirectInput8Create
extern "C" HRESULT WINAPI
DirectInput8Create(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);

typedef HRESULT(WINAPI* PFN_DI8Create)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);

namespace DInputProxy {
    bool   LoadRealDInput8();
    void   UnloadRealDInput8();

    extern PFN_DI8Create g_Real;
    extern HMODULE       g_hDLL;
}
