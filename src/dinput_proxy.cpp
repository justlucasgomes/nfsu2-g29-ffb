// dinput_proxy.cpp
// Proxy IDirectInput8A + G29 FFB init em thread separado.
//
// Fluxo:
//   DirectInput8Create  → retorna ProxyDI8 (wrapper do objeto real)
//   ProxyDI8::CreateDevice (1ª chamada) → dispara G29InitThread
//   G29InitThread       → cria IDirectInput8A próprio, init WheelInput + FFB
//
// O ProxyDevice é um passthrough puro — não modifica eixos nem botões.
// O FFB é calculado e enviado pelo WheelInput/ForceFeedback em thread própria.

#define WIN32_LEAN_AND_MEAN
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>
#include "proxy_log.h"
#include "wheel_input.h"
#include "force_feedback.h"
#include "config.h"
#include "logger.h"
#include "engine_curve.h"

typedef HRESULT (WINAPI* PFN_DI8Create)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);

static HMODULE       g_hReal = nullptr;
static PFN_DI8Create g_pReal = nullptr;

// ── lazy-load do System32\dinput8.dll ────────────────────────────────────────

static bool EnsureReal() {
    if (g_pReal) return true;
    char sys[MAX_PATH] = {};
    GetSystemDirectoryA(sys, MAX_PATH);
    char path[MAX_PATH] = {};
    wsprintfA(path, "%s\\dinput8.dll", sys);
    ProxyLogFmt("[g29_ffb] dinput: carregando %s", path);
    g_hReal = LoadLibraryA(path);
    if (!g_hReal) {
        ProxyLogFmt("[g29_ffb] dinput: ERRO LoadLibrary (err=%u)", GetLastError());
        return false;
    }
    g_pReal = reinterpret_cast<PFN_DI8Create>(
                  GetProcAddress(g_hReal, "DirectInput8Create"));
    if (!g_pReal) {
        ProxyLog("[g29_ffb] dinput: ERRO GetProcAddress");
        FreeLibrary(g_hReal); g_hReal = nullptr; return false;
    }
    ProxyLog("[g29_ffb] dinput: System32\\dinput8.dll OK");
    return true;
}

// ── G29 init thread ───────────────────────────────────────────────────────────

static DWORD WINAPI G29InitThread(LPVOID) {
    for (int i = 0; !g_pReal && i < 200; ++i) Sleep(10);
    if (!g_pReal) {
        ProxyLog("[g29_ffb] G29: timeout aguardando dinput real — abortando");
        return 1;
    }

    char dir[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, dir, MAX_PATH);
    char* s = strrchr(dir, '\\'); if (s) s[1] = '\0';
    char cfgPath[MAX_PATH];
    wsprintfA(cfgPath, "%sconfig.ini", dir);
    g_Config.Load(std::string(cfgPath));
    ProxyLog("[g29_ffb] G29: config carregada");

    // Load per-car torque curve from cars.ini.
    // CarName is read directly — avoids adding a string field to FFBConfig.
    {
        char carName[64] = "default";
        GetPrivateProfileStringA("Engine", "CarName", "default",
                                 carName, sizeof(carName), cfgPath);
        char carsPath[MAX_PATH];
        wsprintfA(carsPath, "%scars.ini", dir);
        LoadEngineCurve(std::string(carsPath), std::string(carName));
    }

    char logPath[MAX_PATH];
    CreateDirectoryA((std::string(dir) + "logs").c_str(), nullptr);
    wsprintfA(logPath, "%slogs\\ffb.log", dir);
    Logger::Get().Init(std::string(logPath),
                       static_cast<LogLevel>(g_Config.general.logLevel));

    IDirectInput8A* pDI = nullptr;
    HRESULT hr = g_pReal(GetModuleHandleA(nullptr), DIRECTINPUT_VERSION,
                         IID_IDirectInput8A,
                         reinterpret_cast<LPVOID*>(&pDI), nullptr);
    if (FAILED(hr) || !pDI) {
        ProxyLogFmt("[g29_ffb] G29: falhou criar IDirectInput8A (hr=0x%08X)", hr);
        return 1;
    }

    bool ok = WheelInput::Get().Init(pDI);
    pDI->Release();

    if (ok) ProxyLog("[g29_ffb] G29: volante detectado — input + FFB ativos");
    else    ProxyLog("[g29_ffb] G29: volante nao encontrado (sem FFB)");

    return 0;
}

