//------------------------------------------------------------------------------
// LR2BGAFilter.cpp
// LR2 BGA Filter - フィルタ本体の実装
//------------------------------------------------------------------------------

// Disable problematic includes before streams.h
#define NO_DSHOW_STRSAFE

#include "LR2BGAFilter.h"
#include "LR2MemoryMonitor.h"
#include <dvdmedia.h>
#include <tlhelp32.h>
#include <string>

//------------------------------------------------------------------------------
// 定数定義 (Constants)
// マジックナンバーを避け、意図を明確にするための名前付き定数
//------------------------------------------------------------------------------
constexpr DWORD kLetterboxCheckIntervalMs = 200;  // 黒帯検出の頻度制限 (ms)
constexpr DWORD kMaxSleepMs = 1000;               // FPS制限用Sleep上限 (ms)

namespace {
std::wstring GuidToString(const GUID& guid) {
  wchar_t buf[64] = {0};
  StringFromGUID2(guid, buf, static_cast<int>(_countof(buf)));
  return std::wstring(buf);
}

GUID GuidFromFourCC(DWORD fourcc) {
  GUID g = {0};
  g.Data1 = fourcc;
  g.Data2 = 0x0000;
  g.Data3 = 0x0010;
  g.Data4[0] = 0x80;
  g.Data4[1] = 0x00;
  g.Data4[2] = 0x00;
  g.Data4[3] = 0xAA;
  g.Data4[4] = 0x00;
  g.Data4[5] = 0x38;
  g.Data4[6] = 0x9B;
  g.Data4[7] = 0x71;
  return g;
}

void LogRendererModuleByClsid(const CLSID& clsid) {
  std::wstring clsidStr = GuidToString(clsid);
  std::wstring keyPath = L"CLSID\\";
  keyPath += clsidStr;
  keyPath += L"\\InprocServer32";

  HKEY hKey = NULL;
  if (RegOpenKeyExW(HKEY_CLASSES_ROOT, keyPath.c_str(), 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
    wchar_t msg[256];
    swprintf_s(msg, L"[LR2BGAFilter] Renderer CLSID=%s (InprocServer32 lookup failed)\n",
               clsidStr.c_str());
    OutputDebugStringW(msg);
    return;
  }

  wchar_t modulePath[MAX_PATH] = {0};
  DWORD type = 0;
  DWORD cb = sizeof(modulePath);
  LONG q = RegQueryValueExW(hKey, NULL, NULL, &type, reinterpret_cast<LPBYTE>(modulePath), &cb);
  RegCloseKey(hKey);

  if (q == ERROR_SUCCESS && (type == REG_SZ || type == REG_EXPAND_SZ)) {
    wchar_t msg[1024];
    swprintf_s(msg, L"[LR2BGAFilter] Renderer CLSID=%s module=%s\n", clsidStr.c_str(), modulePath);
    OutputDebugStringW(msg);
  } else {
    wchar_t msg[256];
    swprintf_s(msg, L"[LR2BGAFilter] Renderer CLSID=%s (module path unavailable)\n",
               clsidStr.c_str());
    OutputDebugStringW(msg);
  }
}

struct QueryAcceptProbe {
  const wchar_t* name;
  GUID subtype;
  DWORD biCompression;
  WORD bitCount;
  int bytesPerPixelNum; // numerator
  int bytesPerPixelDen; // denominator
};

void ProbeQueryAccept(IPin* pDownstreamPin, LONG width, LONG height) {
  if (!pDownstreamPin || width <= 0 || height <= 0) return;

  const GUID yuy2 = GuidFromFourCC(MAKEFOURCC('Y', 'U', 'Y', '2'));
  const GUID uyvy = GuidFromFourCC(MAKEFOURCC('U', 'Y', 'V', 'Y'));
  const GUID yv12 = GuidFromFourCC(MAKEFOURCC('Y', 'V', '1', '2'));
  const GUID nv12 = GuidFromFourCC(MAKEFOURCC('N', 'V', '1', '2'));

  const QueryAcceptProbe probes[] = {
      {L"RGB24", MEDIASUBTYPE_RGB24, BI_RGB, 24, 3, 1},
      {L"RGB32", MEDIASUBTYPE_RGB32, BI_RGB, 32, 4, 1},
      {L"YUY2", yuy2, MAKEFOURCC('Y', 'U', 'Y', '2'), 16, 2, 1},
      {L"UYVY", uyvy, MAKEFOURCC('U', 'Y', 'V', 'Y'), 16, 2, 1},
      {L"YV12", yv12, MAKEFOURCC('Y', 'V', '1', '2'), 12, 3, 2},
      {L"NV12", nv12, MAKEFOURCC('N', 'V', '1', '2'), 12, 3, 2},
  };

  for (const auto& p : probes) {
    VIDEOINFOHEADER vih = {};
    vih.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    vih.bmiHeader.biWidth = width;
    vih.bmiHeader.biHeight = height;
    vih.bmiHeader.biPlanes = 1;
    vih.bmiHeader.biBitCount = p.bitCount;
    vih.bmiHeader.biCompression = p.biCompression;
    LONG stride = ((width * p.bytesPerPixelNum / p.bytesPerPixelDen) + 3) & ~3;
    vih.bmiHeader.biSizeImage = stride * abs(height);

    AM_MEDIA_TYPE mt = {};
    mt.majortype = MEDIATYPE_Video;
    mt.subtype = p.subtype;
    mt.formattype = FORMAT_VideoInfo;
    mt.bFixedSizeSamples = TRUE;
    mt.bTemporalCompression = FALSE;
    mt.lSampleSize = vih.bmiHeader.biSizeImage;
    mt.cbFormat = sizeof(VIDEOINFOHEADER);
    mt.pbFormat = reinterpret_cast<BYTE*>(&vih);

    HRESULT hr = pDownstreamPin->QueryAccept(&mt);

    const wchar_t* verdict = (hr == S_OK) ? L"accepted"
                           : (hr == S_FALSE) ? L"rejected"
                                             : L"error";
    wchar_t line[512];
    swprintf_s(line, L"[LR2BGAFilter] QueryAccept %-5s => hr=0x%08X (%s)\n",
               p.name, static_cast<unsigned int>(hr), verdict);
    OutputDebugStringW(line);
  }
}

void LogMediaTypeDebug(const AM_MEDIA_TYPE* pmt, const wchar_t* context, int index = -1) {
  if (!pmt) return;

  std::wstring major = GuidToString(pmt->majortype);
  std::wstring sub = GuidToString(pmt->subtype);
  std::wstring fmt = GuidToString(pmt->formattype);

  long w = 0, h = 0;
  WORD bit = 0;
  REFERENCE_TIME atpf = 0;

  if (pmt->formattype == FORMAT_VideoInfo && pmt->pbFormat &&
      pmt->cbFormat >= sizeof(VIDEOINFOHEADER)) {
    const VIDEOINFOHEADER* pvi = reinterpret_cast<const VIDEOINFOHEADER*>(pmt->pbFormat);
    w = pvi->bmiHeader.biWidth;
    h = pvi->bmiHeader.biHeight;
    bit = pvi->bmiHeader.biBitCount;
    atpf = pvi->AvgTimePerFrame;
  } else if (pmt->formattype == FORMAT_VideoInfo2 && pmt->pbFormat &&
             pmt->cbFormat >= sizeof(VIDEOINFOHEADER2)) {
    const VIDEOINFOHEADER2* pvi2 = reinterpret_cast<const VIDEOINFOHEADER2*>(pmt->pbFormat);
    w = pvi2->bmiHeader.biWidth;
    h = pvi2->bmiHeader.biHeight;
    bit = pvi2->bmiHeader.biBitCount;
    atpf = pvi2->AvgTimePerFrame;
  }

  wchar_t line[1024];
  if (index >= 0) {
    swprintf_s(line, L"[LR2BGAFilter] %s[%d] major=%s subtype=%s format=%s size=%ldx%ld bit=%u AvgTimePerFrame=%lld\n",
               context, index, major.c_str(), sub.c_str(), fmt.c_str(),
               w, h, bit, atpf);
  } else {
    swprintf_s(line, L"[LR2BGAFilter] %s major=%s subtype=%s format=%s size=%ldx%ld bit=%u AvgTimePerFrame=%lld\n",
               context, major.c_str(), sub.c_str(), fmt.c_str(),
               w, h, bit, atpf);
  }
  OutputDebugStringW(line);
}
} // namespace

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
    0xfff00000, // MERIT_PREFERRED + 0x80000 (Highest priority)
    2, sudPins};

