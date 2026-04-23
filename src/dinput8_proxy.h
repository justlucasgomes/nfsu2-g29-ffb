#pragma once
#define WIN32_LEAN_AND_MEAN
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>

// ─────────────────────────────────────────────────────────────────────────────
//  IDirectInput8A proxy — wraps the real DI8 object returned by the system
//  DirectInput8Create so we can intercept CreateDevice calls and inject our
//  G29 device wrapper.
// ─────────────────────────────────────────────────────────────────────────────

// Exported function — same signature as the real DirectInput8Create.
// Called by NFSU2 when it initialises DirectInput.
extern "C" HRESULT WINAPI
DirectInput8Create(HINSTANCE hinst, DWORD dwVersion, REFIID riidltf,
                   LPVOID* ppvOut, LPUNKNOWN punkOuter);

// The real DirectInput8Create in System32\dinput8.dll.
typedef HRESULT(WINAPI* PFN_DirectInput8Create)(
    HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);

extern PFN_DirectInput8Create  g_RealDI8Create;
extern HMODULE                 g_hRealDI8;

// Loads the real dinput8.dll from System32 and resolves g_RealDI8Create.
bool LoadRealDInput8();
