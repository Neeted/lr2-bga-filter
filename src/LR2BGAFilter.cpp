//------------------------------------------------------------------------------
// LR2BGAFilter.cpp
// LR2 BGA Filter - フィルタ本体の実装
//------------------------------------------------------------------------------

// Disable problematic includes before streams.h
#define NO_DSHOW_STRSAFE

#include "LR2BGAFilter.h"
#include "LR2BGAImageProc.h"
#include <dvdmedia.h>
#include <tlhelp32.h>

//------------------------------------------------------------------------------
// 定数定義 (Constants)
// マジックナンバーを避け、意図を明確にするための名前付き定数
//------------------------------------------------------------------------------
constexpr DWORD kLetterboxCheckIntervalMs = 200;  // 黒帯検出の頻度制限 (ms)
constexpr DWORD kMaxSleepMs = 1000;               // FPS制限用Sleep上限 (ms)

//------------------------------------------------------------------------------
// フィルタ情報 (Filter Information)
//------------------------------------------------------------------------------
static const AMOVIESETUP_MEDIATYPE sudInputTypes[] = {
    {&MEDIATYPE_Video, &MEDIASUBTYPE_RGB32},
    {&MEDIATYPE_Video, &MEDIASUBTYPE_RGB24}};

static const AMOVIESETUP_MEDIATYPE sudOutputTypes[] = {
    {&MEDIATYPE_Video, &MEDIASUBTYPE_RGB24}};

static const AMOVIESETUP_PIN sudPins[] = {
    {const_cast<LPWSTR>(L"Input"), FALSE, FALSE, FALSE, FALSE, &CLSID_NULL,
     NULL, 2, sudInputTypes},
    {const_cast<LPWSTR>(L"Output"), FALSE, TRUE, FALSE, FALSE, &CLSID_NULL,
     NULL, 1, sudOutputTypes}};

static const AMOVIESETUP_FILTER sudFilter = {
    &CLSID_LR2BGAFilter, L"LR2 BGA Filter",
    MERIT_DO_NOT_USE + 1, // Slightly higher merit
    2, sudPins};

//------------------------------------------------------------------------------
// DLL エントリポイント テンプレート (DLL Entry Point Templates)
//------------------------------------------------------------------------------
// Forward declaration
#include "LR2BGAFilterProp.h"

CFactoryTemplate g_Templates[] = {
    {L"LR2 BGA Filter", &CLSID_LR2BGAFilter, CLR2BGAFilter::CreateInstance,
     NULL, &sudFilter},
    {L"LR2 BGA Filter Property Page", &CLSID_LR2BGAFilterPropertyPage,
     CLR2BGAFilterPropertyPage::CreateInstance, NULL, NULL}};

int g_cTemplates = sizeof(g_Templates) / sizeof(g_Templates[0]);

// HINSTANCE g_hInst; // Defined in baseclasses/dllentry.cpp

//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// DLL エントリポイント (DLL Entry Points)
//------------------------------------------------------------------------------
STDAPI DllRegisterServer() { return AMovieDllRegisterServer2(TRUE); }

STDAPI DllUnregisterServer() { return AMovieDllRegisterServer2(FALSE); }

//------------------------------------------------------------------------------
// OpenConfiguration
// rundll32 から呼び出される設定画面起動用エントリポイント
//------------------------------------------------------------------------------
extern "C" void CALLBACK OpenConfiguration(HWND hwnd, HINSTANCE hInst,
                                           LPSTR lpszCmdLine, int nCmdShow) {
  CoInitialize(NULL);

  // フィルタインスタンスを作成
  ILR2BGAFilterSettings *pSettings = NULL;
  HRESULT hr = CoCreateInstance(CLSID_LR2BGAFilter, NULL, CLSCTX_INPROC_SERVER,
                                IID_ILR2BGAFilterSettings, (void **)&pSettings);

  if (SUCCEEDED(hr) && pSettings) {
    // Query ISpecifyPropertyPages from the same object
    ISpecifyPropertyPages *pPages = NULL;
    hr = pSettings->QueryInterface(IID_ISpecifyPropertyPages, (void **)&pPages);

    if (SUCCEEDED(hr) && pPages) {
      // 設定モードを有効化 (外部ウィンドウ表示などを抑制)
      pSettings->SetConfigurationMode(TRUE);

      CAUUID caGUID;
      if (SUCCEEDED(pPages->GetPages(&caGUID))) {
        // プロパティページを表示
        LCID lcid = GetUserDefaultLCID();
        IUnknown *pUnk = NULL;
        pSettings->QueryInterface(IID_IUnknown, (void **)&pUnk);

        OleCreatePropertyFrame(hwnd, 0, 0, L"LR2 BGA Filter Configuration", 1,
                               &pUnk, caGUID.cElems, caGUID.pElems, lcid, 0,
                               NULL);

        if (pUnk)
          pUnk->Release();
        CoTaskMemFree(caGUID.pElems);
      }
      pPages->Release();
    }
    pSettings->Release();
  }

  CoUninitialize();
}

extern "C" BOOL WINAPI DllEntryPoint(HINSTANCE, ULONG, LPVOID);

BOOL APIENTRY DllMain(HINSTANCE hModule, DWORD dwReason, LPVOID lpReserved) {
  if (dwReason == DLL_PROCESS_ATTACH) {
    g_hInst = hModule;
  } else if (dwReason == DLL_PROCESS_DETACH) {
    // g_hInst = NULL; // Optional
  }
  return DllEntryPoint(hModule, dwReason, lpReserved);
}

//------------------------------------------------------------------------------
// CLR2BGAFilter 実装 (CLR2BGAFilter Implementation)
//------------------------------------------------------------------------------

CLR2BGAFilter::CLR2BGAFilter(LPUNKNOWN pUnk, HRESULT *phr)
    : CTransformFilter(NAME("LR2 BGA Filter"), pUnk, CLSID_LR2BGAFilter),
      m_pSettings(NULL), m_pWindow(NULL), m_bConfigMode(FALSE),
      m_pTransformLogic(new LR2BGATransformLogic(NULL, NULL)),
      // 入力情報初期化
      m_inputWidth(0), m_inputHeight(0), m_inputBitCount(0),
      // 統計情報初期化
      m_frameCount(0), m_processedFrameCount(0), m_inputFrameCount(0),
      m_totalProcessTime(0), m_avgProcessTime(0.0),
      m_frameRate(0.0), m_outputFrameRate(0.0)
{
  // 設定マネージャ初期化
  m_pSettings = new LR2BGASettings();
  m_pSettings->Load();

  // ウィンドウマネージャ初期化
  m_pWindow = new LR2BGAWindow(m_pSettings);
  // プロパティページ用に IUnknown を渡す (IBaseFilter は IUnknown を継承)
  m_pWindow->SetFilter(static_cast<IBaseFilter *>(this));

  // TransformLogicの再初期化（設定とウィンドウを渡す）
  m_pTransformLogic.reset(new LR2BGATransformLogic(m_pSettings, m_pWindow));

  if (phr) {
    *phr = S_OK;
  }
}