// ── G29 detection ─────────────────────────────────────────────────────────────
static bool IsG29Device(IDirectInputDevice8A* pDev) {
    DIDEVICEINSTANCEA di{ sizeof(di) };
    if (FAILED(pDev->GetDeviceInfo(&di))) return false;
    return (di.guidProduct.Data1 & 0xFFFF)        == 0x046D
        && ((di.guidProduct.Data1 >> 16) & 0xFFFF) == 0xC24F;
}

// ── ProxyDevice ────────────────────────────────────────────────────────────────
// Passthrough puro para todos os dispositivos.
//
// Exceção: G29 em menus (IsInRace() == false).
// O G29 envia lY/lRz/slider[0] = 65535 em repouso (hardware invertido).
// O NFSU2 na tela de login interpreta esses valores como navegação vertical,
// causando o "phantom scroll". Durante corrida, o perfil do NFSU2 calibra
// corretamente esses eixos — o problema é apenas antes do login.
//
// Solução: no GetDeviceData (caminho buffered usado pelos menus), removemos os
// eventos de eixo de pedal enquanto IsInRace() == false. Assim que o carro
// começa a se mover (velocidade > 0.5 m/s), IsInRace() vira true e os eventos
// passam normalmente para o jogo.
//
// GetDeviceState (caminho polling usado na gameplay) não é modificado.

