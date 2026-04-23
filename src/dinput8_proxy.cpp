#include "dinput8_proxy.h"
#include "input_bridge.h"
#include "logger.h"
#include "config.h"
#include <string>

PFN_DirectInput8Create g_RealDI8Create = nullptr;
HMODULE                g_hRealDI8      = nullptr;

// ─────────────────────────────────────────────────────────────────────────────

bool LoadRealDInput8() {
    char sysDir[MAX_PATH];
    GetSystemDirectoryA(sysDir, sizeof(sysDir));
    std::string path = std::string(sysDir) + "\\dinput8.dll";

    g_hRealDI8 = LoadLibraryA(path.c_str());
    if (!g_hRealDI8) {
        LOG_ERROR("Proxy: failed to load real dinput8 from '%s'", path.c_str());
        return false;
    }

    g_RealDI8Create = reinterpret_cast<PFN_DirectInput8Create>(
        GetProcAddress(g_hRealDI8, "DirectInput8Create"));

    if (!g_RealDI8Create) {
        LOG_ERROR("Proxy: DirectInput8Create not found in system dinput8.dll");
        return false;
    }

    LOG_INFO("Proxy: real dinput8.dll loaded from '%s'", path.c_str());
    return true;
}

// ── Proxy IDirectInput8A implementation ──────────────────────────────────────
// We wrap the real IDirectInput8A to intercept CreateDevice so we can
// initialise our G29 wrapper alongside whatever device the game asks for.

class ProxyDI8 : public IDirectInput8A {
public:
    explicit ProxyDI8(IDirectInput8A* real) : m_real(real) {}

    // ── IUnknown ─────────────────────────────────────────────────────────────
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, LPVOID* ppvObj) override {
        return m_real->QueryInterface(riid, ppvObj);
    }
    ULONG STDMETHODCALLTYPE AddRef() override  { return m_real->AddRef(); }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG ref = m_real->Release();
        if (ref == 0) delete this;
        return ref;
    }

    // ── IDirectInput8A ────────────────────────────────────────────────────────

    HRESULT STDMETHODCALLTYPE CreateDevice(
        REFGUID rguid, LPDIRECTINPUTDEVICE8A* lplpDirectInputDevice,
        LPUNKNOWN pUnkOuter) override
    {
        HRESULT hr = m_real->CreateDevice(rguid, lplpDirectInputDevice, pUnkOuter);

        // On first CreateDevice call (the game is setting up its first controller),
        // try to initialise our G29 bridge if not already done.
        if (SUCCEEDED(hr) && !InputBridge::Get().IsReady()) {
            LOG_INFO("Proxy: CreateDevice called — attempting G29 init...");
            InputBridge::Get().Init(m_real);
        }

        return hr;
    }

    HRESULT STDMETHODCALLTYPE EnumDevices(
        DWORD dwDevType, LPDIENUMDEVICESCALLBACKA lpCallback,
        LPVOID pvRef, DWORD dwFlags) override
    {
        return m_real->EnumDevices(dwDevType, lpCallback, pvRef, dwFlags);
    }

    HRESULT STDMETHODCALLTYPE GetDeviceStatus(REFGUID rguidInstance) override {
        return m_real->GetDeviceStatus(rguidInstance);
    }

    HRESULT STDMETHODCALLTYPE RunControlPanel(HWND hwndOwner, DWORD dwFlags) override {
        return m_real->RunControlPanel(hwndOwner, dwFlags);
    }

    HRESULT STDMETHODCALLTYPE Initialize(HINSTANCE hinst, DWORD dwVersion) override {
        return m_real->Initialize(hinst, dwVersion);
    }

    HRESULT STDMETHODCALLTYPE FindDevice(REFGUID rguidClass, LPCSTR ptszName,
                                          LPGUID pguidInstance) override {
        return m_real->FindDevice(rguidClass, ptszName, pguidInstance);
    }

    HRESULT STDMETHODCALLTYPE EnumDevicesBySemantics(
        LPCSTR ptszUserName, LPDIACTIONFORMATA lpdiActionFormat,
        LPDIENUMDEVICESBYSEMANTICSCBA lpCallback, LPVOID pvRef, DWORD dwFlags) override
    {
        return m_real->EnumDevicesBySemantics(
            ptszUserName, lpdiActionFormat, lpCallback, pvRef, dwFlags);
    }

    HRESULT STDMETHODCALLTYPE ConfigureDevices(
        LPDICONFIGUREDEVICESCALLBACK lpdiCallback,
        LPDICONFIGUREDEVICESPARAMSA lpdiCDParams, DWORD dwFlags, LPVOID pvRefData) override
    {
        return m_real->ConfigureDevices(lpdiCallback, lpdiCDParams, dwFlags, pvRefData);
    }

private:
    IDirectInput8A* m_real;
};

// ── Exported DirectInput8Create ───────────────────────────────────────────────

extern "C" HRESULT WINAPI
DirectInput8Create(HINSTANCE hinst, DWORD dwVersion, REFIID riidltf,
                   LPVOID* ppvOut, LPUNKNOWN punkOuter)
{
    if (!g_RealDI8Create) {
        LOG_ERROR("Proxy: DirectInput8Create called before real DI8 was loaded!");
        return DIERR_GENERIC;
    }

    // Create the real DI8 object
    IDirectInput8A* pReal = nullptr;
    HRESULT hr = g_RealDI8Create(hinst, dwVersion, riidltf,
                                  reinterpret_cast<LPVOID*>(&pReal), punkOuter);
    if (FAILED(hr) || !pReal) {
        LOG_ERROR("Proxy: real DirectInput8Create failed 0x%08X", hr);
        if (ppvOut) *ppvOut = nullptr;
        return hr;
    }

    LOG_INFO("Proxy: real DirectInput8Create OK (ver=0x%04X)", dwVersion);

    // Wrap it in our proxy
    auto* proxy = new ProxyDI8(pReal);
    if (ppvOut) *ppvOut = proxy;

    return DI_OK;
}