CLR2BGAFilter::~CLR2BGAFilter() {
  // レターボックス検出スレッド停止
  // レターボックス検出スレッドが終了していることを確認
  m_pTransformLogic->StopLetterboxThread();

  // std::mutex destroy is automatic

  if (m_pWindow) {
    delete m_pWindow;
    m_pWindow = NULL;
  }

  // 設定マネージャのクリーンアップ
  if (m_pSettings) {
    delete m_pSettings;
    m_pSettings = NULL;
  }

  // m_pTransformLogic is unique_ptr, auto deleted
}

CUnknown *WINAPI CLR2BGAFilter::CreateInstance(LPUNKNOWN pUnk, HRESULT *phr) {
  CLR2BGAFilter *pFilter = new CLR2BGAFilter(pUnk, phr);
  if (pFilter == NULL && phr) {
    *phr = E_OUTOFMEMORY;
  }
  return pFilter;
}

//------------------------------------------------------------------------------
// NonDelegatingQueryInterface - 追加インターフェースの公開
//------------------------------------------------------------------------------
STDMETHODIMP CLR2BGAFilter::NonDelegatingQueryInterface(REFIID riid,
                                                        void **ppv) {
  CheckPointer(ppv, E_POINTER);

  if (riid == IID_ISpecifyPropertyPages) {
    return GetInterface((ISpecifyPropertyPages *)this, ppv);
  }
  if (riid == IID_ILR2BGAFilterSettings) {
    return GetInterface((ILR2BGAFilterSettings *)this, ppv);
  }

  return CTransformFilter::NonDelegatingQueryInterface(riid, ppv);
}

//------------------------------------------------------------------------------
// ISpecifyPropertyPages::GetPages - プロパティページのGUIDを返す
//------------------------------------------------------------------------------
STDMETHODIMP CLR2BGAFilter::GetPages(CAUUID *pPages) {
  CheckPointer(pPages, E_POINTER);

  pPages->cElems = 1;
  pPages->pElems = (GUID *)CoTaskMemAlloc(sizeof(GUID));
  if (pPages->pElems == NULL) {
    return E_OUTOFMEMORY;
  }
  pPages->pElems[0] = CLSID_LR2BGAFilterPropertyPage;

  return S_OK;
}

//------------------------------------------------------------------------------
// ILR2BGAFilterSettings 実装 (LR2BGASettings へ委譲)
//------------------------------------------------------------------------------
STDMETHODIMP CLR2BGAFilter::GetOutputSize(int *pWidth, int *pHeight) {
  CheckPointer(pWidth, E_POINTER);
  CheckPointer(pHeight, E_POINTER);
  *pWidth = m_pSettings->m_outputWidth;
  *pHeight = m_pSettings->m_outputHeight;
  return S_OK;
}

STDMETHODIMP CLR2BGAFilter::SetOutputSize(int width, int height) {
  if (width < 1 || height < 1 || width > 4096 || height > 4096) {
    return E_INVALIDARG;
  }
  m_pSettings->m_outputWidth = width;
  m_pSettings->m_outputHeight = height;
  m_pSettings->Save();
  return S_OK;
}

STDMETHODIMP CLR2BGAFilter::GetResizeAlgorithm(ResizeAlgorithm *pAlgo) {
  CheckPointer(pAlgo, E_POINTER);
  *pAlgo = m_pSettings->m_resizeAlgo;
  return S_OK;
}

STDMETHODIMP CLR2BGAFilter::SetResizeAlgorithm(ResizeAlgorithm algo) {
  if (algo != RESIZE_NEAREST && algo != RESIZE_BILINEAR) {
    return E_INVALIDARG;
  }
  m_pSettings->m_resizeAlgo = algo;
  m_pSettings->Save();
  return S_OK;
}

STDMETHODIMP CLR2BGAFilter::GetKeepAspectRatio(BOOL *pKeep) {
  CheckPointer(pKeep, E_POINTER);
  *pKeep = m_pSettings->m_keepAspectRatio ? TRUE : FALSE;
  return S_OK;
}

STDMETHODIMP CLR2BGAFilter::SetKeepAspectRatio(BOOL keep) {
  m_pSettings->m_keepAspectRatio = (keep != FALSE);
  m_pSettings->Save();
  return S_OK;
}

STDMETHODIMP CLR2BGAFilter::GetDebugMode(BOOL *pDebug) {
  CheckPointer(pDebug, E_POINTER);
  *pDebug = m_pSettings->m_debugMode ? TRUE : FALSE;
  return S_OK;
}

STDMETHODIMP CLR2BGAFilter::SetDebugMode(BOOL debug) {
  m_pSettings->m_debugMode = (debug != FALSE);
  m_pSettings->Save();
  return S_OK;
}

STDMETHODIMP CLR2BGAFilter::GetMaxFPS(int *pMaxFPS) {
  CheckPointer(pMaxFPS, E_POINTER);
  *pMaxFPS = m_pSettings->m_maxFPS;
  return S_OK;
}

STDMETHODIMP CLR2BGAFilter::SetMaxFPS(int maxFPS) {
  if (maxFPS < 1 || maxFPS > 60) {
    return E_INVALIDARG;
  }
  m_pSettings->m_maxFPS = maxFPS;
  m_pSettings->Save();
  return S_OK;
}

STDMETHODIMP CLR2BGAFilter::GetLimitFPS(BOOL *pEnabled) {
  CheckPointer(pEnabled, E_POINTER);
  *pEnabled = m_pSettings->m_limitFPSEnabled ? TRUE : FALSE;
  return S_OK;
}

STDMETHODIMP CLR2BGAFilter::SetLimitFPS(BOOL enabled) {
  m_pSettings->m_limitFPSEnabled = (enabled != FALSE);
  m_pSettings->Save();
  return S_OK;
}

STDMETHODIMP CLR2BGAFilter::GetDummyMode(BOOL *pDummy) {
  CheckPointer(pDummy, E_POINTER);
  *pDummy = m_pSettings->m_dummyMode ? TRUE : FALSE;
  return S_OK;
}

STDMETHODIMP CLR2BGAFilter::SetDummyMode(BOOL dummy) {
  m_pSettings->m_dummyMode = (dummy != FALSE);
  m_pTransformLogic->ResetDummySent(); // Reset on mode change
  m_pSettings->Save();
  return S_OK;
}