class ProxyDevice : public IDirectInputDevice8A {
public:
    explicit ProxyDevice(IDirectInputDevice8A* real, bool isG29)
        : m_real(real), m_isG29(isG29) {
        ProxyLogFmt("[g29_ffb] ProxyDevice #%d created%s",
                    ++s_count, isG29 ? " [G29 — menu filter active]" : "");
    }
    static int s_count;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID r, LPVOID* p) override
        { return m_real->QueryInterface(r, p); }
    ULONG STDMETHODCALLTYPE AddRef()  override { return m_real->AddRef(); }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG ref = m_real->Release();
        if (!ref) delete this;
        return ref;
    }

    HRESULT STDMETHODCALLTYPE GetDeviceState(DWORD cb, LPVOID pv) override
        { return m_real->GetDeviceState(cb, pv); }

    // GetDeviceData — caminho buffered (menus do NFSU2).
    // Quando não estamos em corrida, remove eventos de eixo de pedal do G29
    // para que o lY=65535 em repouso não cause navegação vertical nos menus.
    // Offsets DIJOYSTATE: lY=4  lRz=20  rglSlider[0]=24  rglSlider[1]=28
    HRESULT STDMETHODCALLTYPE GetDeviceData(DWORD cb, LPDIDEVICEOBJECTDATA rg,
                                             LPDWORD pdw, DWORD fl) override {
        HRESULT hr = m_real->GetDeviceData(cb, rg, pdw, fl);
        if (SUCCEEDED(hr) && rg && pdw && *pdw > 0
                && m_isG29 && !WheelInput::Get().IsInRace()) {
            DWORD out = 0;
            for (DWORD i = 0; i < *pdw; ++i) {
                DWORD ofs = rg[i].dwOfs;
                // Mantém steering (0), botões (>=48), POVs (32-47).
                // Remove eixos de pedal que causam phantom scroll.
                bool isPedal = (ofs == 4 || ofs == 20 || ofs == 24 || ofs == 28);
                if (!isPedal) rg[out++] = rg[i];
            }
            *pdw = out;
        }
        return hr;
    }
    HRESULT STDMETHODCALLTYPE GetCapabilities(LPDIDEVCAPS p) override
        { return m_real->GetCapabilities(p); }
    HRESULT STDMETHODCALLTYPE EnumObjects(LPDIENUMDEVICEOBJECTSCALLBACKA cb,
                                           LPVOID ref, DWORD fl) override
        { return m_real->EnumObjects(cb, ref, fl); }
    HRESULT STDMETHODCALLTYPE GetProperty(REFGUID g, LPDIPROPHEADER p) override
        { return m_real->GetProperty(g, p); }
    HRESULT STDMETHODCALLTYPE SetProperty(REFGUID g, LPCDIPROPHEADER p) override
        { return m_real->SetProperty(g, p); }
    HRESULT STDMETHODCALLTYPE Acquire() override
        { return m_real->Acquire(); }
    HRESULT STDMETHODCALLTYPE Unacquire() override
        { return m_real->Unacquire(); }
    HRESULT STDMETHODCALLTYPE SetDataFormat(LPCDIDATAFORMAT p) override
        { return m_real->SetDataFormat(p); }
    HRESULT STDMETHODCALLTYPE SetEventNotification(HANDLE h) override
        { return m_real->SetEventNotification(h); }
    HRESULT STDMETHODCALLTYPE SetCooperativeLevel(HWND h, DWORD fl) override
        { return m_real->SetCooperativeLevel(h, fl); }
    HRESULT STDMETHODCALLTYPE GetObjectInfo(LPDIDEVICEOBJECTINSTANCEA p,
                                             DWORD o, DWORD h) override
        { return m_real->GetObjectInfo(p, o, h); }
    HRESULT STDMETHODCALLTYPE GetDeviceInfo(LPDIDEVICEINSTANCEA p) override
        { return m_real->GetDeviceInfo(p); }
    HRESULT STDMETHODCALLTYPE RunControlPanel(HWND h, DWORD fl) override
        { return m_real->RunControlPanel(h, fl); }
    HRESULT STDMETHODCALLTYPE Initialize(HINSTANCE h, DWORD v, REFGUID g) override
        { return m_real->Initialize(h, v, g); }
    HRESULT STDMETHODCALLTYPE CreateEffect(REFGUID g, LPCDIEFFECT p,
                                            LPDIRECTINPUTEFFECT* pp,
                                            LPUNKNOWN u) override
        { return m_real->CreateEffect(g, p, pp, u); }
    HRESULT STDMETHODCALLTYPE EnumEffects(LPDIENUMEFFECTSCALLBACKA cb,
                                           LPVOID ref, DWORD fl) override
        { return m_real->EnumEffects(cb, ref, fl); }
    HRESULT STDMETHODCALLTYPE GetEffectInfo(LPDIEFFECTINFOA p, REFGUID g) override
        { return m_real->GetEffectInfo(p, g); }
    HRESULT STDMETHODCALLTYPE GetForceFeedbackState(LPDWORD p) override
        { return m_real->GetForceFeedbackState(p); }
    HRESULT STDMETHODCALLTYPE SendForceFeedbackCommand(DWORD fl) override
        { return m_real->SendForceFeedbackCommand(fl); }
    HRESULT STDMETHODCALLTYPE EnumCreatedEffectObjects(
        LPDIENUMCREATEDEFFECTOBJECTSCALLBACK cb, LPVOID ref, DWORD fl) override
        { return m_real->EnumCreatedEffectObjects(cb, ref, fl); }
    HRESULT STDMETHODCALLTYPE Escape(LPDIEFFESCAPE p) override
        { return m_real->Escape(p); }
    HRESULT STDMETHODCALLTYPE Poll() override
        { return m_real->Poll(); }
    HRESULT STDMETHODCALLTYPE SendDeviceData(DWORD cb,
                                              LPCDIDEVICEOBJECTDATA rg,
                                              LPDWORD pdw, DWORD fl) override
        { return m_real->SendDeviceData(cb, rg, pdw, fl); }
    HRESULT STDMETHODCALLTYPE EnumEffectsInFile(LPCSTR f,
        LPDIENUMEFFECTSINFILECALLBACK cb, LPVOID ref, DWORD fl) override
        { return m_real->EnumEffectsInFile(f, cb, ref, fl); }
    HRESULT STDMETHODCALLTYPE WriteEffectToFile(LPCSTR f, DWORD n,
        LPDIFILEEFFECT rg, DWORD fl) override
        { return m_real->WriteEffectToFile(f, n, rg, fl); }
    HRESULT STDMETHODCALLTYPE BuildActionMap(LPDIACTIONFORMATA p,
                                              LPCSTR u, DWORD fl) override
        { return m_real->BuildActionMap(p, u, fl); }
    HRESULT STDMETHODCALLTYPE SetActionMap(LPDIACTIONFORMATA p,
                                            LPCSTR u, DWORD fl) override
        { return m_real->SetActionMap(p, u, fl); }
    HRESULT STDMETHODCALLTYPE GetImageInfo(LPDIDEVICEIMAGEINFOHEADERA p) override
        { return m_real->GetImageInfo(p); }

private:
    IDirectInputDevice8A* m_real;
    bool                  m_isG29;
};
int ProxyDevice::s_count = 0;

// ── EnumDevices callback shim ─────────────────────────────────────────────────

struct EnumDevCtx { LPDIENUMDEVICESCALLBACKA realCb; LPVOID realRef; };