//------------------------------------------------------------------------------
// DLL エントリポイント テンプレート (DLL Entry Point Templates)
//------------------------------------------------------------------------------
// Forward declaration
#include "LR2BGAFilterProp.h"
#include "LR2NullAudioRenderer.h"

// 外部参照: sudNullAudioFilter (LR2NullAudioRenderer.cpp で定義)
extern AMOVIESETUP_FILTER sudNullAudioFilter;

CFactoryTemplate g_Templates[] = {
    {L"LR2 BGA Filter", &CLSID_LR2BGAFilter, CLR2BGAFilter::CreateInstance,
     NULL, &sudFilter},
    {L"LR2 BGA Filter Property Page", &CLSID_LR2BGAFilterPropertyPage,
     CLR2BGAFilterPropertyPage::CreateInstance, NULL, NULL},
    {L"LR2 BGA Null Audio Renderer", &CLSID_LR2NullAudioRenderer,
     CLR2NullAudioRenderer::CreateInstance, NULL, &sudNullAudioFilter}};

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
      // ラッチ設定初期化
      m_activePassthrough(false), m_activeDummy(false),
      m_activeWidth(0), m_activeHeight(0),
      // 統計情報初期化
      m_frameCount(0), m_processedFrameCount(0), m_inputFrameCount(0),
      m_totalProcessTime(0), m_avgProcessTime(0.0),
      m_frameRate(0.0), m_outputFrameRate(0.0), m_qpcFrequency({0}),
      m_pMemoryMonitor(std::make_unique<LR2MemoryMonitor>())
{
  QueryPerformanceFrequency(&m_qpcFrequency);

  // 設定マネージャ初期化
  m_pSettings = new LR2BGASettings();
  m_pSettings->Load();

  // ウィンドウマネージャ初期化
  m_pWindow = new LR2BGAWindow(m_pSettings);
  // プロパティページ用に IUnknown を渡す (IBaseFilter は IUnknown を継承)
  m_pWindow->SetFilter(static_cast<IBaseFilter *>(this));

  // TransformLogicの再初期化（設定とウィンドウを渡す）
  m_pTransformLogic.reset(new LR2BGATransformLogic(m_pSettings, m_pWindow));

  // Memory Monitor Callback
  m_pMemoryMonitor->SetResultCallback([this](int sceneId) {
      if (m_pWindow) {
          // Notify window of scene change (for close on result, etc.)
          m_pWindow->OnSceneChanged(sceneId);
      }
  });

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

STDMETHODIMP CLR2BGAFilter::GetCloseOnResult(BOOL *pClose) {
  CheckPointer(pClose, E_POINTER);
  m_pSettings->Lock();
  *pClose = m_pSettings->m_closeOnResult ? TRUE : FALSE;
  m_pSettings->Unlock();
  return S_OK;
}

STDMETHODIMP CLR2BGAFilter::SetCloseOnResult(BOOL close) {
  m_pSettings->Lock();
  m_pSettings->m_closeOnResult = (close != FALSE);
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

    m_pOutput = new CLR2BGAOutputPin(NAME("LR2 BGA Filter Output"), this,
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
// CLR2BGAOutputPin::CheckConnect
// 接続先フィルタの検証 (プロセス名チェック、レンダラーチェック)
//------------------------------------------------------------------------------
HRESULT CLR2BGAOutputPin::CheckConnect(IPin *pPin) {
  HRESULT hr = CTransformOutputPin::CheckConnect(pPin);
  if (FAILED(hr)) return hr;

  CLR2BGAFilter* pFilter = static_cast<CLR2BGAFilter*>(m_pTransformFilter);
  LR2BGASettings* pSettings = pFilter->m_pSettings;
  const bool debugEnabled = (pSettings && pSettings->m_debugMode);

  if (debugEnabled) {
    // Debug: 下流ピンが受け入れるMediaType候補を列挙
    PIN_INFO pinInfoDbg = {0};
    if (SUCCEEDED(pPin->QueryPinInfo(&pinInfoDbg))) {
      wchar_t filterName[128] = L"(unknown)";
      if (pinInfoDbg.pFilter) {
        FILTER_INFO fi = {0};
        if (SUCCEEDED(pinInfoDbg.pFilter->QueryFilterInfo(&fi))) {
          wcsncpy_s(filterName, fi.achName, _TRUNCATE);
          if (fi.pGraph) fi.pGraph->Release();
        }
        CLSID clsid = CLSID_NULL;
        if (SUCCEEDED(pinInfoDbg.pFilter->GetClassID(&clsid))) {
          std::wstring clsidStr = GuidToString(clsid);
          wchar_t cmsg[256];
          swprintf_s(cmsg, L"[LR2BGAFilter] Downstream filter CLSID=%s\n", clsidStr.c_str());
          OutputDebugStringW(cmsg);
          LogRendererModuleByClsid(clsid);
        }
      }
      wchar_t info[512];
      swprintf_s(info, L"[LR2BGAFilter] CheckConnect downstream pin=%s filter=%s\n",
                 pinInfoDbg.achName, filterName);
      OutputDebugStringW(info);
      if (pinInfoDbg.pFilter) pinInfoDbg.pFilter->Release();
    }
    IEnumMediaTypes* pEnumMT = NULL;
    HRESULT hrEnum = pPin->EnumMediaTypes(&pEnumMT);
    if (SUCCEEDED(hrEnum) && pEnumMT) {
      wchar_t startMsg[256];
      swprintf_s(startMsg, L"[LR2BGAFilter] EnumMediaTypes started hr=0x%08X\n",
                 static_cast<unsigned int>(hrEnum));
      OutputDebugStringW(startMsg);

      AM_MEDIA_TYPE* pmt = NULL;
      int idx = 0;
      HRESULT hrNext = S_OK;
      while ((hrNext = pEnumMT->Next(1, &pmt, NULL)) == S_OK) {
        LogMediaTypeDebug(pmt, L"DownstreamAccept", idx++);
        DeleteMediaType(pmt);
        pmt = NULL;
      }

      if (idx == 0) {
        OutputDebugStringW(L"[LR2BGAFilter] EnumMediaTypes returned 0 entries.\n");
      }
      wchar_t endMsg[256];
      swprintf_s(endMsg, L"[LR2BGAFilter] EnumMediaTypes finished count=%d hr=0x%08X\n",
                 idx, static_cast<unsigned int>(hrNext));
      OutputDebugStringW(endMsg);

      pEnumMT->Release();
    } else {
      wchar_t failMsg[256];
      swprintf_s(failMsg, L"[LR2BGAFilter] CheckConnect: EnumMediaTypes unavailable hr=0x%08X\n",
                 static_cast<unsigned int>(hrEnum));
      OutputDebugStringW(failMsg);
    }

    // Debug: QueryAcceptで代表フォーマット受理可否をプローブ
    LONG probeW = pFilter->m_pSettings ? pFilter->m_pSettings->m_outputWidth : 256;
    LONG probeH = pFilter->m_pSettings ? pFilter->m_pSettings->m_outputHeight : 256;
    ProbeQueryAccept(pPin, probeW, probeH);
  }

  // 1. プロセス名チェック (Only output to LR2)
  if (pSettings->m_onlyOutputToLR2) {
    WCHAR szPath[MAX_PATH];
    if (GetModuleFileNameW(NULL, szPath, MAX_PATH)) {
      // パスを小文字に変換してチェック
      _wcslwr_s(szPath, MAX_PATH);
      if (wcsstr(szPath, L"body") == NULL) {
        // "body" が含まれていない場合、接続を拒否
        // ただし、設定ツール(dllhost, rundll32)からの呼び出しは考慮する必要があるかも？
        // 基本的にフィルタ接続はホストプロセス内で行われるため、これで機能するはず。
        // GraphStudioNext等の別プロセスでロードされた場合は拒否される（意図通り）。
        return E_FAIL;
      }
    }
  }

  // 2. 出力先フィルタの種類チェック (Only output to renderer)
  if (pSettings->m_onlyOutputToRenderer) {
    PIN_INFO pinInfo;
    if (SUCCEEDED(pPin->QueryPinInfo(&pinInfo))) {
      if (pinInfo.pFilter) {
        bool bHasOutputPin = false;
        IEnumPins* pEnum = NULL;
        if (SUCCEEDED(pinInfo.pFilter->EnumPins(&pEnum))) {
          IPin* pP = NULL;
          while (pEnum->Next(1, &pP, NULL) == S_OK) {
            PIN_DIRECTION dir;
            pP->QueryDirection(&dir);
            if (dir == PINDIR_OUTPUT) {
              bHasOutputPin = true;
            }
            pP->Release();
            if (bHasOutputPin) break; 
          }
          pEnum->Release();
        }
        pinInfo.pFilter->Release();

        // 出力ピンを持っている場合（＝レンダラーではない）、接続を拒否
        if (bHasOutputPin) {
          return E_FAIL;
        }
      }
    }
  }

  return S_OK;
}

//------------------------------------------------------------------------------
// Settings Implementation (Connection Restrictions)
//------------------------------------------------------------------------------
STDMETHODIMP CLR2BGAFilter::GetOnlyOutputToLR2(BOOL *pEnabled) {
  CheckPointer(pEnabled, E_POINTER);
  m_pSettings->Lock();
  *pEnabled = m_pSettings->m_onlyOutputToLR2 ? TRUE : FALSE;
  m_pSettings->Unlock();
  return S_OK;
}

STDMETHODIMP CLR2BGAFilter::SetOnlyOutputToLR2(BOOL enabled) {
  m_pSettings->Lock();
  m_pSettings->m_onlyOutputToLR2 = (enabled != FALSE);
  m_pSettings->Unlock();
  m_pSettings->Save();
  return S_OK;
}

STDMETHODIMP CLR2BGAFilter::GetOnlyOutputToRenderer(BOOL *pEnabled) {
  CheckPointer(pEnabled, E_POINTER);
  m_pSettings->Lock();
  *pEnabled = m_pSettings->m_onlyOutputToRenderer ? TRUE : FALSE;
  m_pSettings->Unlock();
  return S_OK;
}

STDMETHODIMP CLR2BGAFilter::SetOnlyOutputToRenderer(BOOL enabled) {
  m_pSettings->Lock();
  m_pSettings->m_onlyOutputToRenderer = (enabled != FALSE);
  m_pSettings->Unlock();
  m_pSettings->Save();
  return S_OK;
}



//------------------------------------------------------------------------------
// StartStreaming - ストリーミング開始
//------------------------------------------------------------------------------
HRESULT CLR2BGAFilter::StartStreaming() {
  // 入力フォーマットをここで確定（Transformで参照するキャッシュ）
  if (m_pInput->IsConnected() == FALSE) {
    return E_UNEXPECTED;
  }
  CMediaType mtIn;
  HRESULT hrIn = m_pInput->ConnectionMediaType(&mtIn);
  if (FAILED(hrIn)) {
    return hrIn;
  }
  if (FAILED(CheckInputType(&mtIn))) {
    return VFW_E_TYPE_NOT_ACCEPTED;
  }

  REFERENCE_TIME avgTimePerFrame = 0;
  if (mtIn.formattype == FORMAT_VideoInfo2) {
    VIDEOINFOHEADER2 *pvi2In = (VIDEOINFOHEADER2 *)mtIn.Format();
    m_inputWidth = pvi2In->bmiHeader.biWidth;
    m_inputHeight = abs(pvi2In->bmiHeader.biHeight);
    m_inputBitCount = pvi2In->bmiHeader.biBitCount;
    avgTimePerFrame = pvi2In->AvgTimePerFrame;
  } else {
    VIDEOINFOHEADER *pviIn = (VIDEOINFOHEADER *)mtIn.Format();
    m_inputWidth = pviIn->bmiHeader.biWidth;
    m_inputHeight = abs(pviIn->bmiHeader.biHeight);
    m_inputBitCount = pviIn->bmiHeader.biBitCount;
    avgTimePerFrame = pviIn->AvgTimePerFrame;
  }
  m_frameRate = (avgTimePerFrame > 0) ? (10000000.0 / avgTimePerFrame) : 0.0;

  // 設定画面の自動オープン
  if (!m_bConfigMode && m_pSettings->m_autoOpenSettings && m_pWindow) {
    m_pWindow->ShowPropertyPage();
  }

  // 外部ウィンドウが有効な場合、表示する
  if (!m_bConfigMode && m_pSettings->m_extWindowEnabled && m_pWindow) {
    m_pWindow->ShowExternalWindow();
  }

  // 出力サイズの決定 (SetMediaType と同じロジック)
  int outWidth, outHeight;
  if (m_pSettings->m_dummyMode) {
    outWidth = 1;
    outHeight = 1;
  } else if (m_pSettings->m_passthroughMode) {
    // パススルーモードでは入力サイズをそのまま使用
    outWidth = m_inputWidth;
    outHeight = m_inputHeight;
  } else {
    outWidth = m_pSettings->m_outputWidth;
    outHeight = m_pSettings->m_outputHeight;
  }

  // Transform用にサイズをラッチ (毎フレームのMediaType取得を回避)
  m_activeWidth = outWidth;
  m_activeHeight = outHeight;

  // TransformLogic開始
  m_pTransformLogic->StartStreaming(m_inputWidth, m_inputHeight, m_inputBitCount,
                                    outWidth, outHeight);
  // レターボックス検出スレッドを開始 (Logic側)
  m_pTransformLogic->StartLetterboxThread();

  // Memory Monitor Start
  // 設定が有効な場合のみスレッドを開始する
  if (m_pMemoryMonitor && m_pSettings->m_closeOnResult) {
      m_pMemoryMonitor->Start();
  }

  if (m_pSettings && m_pSettings->m_debugMode) {
    // Debug: 接続確定後の最終出力MediaTypeを記録
    CMediaType mtOut;
    if (SUCCEEDED(m_pOutput->ConnectionMediaType(&mtOut))) {
      LogMediaTypeDebug(static_cast<const AM_MEDIA_TYPE*>(&mtOut), L"NegotiatedOutput");
    } else {
      OutputDebugStringW(L"[LR2BGAFilter] StartStreaming: failed to get negotiated output media type.\n");
    }
  }

  return CTransformFilter::StartStreaming();
}

//------------------------------------------------------------------------------
// StopStreaming - ストリーミング停止
//------------------------------------------------------------------------------
HRESULT CLR2BGAFilter::StopStreaming() {
  CAutoLock lock(&m_csReceive);

  // Memory Monitor Stop
  if (m_pMemoryMonitor) {
      m_pMemoryMonitor->Stop();
  }

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

  if (mtIn.formattype == FORMAT_VideoInfo2) {
    VIDEOINFOHEADER2 *pvi2 = (VIDEOINFOHEADER2 *)mtIn.Format();
    inWidth = pvi2->bmiHeader.biWidth;
    inHeight = abs(pvi2->bmiHeader.biHeight);
    avgTimePerFrame = pvi2->AvgTimePerFrame;
  } else {
    VIDEOINFOHEADER *pvi = (VIDEOINFOHEADER *)mtIn.Format();
    inWidth = pvi->bmiHeader.biWidth;
    inHeight = abs(pvi->bmiHeader.biHeight);
    avgTimePerFrame = pvi->AvgTimePerFrame;
  }

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
//   2.5 入出力フォーマットの決定
//      - StartStreaming で確定したキャッシュ値
//        (m_inputWidth/m_inputHeight/m_inputBitCount, m_activeWidth/m_activeHeight) を使用
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
//   - 設定値/フォーマット情報は、ストリーミング開始時にラッチされた値
//     (m_activeWidth/m_activeHeight, m_inputWidth/m_inputHeight/m_inputBitCount) を使用します。
//     これは、処理中に設定が変更されてバッファオーバーランが発生するのを防ぐためです。
//------------------------------------------------------------------------------
HRESULT CLR2BGAFilter::Transform(IMediaSample *pIn, IMediaSample *pOut) {
  m_inputFrameCount++;

  REFERENCE_TIME rtStart = 0, rtEnd = 0;
  pIn->GetTime(&rtStart, &rtEnd);

  // パフォーマンス計測
  LARGE_INTEGER startTime, endTime, freq = m_qpcFrequency;
  if (freq.QuadPart <= 0) {
    QueryPerformanceFrequency(&freq); // フェイルセーフ
  }
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

  // フォーマットは StartStreaming で確定したキャッシュ値を使用
  int srcWidth = m_inputWidth;
  int srcHeight = m_inputHeight;
  int srcBitCount = m_inputBitCount;
  if (srcWidth <= 0 || srcHeight <= 0 || srcBitCount <= 0) {
    return E_UNEXPECTED;
  }
  int srcStride = ((srcWidth * (srcBitCount / 8) + 3) & ~3);

  int dstWidth = m_activeWidth;
  int dstHeight = m_activeHeight;
  if (dstWidth <= 0 || dstHeight <= 0) {
    return E_UNEXPECTED;
  }
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