STDMETHODIMP CLR2BGAFilter::GetPassthroughMode(BOOL *pPassthrough) {
  CheckPointer(pPassthrough, E_POINTER);
  *pPassthrough = m_pSettings->m_passthroughMode ? TRUE : FALSE;
  return S_OK;
}

STDMETHODIMP CLR2BGAFilter::SetPassthroughMode(BOOL passthrough) {
  m_pSettings->m_passthroughMode = (passthrough != FALSE);
  m_pSettings->Save();
  return S_OK;
}

STDMETHODIMP CLR2BGAFilter::GetExternalWindowEnabled(BOOL *pEnabled) {
  CheckPointer(pEnabled, E_POINTER);
  *pEnabled = m_pSettings->m_extWindowEnabled ? TRUE : FALSE;
  return S_OK;
}

STDMETHODIMP CLR2BGAFilter::SetExternalWindowEnabled(BOOL enabled) {
  m_pSettings->m_extWindowEnabled = (enabled != FALSE);
  m_pSettings->Save();
  // Dynamically show/hide window (Only if not in config mode)
  if (m_pSettings->m_extWindowEnabled && !m_bConfigMode) {
    m_pWindow->ShowExternalWindow();
  } else {
    m_pWindow->CloseExternalWindow();
  }
  return S_OK;
}

STDMETHODIMP CLR2BGAFilter::GetExternalWindowPosition(int *pX, int *pY) {
  CheckPointer(pX, E_POINTER);
  CheckPointer(pY, E_POINTER);
  *pX = m_pSettings->m_extWindowX;
  *pY = m_pSettings->m_extWindowY;
  return S_OK;
}

STDMETHODIMP CLR2BGAFilter::SetExternalWindowPosition(int x, int y) {
  m_pSettings->m_extWindowX = x;
  m_pSettings->m_extWindowY = y;
  m_pSettings->Save();
  if (!m_bConfigMode)
    m_pWindow->UpdateExternalWindowPos();
  return S_OK;
}

STDMETHODIMP CLR2BGAFilter::GetExternalWindowSize(int *pWidth, int *pHeight) {
  CheckPointer(pWidth, E_POINTER);
  CheckPointer(pHeight, E_POINTER);
  *pWidth = m_pSettings->m_extWindowWidth;
  *pHeight = m_pSettings->m_extWindowHeight;
  return S_OK;
}

STDMETHODIMP CLR2BGAFilter::SetExternalWindowSize(int width, int height) {
  if (width < 1 || width > 4096 || height < 1 || height > 4096) {
    return E_INVALIDARG;
  }
  m_pSettings->m_extWindowWidth = width;
  m_pSettings->m_extWindowHeight = height;
  m_pSettings->Save();
  if (!m_bConfigMode)
    m_pWindow->UpdateExternalWindowPos();
  return S_OK;
}

STDMETHODIMP CLR2BGAFilter::GetExternalWindowAlgorithm(ResizeAlgorithm *pAlgo) {
  CheckPointer(pAlgo, E_POINTER);
  *pAlgo = m_pSettings->m_extWindowAlgo;
  return S_OK;
}

STDMETHODIMP CLR2BGAFilter::SetExternalWindowAlgorithm(ResizeAlgorithm algo) {
  m_pSettings->m_extWindowAlgo = algo;
  m_pSettings->Save();
  return S_OK;
}

STDMETHODIMP CLR2BGAFilter::GetExternalWindowKeepAspect(BOOL *pKeep) {
  CheckPointer(pKeep, E_POINTER);
  *pKeep = m_pSettings->m_extWindowKeepAspect ? TRUE : FALSE;
  return S_OK;
}

STDMETHODIMP CLR2BGAFilter::SetExternalWindowKeepAspect(BOOL keep) {
  m_pSettings->m_extWindowKeepAspect = (keep != FALSE);
  m_pSettings->Save();
  return S_OK;
}

STDMETHODIMP CLR2BGAFilter::GetExternalWindowPassthrough(BOOL *pPassthrough) {
  CheckPointer(pPassthrough, E_POINTER);
  *pPassthrough = m_pSettings->m_extWindowPassthrough ? TRUE : FALSE;
  return S_OK;
}

STDMETHODIMP CLR2BGAFilter::SetExternalWindowPassthrough(BOOL passthrough) {
  m_pSettings->m_extWindowPassthrough = (passthrough != FALSE);
  m_pSettings->Save();
  return S_OK;
}

STDMETHODIMP CLR2BGAFilter::GetExternalWindowTopmost(BOOL *pTopmost) {
  CheckPointer(pTopmost, E_POINTER);
  *pTopmost = m_pSettings->m_extWindowTopmost ? TRUE : FALSE;
  return S_OK;
}

STDMETHODIMP CLR2BGAFilter::SetExternalWindowTopmost(BOOL topmost) {
  m_pSettings->m_extWindowTopmost = (topmost != FALSE);
  m_pSettings->Save();
  if (!m_bConfigMode)
    m_pWindow->UpdateExternalWindowPos();
  return S_OK;
}

STDMETHODIMP CLR2BGAFilter::SetConfigurationMode(BOOL bConfigMode) {
  m_bConfigMode = bConfigMode;
  // 設定モードに入った場合、外部ウィンドウを閉じる
  if (m_bConfigMode && m_pWindow) {
    m_pWindow->CloseExternalWindow();
  }
  return S_OK;
}

STDMETHODIMP CLR2BGAFilter::GetBrightnessLR2(int *pBrightness) {
  CheckPointer(pBrightness, E_POINTER);
  *pBrightness = m_pSettings->m_brightnessLR2;
  return S_OK;
}

STDMETHODIMP CLR2BGAFilter::SetBrightnessLR2(int brightness) {
  if (brightness < 0 || brightness > 100)
    return E_INVALIDARG;
  m_pSettings->m_brightnessLR2 = brightness;
  m_pSettings->Save();
  return S_OK;
}

STDMETHODIMP CLR2BGAFilter::GetBrightnessExt(int *pBrightness) {
  CheckPointer(pBrightness, E_POINTER);
  *pBrightness = m_pSettings->m_brightnessExt;
  return S_OK;
}

STDMETHODIMP CLR2BGAFilter::SetBrightnessExt(int brightness) {
  if (brightness < 0 || brightness > 100)
    return E_INVALIDARG;
  m_pSettings->m_brightnessExt = brightness;
  m_pSettings->Save();
  m_pWindow->UpdateOverlayWindow(); // Notify Overlay Window
  return S_OK;
}