static BOOL PASCAL EnumDevShim(LPCDIDEVICEINSTANCEA pDDI, LPVOID pvRef) {
    auto* ctx = static_cast<EnumDevCtx*>(pvRef);
    if (pDDI)
        ProxyLogFmt("[g29_ffb] EnumDevices: '%s' inst={%08lX-%04X-%04X-...}",
                    pDDI->tszProductName,
                    pDDI->guidInstance.Data1,
                    pDDI->guidInstance.Data2,
                    pDDI->guidInstance.Data3);
    return ctx->realCb(pDDI, ctx->realRef);
}

// ── ProxyDI8 — wrapper de IDirectInput8A ─────────────────────────────────────

class ProxyDI8 : public IDirectInput8A {
public:
    explicit ProxyDI8(IDirectInput8A* real) : m_real(real) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID r, LPVOID* p) override
        { return m_real->QueryInterface(r, p); }
    ULONG STDMETHODCALLTYPE AddRef()  override { return m_real->AddRef(); }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG ref = m_real->Release();
        if (!ref) delete this;
        return ref;
    }

    HRESULT STDMETHODCALLTYPE CreateDevice(REFGUID guid,
                                            LPDIRECTINPUTDEVICE8A* ppDev,
                                            LPUNKNOWN pUnk) override {
        HRESULT hr = m_real->CreateDevice(guid, ppDev, pUnk);
        if (SUCCEEDED(hr) && ppDev && *ppDev) {
            static LONG s_once = 0;
            if (InterlockedCompareExchange(&s_once, 1, 0) == 0) {
                ProxyLog("[g29_ffb] G29: disparando G29InitThread");
                HANDLE h = CreateThread(nullptr, 0, G29InitThread, nullptr, 0, nullptr);
                if (h) CloseHandle(h);
            }
            *ppDev = new ProxyDevice(*ppDev, IsG29Device(*ppDev));
        }
        return hr;
    }

    HRESULT STDMETHODCALLTYPE EnumDevices(DWORD t, LPDIENUMDEVICESCALLBACKA cb,
                                           LPVOID ref, DWORD fl) override {
        if (cb) {
            EnumDevCtx ctx { cb, ref };
            return m_real->EnumDevices(t, EnumDevShim, &ctx, fl);
        }
        return m_real->EnumDevices(t, cb, ref, fl);
    }
    HRESULT STDMETHODCALLTYPE GetDeviceStatus(REFGUID g) override
        { return m_real->GetDeviceStatus(g); }
    HRESULT STDMETHODCALLTYPE RunControlPanel(HWND h, DWORD f) override
        { return m_real->RunControlPanel(h, f); }
    HRESULT STDMETHODCALLTYPE Initialize(HINSTANCE h, DWORD v) override
        { return m_real->Initialize(h, v); }
    HRESULT STDMETHODCALLTYPE FindDevice(REFGUID c, LPCSTR n, LPGUID g) override
        { return m_real->FindDevice(c, n, g); }
    HRESULT STDMETHODCALLTYPE EnumDevicesBySemantics(LPCSTR u,
        LPDIACTIONFORMATA f, LPDIENUMDEVICESBYSEMANTICSCBA cb,
        LPVOID ref, DWORD fl) override
        { return m_real->EnumDevicesBySemantics(u, f, cb, ref, fl); }
    HRESULT STDMETHODCALLTYPE ConfigureDevices(LPDICONFIGUREDEVICESCALLBACK cb,
        LPDICONFIGUREDEVICESPARAMSA p, DWORD f, LPVOID r) override
        { return m_real->ConfigureDevices(cb, p, f, r); }

private:
    IDirectInput8A* m_real;
};

// ── export ────────────────────────────────────────────────────────────────────

extern "C" HRESULT WINAPI
DirectInput8Create(HINSTANCE hinst, DWORD dwVersion, REFIID riid,
                   LPVOID* ppvOut, LPUNKNOWN pUnk)
{
    if (!EnsureReal()) {
        if (ppvOut) *ppvOut = nullptr;
        return E_FAIL;
    }

    IDirectInput8A* pReal = nullptr;
    HRESULT hr = g_pReal(hinst, dwVersion, riid,
                         reinterpret_cast<LPVOID*>(&pReal), pUnk);
    if (FAILED(hr) || !pReal) {
        ProxyLogFmt("[g29_ffb] dinput: DirectInput8Create falhou (hr=0x%08X)", hr);
        if (ppvOut) *ppvOut = nullptr;
        return hr;
    }

    ProxyLogFmt("[g29_ffb] dinput: DirectInput8Create OK (ver=0x%04X) — wrapping", dwVersion);
    if (ppvOut) *ppvOut = new ProxyDI8(pReal);
    return DI_OK;
}