STDMETHODIMP CLR2BGAFilter::GetAutoOpenSettings(BOOL *pAutoOpen) {
  CheckPointer(pAutoOpen, E_POINTER);
  m_pSettings->Lock();
  *pAutoOpen = m_pSettings->m_autoOpenSettings ? TRUE : FALSE;
  m_pSettings->Unlock();
  return S_OK;
}

STDMETHODIMP CLR2BGAFilter::SetAutoOpenSettings(BOOL autoOpen) {
  m_pSettings->Lock();
  m_pSettings->m_autoOpenSettings = (autoOpen != FALSE);
  m_pSettings
      ->Unlock(); // Save() も内部でロックを行うため、ここでアンロックする。
                  // Save() は内部で Lock()
                  // を呼ぶ。Windowsのクリティカルセクションは再帰ロック可能だが、
                  // ここでは明確にするために Unlock しておく。

  m_pSettings->Save();
  return S_OK;
}

STDMETHODIMP CLR2BGAFilter::GetCloseOnRightClick(BOOL *pClose) {
  CheckPointer(pClose, E_POINTER);
  m_pSettings->Lock();
  *pClose = m_pSettings->m_closeOnRightClick ? TRUE : FALSE;
  m_pSettings->Unlock();
  return S_OK;
}

STDMETHODIMP CLR2BGAFilter::SetCloseOnRightClick(BOOL close) {
  m_pSettings->Lock();
  m_pSettings->m_closeOnRightClick = (close != FALSE);
  m_pSettings->Unlock();
  m_pSettings->Save();
  return S_OK;
}

STDMETHODIMP CLR2BGAFilter::GetGamepadCloseEnabled(BOOL *pEnabled) {
  CheckPointer(pEnabled, E_POINTER);
  m_pSettings->Lock();
  *pEnabled = m_pSettings->m_gamepadCloseEnabled ? TRUE : FALSE;
  m_pSettings->Unlock();
  return S_OK;
}

STDMETHODIMP CLR2BGAFilter::SetGamepadCloseEnabled(BOOL enabled) {
  m_pSettings->Lock();
  m_pSettings->m_gamepadCloseEnabled = (enabled != FALSE);
  m_pSettings->Unlock();
  m_pSettings->Save();
  m_pWindow->RestartInputMonitor();
  return S_OK;
}

STDMETHODIMP CLR2BGAFilter::GetGamepadID(int *pID) {
  CheckPointer(pID, E_POINTER);
  m_pSettings->Lock();
  *pID = m_pSettings->m_gamepadID;
  m_pSettings->Unlock();
  return S_OK;
}

STDMETHODIMP CLR2BGAFilter::SetGamepadID(int id) {
  if (id < 0 || id > 15)
    return E_INVALIDARG;
  m_pSettings->Lock();
  m_pSettings->m_gamepadID = id;
  m_pSettings->Unlock();
  m_pSettings->Save();
  m_pWindow->RestartInputMonitor();
  return S_OK;
}

STDMETHODIMP CLR2BGAFilter::GetGamepadButtonID(int *pBtnID) {
  CheckPointer(pBtnID, E_POINTER);
  m_pSettings->Lock();
  *pBtnID = m_pSettings->m_gamepadButtonID;
  m_pSettings->Unlock();
  return S_OK;
}

STDMETHODIMP CLR2BGAFilter::SetGamepadButtonID(int btnID) {
  if (btnID < 0 || btnID > 31)
    return E_INVALIDARG;
  m_pSettings->Lock();
  m_pSettings->m_gamepadButtonID = btnID;
  m_pSettings->Unlock();
  m_pSettings->Save();
  m_pWindow->RestartInputMonitor();
  return S_OK;
}

STDMETHODIMP CLR2BGAFilter::GetKeyboardCloseEnabled(BOOL *pEnabled) {
  CheckPointer(pEnabled, E_POINTER);
  m_pSettings->Lock();
  *pEnabled = m_pSettings->m_keyboardCloseEnabled ? TRUE : FALSE;
  m_pSettings->Unlock();
  return S_OK;
}

STDMETHODIMP CLR2BGAFilter::SetKeyboardCloseEnabled(BOOL enabled) {
  m_pSettings->Lock();
  m_pSettings->m_keyboardCloseEnabled = (enabled != FALSE);
  m_pSettings->Unlock();
  m_pSettings->Save();
  m_pWindow->RestartInputMonitor();
  return S_OK;
}

STDMETHODIMP CLR2BGAFilter::GetKeyboardKeyCode(int *pKeyCode) {
  CheckPointer(pKeyCode, E_POINTER);
  m_pSettings->Lock();
  *pKeyCode = m_pSettings->m_keyboardKeyCode;
  m_pSettings->Unlock();
  return S_OK;
}

STDMETHODIMP CLR2BGAFilter::SetKeyboardKeyCode(int keyCode) {
  // 任意のキーコードに対するバリデーションは省略
  m_pSettings->Lock();
  m_pSettings->m_keyboardKeyCode = keyCode;
  m_pSettings->Unlock();
  m_pSettings->Save();
  m_pWindow->RestartInputMonitor();
  return S_OK;
}

STDMETHODIMP CLR2BGAFilter::GetAutoRemoveLetterbox(BOOL *pEnabled) {
  CheckPointer(pEnabled, E_POINTER);
  m_pSettings->Lock();
  *pEnabled = m_pSettings->m_autoRemoveLetterbox;
  m_pSettings->Unlock();
  return S_OK;
}

STDMETHODIMP CLR2BGAFilter::SetAutoRemoveLetterbox(BOOL enabled) {
  m_pSettings->Lock();
  m_pSettings->m_autoRemoveLetterbox = (enabled != FALSE);
  m_pSettings->Unlock();
  m_pSettings->Save();

  m_pTransformLogic->ResetLetterboxState();

  return S_OK;
}

STDMETHODIMP CLR2BGAFilter::GetLetterboxThreshold(int *pThreshold) {
  CheckPointer(pThreshold, E_POINTER);
  m_pSettings->Lock();
  *pThreshold = m_pSettings->m_lbThreshold;
  m_pSettings->Unlock();
  return S_OK;
}

STDMETHODIMP CLR2BGAFilter::SetLetterboxThreshold(int threshold) {
  m_pSettings->Lock();
  m_pSettings->m_lbThreshold = threshold;
  m_pSettings->Unlock();
  m_pSettings->Save();

  // 検出器の設定を更新
  m_pTransformLogic->GetDetector().SetParams(threshold, m_pSettings->m_lbStability);
  return S_OK;
}

STDMETHODIMP CLR2BGAFilter::GetLetterboxStability(int *pStability) {
  CheckPointer(pStability, E_POINTER);
  m_pSettings->Lock();
  *pStability = m_pSettings->m_lbStability;
  m_pSettings->Unlock();
  return S_OK;
}

STDMETHODIMP CLR2BGAFilter::SetLetterboxStability(int stability) {
  m_pSettings->Lock();
  m_pSettings->m_lbStability = stability;
  m_pSettings->Unlock();
  m_pSettings->Save();

  // 検出器の設定を更新
  m_pTransformLogic->GetDetector().SetParams(m_pSettings->m_lbThreshold, stability);
  return S_OK;
}

STDMETHODIMP CLR2BGAFilter::ResetPerformanceStatistics() {
  m_pTransformLogic->ResetStatistics();
  return S_OK;
}

//------------------------------------------------------------------------------
// GetPin - カスタムピン名を使用するためのオーバーライド ("In"/"Out")
//------------------------------------------------------------------------------
CBasePin *CLR2BGAFilter::GetPin(int n) {
  HRESULT hr = S_OK;

  // ピンが未作成の場合に作成する
  if (m_pInput == NULL) {
    // 自己接続(再帰接続)を防ぐためのカスタム入力ピンを使用
    m_pInput =
        new CLR2BGAInputPin(NAME("LR2 BGA Filter Input"), this, &hr, L"In");
    if (FAILED(hr) || m_pInput == NULL) {
      delete m_pInput;
      m_pInput = NULL;
      return NULL;
    }

    m_pOutput = new CTransformOutputPin(NAME("LR2 BGA Filter Output"), this,
                                        &hr, L"Out");
    if (FAILED(hr) || m_pOutput == NULL) {
      delete m_pInput;
      m_pInput = NULL;
      delete m_pOutput;
      m_pOutput = NULL;
      return NULL;
    }
  }

  if (n == 0)
    return m_pInput;
  else if (n == 1)
    return m_pOutput;
  return NULL;
}

//------------------------------------------------------------------------------
// CLR2BGAInputPin::CheckConnect
// 他のLR2 BGA Filterが上流にある場合、接続を拒否する（多重挿入防止）
//------------------------------------------------------------------------------
HRESULT CLR2BGAInputPin::CheckConnect(IPin *pPin) {
  HRESULT hr = CTransformInputPin::CheckConnect(pPin);
  if (FAILED(hr))
    return hr;

  // 上流フィルタの情報を取得
  PIN_INFO pinInfo;
  if (SUCCEEDED(pPin->QueryPinInfo(&pinInfo))) {
    if (pinInfo.pFilter) {
      // CLSIDをチェック
      CLSID clsid;
      if (SUCCEEDED(pinInfo.pFilter->GetClassID(&clsid))) {
        if (IsEqualGUID(clsid, CLSID_LR2BGAFilter)) {
          // 自己接続を防止！
          pinInfo.pFilter->Release();
          return E_FAIL;
        }
      }
      pinInfo.pFilter->Release();
    }
  }
  return S_OK;
}

//------------------------------------------------------------------------------
// StartStreaming - ストリーミング開始
//------------------------------------------------------------------------------
HRESULT CLR2BGAFilter::StartStreaming() {
  // 設定画面の自動オープン
  if (!m_bConfigMode && m_pSettings->m_autoOpenSettings && m_pWindow) {
    m_pWindow->ShowPropertyPage();
  }

  // 外部ウィンドウが有効な場合、表示する
  if (!m_bConfigMode && m_pSettings->m_extWindowEnabled && m_pWindow) {
    m_pWindow->ShowExternalWindow();
  }

  // TransformLogic開始
  m_pTransformLogic->StartStreaming(m_inputWidth, m_inputHeight, m_inputBitCount,
                                    m_pSettings->m_outputWidth, m_pSettings->m_outputHeight);
  // レターボックス検出スレッドを開始 (Logic側)
  m_pTransformLogic->StartLetterboxThread();

  return CTransformFilter::StartStreaming();
}

//------------------------------------------------------------------------------
// StopStreaming - ストリーミング停止
//------------------------------------------------------------------------------
HRESULT CLR2BGAFilter::StopStreaming() {
  CAutoLock lock(&m_csReceive);

  // レターボックス検出スレッドを停止
  m_pTransformLogic->StopLetterboxThread();
  
  // TransformLogic停止
  m_pTransformLogic->StopStreaming();

  // 外部ウィンドウを非表示にする
  if (m_pWindow) {
    m_pWindow->CloseExternalWindow();
  }
  return CTransformFilter::StopStreaming();
}

// EndOfStream implementation is removed as base class handles it

//------------------------------------------------------------------------------
// CheckInputType - 入力メディアタイプのチェック
//------------------------------------------------------------------------------
HRESULT CLR2BGAFilter::CheckInputType(const CMediaType *mtIn) {
  if (mtIn->majortype != MEDIATYPE_Video)
    return VFW_E_TYPE_NOT_ACCEPTED;
  if (mtIn->subtype != MEDIASUBTYPE_RGB32 &&
      mtIn->subtype != MEDIASUBTYPE_RGB24)
    return VFW_E_TYPE_NOT_ACCEPTED;
  if (mtIn->formattype != FORMAT_VideoInfo &&
      mtIn->formattype != FORMAT_VideoInfo2)
    return VFW_E_TYPE_NOT_ACCEPTED;
  return S_OK;
}

//------------------------------------------------------------------------------
// CheckTransform - 入出力の組み合わせチェック
//------------------------------------------------------------------------------
HRESULT CLR2BGAFilter::CheckTransform(const CMediaType *mtIn,
                                      const CMediaType *mtOut) {
  if (FAILED(CheckInputType(mtIn)))
    return VFW_E_TYPE_NOT_ACCEPTED;
  if (mtOut->majortype != MEDIATYPE_Video)
    return VFW_E_TYPE_NOT_ACCEPTED;
  if (mtOut->subtype != MEDIASUBTYPE_RGB24)
    return VFW_E_TYPE_NOT_ACCEPTED;
  return S_OK;
}

//------------------------------------------------------------------------------
// GetMediaType - 出力メディアタイプの取得
//------------------------------------------------------------------------------
HRESULT CLR2BGAFilter::GetMediaType(int iPosition, CMediaType *pMediaType) {
  if (pMediaType == NULL)
    return E_POINTER;
  if (m_pInput->IsConnected() == FALSE)
    return E_UNEXPECTED;
  if (iPosition < 0)
    return E_INVALIDARG;
  if (iPosition > 0)
    return VFW_S_NO_MORE_ITEMS;

  CMediaType mtIn;
  HRESULT hr = m_pInput->ConnectionMediaType(&mtIn);
  if (FAILED(hr))
    return hr;

  int inWidth = 0;
  int inHeight = 0;
  REFERENCE_TIME avgTimePerFrame = 0;
  int inputBitCount = 0;

  if (mtIn.formattype == FORMAT_VideoInfo2) {
    VIDEOINFOHEADER2 *pvi2 = (VIDEOINFOHEADER2 *)mtIn.Format();
    inWidth = pvi2->bmiHeader.biWidth;
    inHeight = abs(pvi2->bmiHeader.biHeight);
    avgTimePerFrame = pvi2->AvgTimePerFrame;
    inputBitCount = pvi2->bmiHeader.biBitCount;
  } else {
    VIDEOINFOHEADER *pvi = (VIDEOINFOHEADER *)mtIn.Format();
    inWidth = pvi->bmiHeader.biWidth;
    inHeight = abs(pvi->bmiHeader.biHeight);
    avgTimePerFrame = pvi->AvgTimePerFrame;
    inputBitCount = pvi->bmiHeader.biBitCount;
  }

  m_inputWidth = inWidth;
  m_inputHeight = inHeight;
  m_inputBitCount = inputBitCount;

  if (avgTimePerFrame > 0) {
    m_frameRate = 10000000.0 / avgTimePerFrame;
  } else {
    m_frameRate = 0.0;
  }

  // 設定に基づいて出力サイズを決定
  // 注意:
  // バッファサイズの問題を防ぐため、StartStreaming/Transformのラッチロジックと一致させる必要があります
  // GetMediaType は StartStreaming の前、接続時に呼ばれるため、ここでは
  // m_pSettings を直接使用します。
  int outWidth, outHeight;
  if (m_pSettings->m_dummyMode) {
    outWidth = 1;
    outHeight = 1;
  } else if (m_pSettings->m_passthroughMode) {
    outWidth = inWidth;
    outHeight = inHeight;
  } else {
    outWidth = m_pSettings->m_outputWidth;
    outHeight = m_pSettings->m_outputHeight;
  }

  pMediaType->SetType(&MEDIATYPE_Video);
  pMediaType->SetSubtype(&MEDIASUBTYPE_RGB24);
  pMediaType->SetFormatType(&FORMAT_VideoInfo);
  pMediaType->SetTemporalCompression(FALSE);

  VIDEOINFOHEADER *pviOut =
      (VIDEOINFOHEADER *)pMediaType->AllocFormatBuffer(sizeof(VIDEOINFOHEADER));
  if (pviOut == NULL)
    return E_OUTOFMEMORY;
  ZeroMemory(pviOut, sizeof(VIDEOINFOHEADER));

  int stride = ((outWidth * 3 + 3) & ~3);

  SetRect(&pviOut->rcSource, 0, 0, outWidth, outHeight);
  SetRect(&pviOut->rcTarget, 0, 0, outWidth, outHeight);
  pviOut->AvgTimePerFrame = avgTimePerFrame;

  pviOut->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  pviOut->bmiHeader.biWidth = outWidth;
  pviOut->bmiHeader.biHeight = outHeight;
  pviOut->bmiHeader.biPlanes = 1;
  pviOut->bmiHeader.biBitCount = 24;
  pviOut->bmiHeader.biCompression = BI_RGB;
  pviOut->bmiHeader.biSizeImage = stride * outHeight;

  pMediaType->SetSampleSize(pviOut->bmiHeader.biSizeImage);

  return S_OK;
}

//------------------------------------------------------------------------------
// DecideBufferSize - 出力バッファサイズの決定
//------------------------------------------------------------------------------
HRESULT CLR2BGAFilter::DecideBufferSize(IMemAllocator *pAlloc,
                                        ALLOCATOR_PROPERTIES *pProp) {
  if (m_pInput->IsConnected() == FALSE)
    return E_UNEXPECTED;

  CMediaType mtOut;
  HRESULT hr = m_pOutput->ConnectionMediaType(&mtOut);
  if (FAILED(hr))
    return hr;

  VIDEOINFOHEADER *pvi = (VIDEOINFOHEADER *)mtOut.Format();

  pProp->cBuffers = 1;
  pProp->cbBuffer = pvi->bmiHeader.biSizeImage;

  if (pProp->cbBuffer == 0) {
    int stride = ((pvi->bmiHeader.biWidth * 3 + 3) & ~3);
    pProp->cbBuffer = stride * abs(pvi->bmiHeader.biHeight);
  }

  ALLOCATOR_PROPERTIES actual;
  hr = pAlloc->SetProperties(pProp, &actual);
  if (FAILED(hr))
    return hr;

  if (actual.cbBuffer < pProp->cbBuffer)
    return E_FAIL;

  return S_OK;
}

//------------------------------------------------------------------------------
// Transform - フレーム変換処理
//
// 役割:
//   入力サンプル (pIn) を受け取り、加工して出力サンプル (pOut) に書き込みます。
//   DirectShowのデータフローの中心となるメソッドです。
//
// 処理フロー:
//   1. パフォーマンス計測開始
//   2. 入出力バッファのポインタ取得
//   3. 黒帯検出 (Auto Letterbox Removal)
//      - 負荷軽減のため、一定間隔 (200ms) で別スレッドに解析を依頼します。
//      - 検出結果 (m_currentLBMode) に基づき、切り出し範囲 (srcRect)
//      を決定します。
//   4. 外部ウィンドウの更新 (プレビュー機能)
//   5. FPS制限
//      - 設定された上限FPSを超えないように、必要に応じて Sleep で待機します。
//   6. 画像変換 (以下のいずれか)
//      - ダミーモード: 黒画面または静止画を出力
//      - パススルー: 入力をそのままコピー
//      - リサイズ: 指定解像度へ拡大・縮小 (バイリニア/最近傍)
//   7. 明るさ調整 (LR2用)
//   8. 統計情報更新
//
// 同期に関する注意:
//   - 設定値 (m_outputWidth 等) は、ストリーミング開始時にラッチされた値
//   (m_activeWidth) を使用します。
//     これは、処理中に設定が変更されてバッファオーバーランが発生するのを防ぐためです。
//------------------------------------------------------------------------------
HRESULT CLR2BGAFilter::Transform(IMediaSample *pIn, IMediaSample *pOut) {
  m_inputFrameCount++;

  REFERENCE_TIME rtStart = 0, rtEnd = 0;
  pIn->GetTime(&rtStart, &rtEnd);

  // パフォーマンス計測
  LARGE_INTEGER startTime, endTime, freq;
  QueryPerformanceFrequency(&freq);
  QueryPerformanceCounter(&startTime);

  // 必要であればデバッグウィンドウや外部ウィンドウを表示
  if (m_pSettings->m_debugMode && m_frameCount == 0) {
    m_pWindow->ShowDebugWindow();
  }

  BYTE *pSrcData;
  HRESULT hr = pIn->GetPointer(&pSrcData);
  if (FAILED(hr)) return hr;

  BYTE *pDstData;
  hr = pOut->GetPointer(&pDstData);
  if (FAILED(hr)) return hr;

  // フォーマット取得
  CMediaType mtIn;
  m_pInput->ConnectionMediaType(&mtIn);

  int srcWidth, srcHeight, srcBitCount;
  if (mtIn.formattype == FORMAT_VideoInfo2) {
    VIDEOINFOHEADER2 *pvi2In = (VIDEOINFOHEADER2 *)mtIn.Format();
    srcWidth = pvi2In->bmiHeader.biWidth;
    srcHeight = abs(pvi2In->bmiHeader.biHeight);
    srcBitCount = pvi2In->bmiHeader.biBitCount;
  } else {
    VIDEOINFOHEADER *pviIn = (VIDEOINFOHEADER *)mtIn.Format();
    srcWidth = pviIn->bmiHeader.biWidth;
    srcHeight = abs(pviIn->bmiHeader.biHeight);
    srcBitCount = pviIn->bmiHeader.biBitCount;
  }
  int srcStride = ((srcWidth * (srcBitCount / 8) + 3) & ~3);

  CMediaType mtOut;
  m_pOutput->ConnectionMediaType(&mtOut);
  VIDEOINFOHEADER *pviOut = (VIDEOINFOHEADER *)mtOut.Format();
  int dstWidth = pviOut->bmiHeader.biWidth;
  int dstHeight = abs(pviOut->bmiHeader.biHeight);
  int dstStride = ((dstWidth * 3 + 3) & ~3);

  // -------------------------------------------------------------------------
  // 黒帯除去ロジック (Delegated to TransformLogic)
  // -------------------------------------------------------------------------
  RECT srcRect = {0, 0, srcWidth, srcHeight};
  RECT *pSrcRect = NULL;
  
  m_pTransformLogic->ProcessLetterboxDetection(pSrcData, pIn->GetActualDataLength(), srcWidth, srcHeight, srcStride, srcBitCount, srcRect, pSrcRect);

  // -------------------------------------------------------------------------
  // 外部ウィンドウ更新
  // -------------------------------------------------------------------------
  if (m_pSettings->m_extWindowEnabled) {
    m_pWindow->UpdateExternalWindow(pSrcData, srcWidth, srcHeight, srcStride,
                                    srcBitCount, pSrcRect);
  }

  // デバッグ情報の更新
  UpdateDebugInfo();

  // -------------------------------------------------------------------------
  // FPS制限 (Delegated to TransformLogic)
  // -------------------------------------------------------------------------
  LARGE_INTEGER midTime;
  QueryPerformanceCounter(&midTime);

  hr = m_pTransformLogic->WaitFPSLimit(rtStart, rtEnd);
  if (hr == S_FALSE) {
      // FPS制限でスキップされた場合でも、ここまでの処理時間を記録する
      // (WaitFPSLimit内のSleep時間は含まない)
      m_processedFrameCount++;
      m_totalProcessTime += (midTime.QuadPart - startTime.QuadPart) * 10000000 / freq.QuadPart;
      return S_FALSE; // Skip
  }

  // -------------------------------------------------------------------------
  // 出力バッファ生成 (Delegated to TransformLogic)
  // -------------------------------------------------------------------------
  LARGE_INTEGER midTime2;
  QueryPerformanceCounter(&midTime2);

  long outDataLen = 0;
  hr = m_pTransformLogic->FillOutputBuffer(pSrcData, pDstData, srcWidth, srcHeight, srcStride, srcBitCount,
                                           dstWidth, dstHeight, dstStride, pSrcRect, rtStart, rtEnd, outDataLen);
  pOut->SetActualDataLength(outDataLen);
  pOut->SetTime(&rtStart, &rtEnd);
  pOut->SetSyncPoint(TRUE);
  
  if (hr == S_FALSE) {
      // ダミーモード待機などでスキップされた場合
      // WaitFPSLimit同様、待機時間を除外して計測する
      m_processedFrameCount++;
      m_totalProcessTime += (midTime2.QuadPart - startTime.QuadPart) * 10000000 / freq.QuadPart;
      return S_FALSE; // Dummy skip
  }

  // 統計情報更新 (正常出力)
  QueryPerformanceCounter(&endTime);
  m_frameCount++; // 出力フレーム数
  m_processedFrameCount++; // 計測対象フレーム数
  m_totalProcessTime +=
      (endTime.QuadPart - startTime.QuadPart) * 10000000 / freq.QuadPart;

  return S_OK;
}

// ------------------------------------------------------------------------------
// Helper: UpdateDebugInfo - デバッグ情報の更新
//
// 役割:
//   現在のフィルタ状態（接続情報、画像サイズ、フレームレート、統計情報）を
//   UIウィンドウに反映させます。
//
// 処理内容:
//   1. 設定フラグ (m_debugMode) をチェック。無効なら何もしない。
//   2. 平均処理時間を計算 (m_totalProcessTime / m_processedFrameCount)。
//   3. 上流・下流の接続先フィルタ名を取得。
//   4. UIウィンドウの更新メソッドを呼び出し、全情報を渡す。
// ------------------------------------------------------------------------------
void CLR2BGAFilter::UpdateDebugInfo() {
  if (!m_pSettings->m_debugMode)
    return;

  if (m_totalProcessTime > 0 && m_processedFrameCount > 0) {
    m_avgProcessTime = (double)m_totalProcessTime / m_processedFrameCount / 10000.0;
  }

  // 上流・下流の接続情報を取得 (Header表示用)
  PIN_INFO pinInfo = {0};
  std::wstring inputName = L"Disconnected";
  std::wstring outputName = L"Disconnected";

  if (m_pInput->IsConnected()) {
    IPin *pPeer = NULL;
    m_pInput->ConnectedTo(&pPeer);
    if (pPeer) {
      if (SUCCEEDED(pPeer->QueryPinInfo(&pinInfo))) {
        if (pinInfo.pFilter) {
          FILTER_INFO filterInfo = {0};
          pinInfo.pFilter->QueryFilterInfo(&filterInfo);
          inputName = filterInfo.achName;
          filterInfo.pGraph->Release();
          pinInfo.pFilter->Release();
        }
      }
      pPeer->Release();
    }
  }

  if (m_pOutput->IsConnected()) {
    IPin *pPeer = NULL;
    m_pOutput->ConnectedTo(&pPeer);
    if (pPeer) {
      if (SUCCEEDED(pPeer->QueryPinInfo(&pinInfo))) {
        if (pinInfo.pFilter) {
          FILTER_INFO filterInfo = {0};
          pinInfo.pFilter->QueryFilterInfo(&filterInfo);
          outputName = filterInfo.achName;
          filterInfo.pGraph->Release();
          pinInfo.pFilter->Release();
        }
      }
      pPeer->Release();
    }
  }

  // フィルタグラフ全体の情報を取得
  std::wstring graphInfo = GetFilterGraphInfo();

  m_pWindow->UpdateDebugInfo(
      inputName, outputName, graphInfo, m_inputWidth, m_inputHeight,
      m_inputBitCount, m_pSettings->m_outputWidth, m_pSettings->m_outputHeight,
      m_frameRate, m_outputFrameRate, m_frameCount, m_pTransformLogic->GetDroppedFrames(),
      m_avgProcessTime, m_pTransformLogic->GetDetector().GetDebugInfo());
}

// ------------------------------------------------------------------------------
// 上流のフィルタ名を再帰的に取得するヘルパー
// ------------------------------------------------------------------------------
void CollectUpstream(IPin *pPin, std::vector<std::wstring> &filters,
                     int depth) {
  if (depth > 20)
    return;

  IPin *pPeer = NULL;
  pPin->ConnectedTo(&pPeer);
  if (pPeer) {
    PIN_INFO pinInfo = {0};
    if (SUCCEEDED(pPeer->QueryPinInfo(&pinInfo))) {
      if (pinInfo.pFilter) {
        FILTER_INFO filterInfo = {0};
        pinInfo.pFilter->QueryFilterInfo(&filterInfo);

        // リストの先頭に追加（逆順に辿っているため）
        filters.insert(filters.begin(), filterInfo.achName);

        // さらに入力ピンを探して遡る
        IEnumPins *pEnum = NULL;
        if (SUCCEEDED(pinInfo.pFilter->EnumPins(&pEnum))) {
          IPin *pNextPin = NULL;
          while (pEnum->Next(1, &pNextPin, NULL) == S_OK) {
            PIN_DIRECTION dir;
            pNextPin->QueryDirection(&dir);
            if (dir == PINDIR_INPUT) {
              if (pNextPin->ConnectedTo(NULL) != VFW_E_NOT_CONNECTED) {
                CollectUpstream(pNextPin, filters, depth + 1);
              }
              pNextPin->Release();
              break;
            }
            pNextPin->Release();
          }
          pEnum->Release();
        }

        if (filterInfo.pGraph)
          filterInfo.pGraph->Release();
        pinInfo.pFilter->Release();
      }
    }
    pPeer->Release();
  }
}

// ------------------------------------------------------------------------------
// 下流のフィルタ名を再帰的に取得するヘルパー
// ------------------------------------------------------------------------------
void CollectDownstream(IPin *pPin, std::vector<std::wstring> &filters,
                       int depth) {
  if (depth > 20)
    return;

  IPin *pPeer = NULL;
  pPin->ConnectedTo(&pPeer);
  if (pPeer) {
    PIN_INFO pinInfo = {0};
    if (SUCCEEDED(pPeer->QueryPinInfo(&pinInfo))) {
      if (pinInfo.pFilter) {
        FILTER_INFO filterInfo = {0};
        pinInfo.pFilter->QueryFilterInfo(&filterInfo);

        filters.push_back(filterInfo.achName);

        // さらに出力ピンを探して下る
        IEnumPins *pEnum = NULL;
        if (SUCCEEDED(pinInfo.pFilter->EnumPins(&pEnum))) {
          IPin *pNextPin = NULL;
          while (pEnum->Next(1, &pNextPin, NULL) == S_OK) {
            PIN_DIRECTION dir;
            pNextPin->QueryDirection(&dir);
            if (dir == PINDIR_OUTPUT) {
              if (pNextPin->ConnectedTo(NULL) == S_OK) {
                CollectDownstream(pNextPin, filters, depth + 1);
              }
              pNextPin->Release();
              break;
            }
            pNextPin->Release();
          }
          pEnum->Release();
        }

        if (filterInfo.pGraph)
          filterInfo.pGraph->Release();
        pinInfo.pFilter->Release();
      }
    }
    pPeer->Release();
  }
}

std::wstring CLR2BGAFilter::GetFilterGraphInfo() {
  std::vector<std::wstring> filters;

  // 1. 上流を収集
  CollectUpstream(m_pInput, filters, 1);

  // 2. 自分自身を追加
  filters.push_back(L"LR2 BGA Filter (Me)");

  // 3. 下流を収集
  CollectDownstream(m_pOutput, filters, 1);

  // 番号付きリスト文字列を作成
  std::wstring info = L"";
  wchar_t buf[512];
  for (size_t i = 0; i < filters.size(); i++) {
    swprintf_s(buf, 512, L"%d. [%s]\r\n", (int)(i + 1), filters[i].c_str());
    info += buf;
  }

  return info;
}

STDMETHODIMP CLR2BGAFilter::GetDebugWindowRect(int *pX, int *pY, int *pWidth, int *pHeight) {
  CheckPointer(pX, E_POINTER);
  CheckPointer(pY, E_POINTER);
  CheckPointer(pWidth, E_POINTER);
  CheckPointer(pHeight, E_POINTER);
  if (m_pSettings) {
    m_pSettings->Lock();
    *pX = m_pSettings->m_debugWindowX;
    *pY = m_pSettings->m_debugWindowY;
    *pWidth = m_pSettings->m_debugWindowWidth;
    *pHeight = m_pSettings->m_debugWindowHeight;
    m_pSettings->Unlock();
    return S_OK;
  }
  return E_FAIL;
}

STDMETHODIMP CLR2BGAFilter::SetDebugWindowRect(int x, int y, int width, int height) {
  if (m_pSettings) {
    m_pSettings->Lock();
    m_pSettings->m_debugWindowX = x;
    m_pSettings->m_debugWindowY = y;
    m_pSettings->m_debugWindowWidth = width;
    m_pSettings->m_debugWindowHeight = height;
    m_pSettings->Save(); // 即時保存
    m_pSettings->Unlock();
    return S_OK;
  }
  return E_FAIL;
}

void CLR2BGAFilter::FocusLR2Window() { m_pWindow->FocusLR2Window(); }

// ------------------------------------------------------------------------------
// Letterbox Control Wrappers (Delegated to TransformLogic)
// ------------------------------------------------------------------------------
void CLR2BGAFilter::StartLetterboxThread() {
    if(m_pTransformLogic) m_pTransformLogic->StartLetterboxThread();
}

void CLR2BGAFilter::StopLetterboxThread() {
    if(m_pTransformLogic) m_pTransformLogic->StopLetterboxThread();
}

