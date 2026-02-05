//------------------------------------------------------------------------------
// LR2BGAFilter.h
// LR2 BGA Filter - 映像リサイズ/外部レンダリング用 DirectShow フィルタ
//------------------------------------------------------------------------------
#pragma once

// Disable strict string checks for legacy DirectShow code
#define NO_DSHOW_STRSAFE

#include <initguid.h>
#include <cguid.h>
#include <streams.h>
#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <atomic>

#include "LR2BGALetterboxDetector.h"
#include "LR2BGASettings.h"
#include "LR2BGATypes.h"
#include "LR2BGAWindow.h"

//------------------------------------------------------------------------------
// Filter GUID
// {7B8C9D0E-1F2A-4B3C-8D4E-5F6A7B8C9D0E}
//------------------------------------------------------------------------------
DEFINE_GUID(CLSID_LR2BGAFilter, 0x7b8c9d0e, 0x1f2a, 0x4b3c, 0x8d, 0x4e, 0x5f,
            0x6a, 0x7b, 0x8c, 0x9d, 0x0e);

//------------------------------------------------------------------------------
// Property Page GUID
// {7B8C9D0E-1F2A-4B3C-8D4E-5F6A7B8C9D0F}
//------------------------------------------------------------------------------
DEFINE_GUID(CLSID_LR2BGAFilterPropertyPage, 0x7b8c9d0e, 0x1f2a, 0x4b3c, 0x8d,
            0x4e, 0x5f, 0x6a, 0x7b, 0x8c, 0x9d, 0x0f);

//------------------------------------------------------------------------------
// Settings Interface GUID
// {7B8C9D0E-1F2A-4B3C-8D4E-5F6A7B8C9D10}
//------------------------------------------------------------------------------
DEFINE_GUID(IID_ILR2BGAFilterSettings, 0x7b8c9d0e, 0x1f2a, 0x4b3c, 0x8d, 0x4e,
            0x5f, 0x6a, 0x7b, 0x8c, 0x9d, 0x10);

//------------------------------------------------------------------------------
// Operation Modes
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// ILR2BGAFilterSettings インターフェース
// フィルタ設定のためのカスタムインターフェース
//------------------------------------------------------------------------------
DECLARE_INTERFACE_(ILR2BGAFilterSettings, IUnknown) {
  // LR2出力サイズ設定
  STDMETHOD(GetOutputSize)(THIS_ int *pWidth, int *pHeight) PURE;
  STDMETHOD(SetOutputSize)(THIS_ int width, int height) PURE;

  // LR2リサイズアルゴリズム設定
  STDMETHOD(GetResizeAlgorithm)(THIS_ ResizeAlgorithm * pAlgo) PURE;
  STDMETHOD(SetResizeAlgorithm)(THIS_ ResizeAlgorithm algo) PURE;

  // LR2アスペクト比維持設定
  STDMETHOD(GetKeepAspectRatio)(THIS_ BOOL * pKeep) PURE;
  STDMETHOD(SetKeepAspectRatio)(THIS_ BOOL keep) PURE;

  // デバッグモード設定
  STDMETHOD(GetDebugMode)(THIS_ BOOL * pDebug) PURE;
  STDMETHOD(SetDebugMode)(THIS_ BOOL debug) PURE;

  // 最大FPS制限 (有効/無効の切り替えとターゲットFPS設定)
  STDMETHOD(GetMaxFPS)(THIS_ int *pMaxFPS) PURE;
  STDMETHOD(SetMaxFPS)(THIS_ int maxFPS) PURE;
  STDMETHOD(GetLimitFPS)(THIS_ BOOL *pEnabled) PURE;
  STDMETHOD(SetLimitFPS)(THIS_ BOOL enabled) PURE;

  // LR2ダミーモード (1x1の黒画像を一度だけ送信し、以降の処理をスキップ)
  STDMETHOD(GetDummyMode)(THIS_ BOOL * pDummy) PURE;
  STDMETHOD(SetDummyMode)(THIS_ BOOL dummy) PURE;

  // LR2パススルーモード (リサイズなし、色変換のみ)
  STDMETHOD(GetPassthroughMode)(THIS_ BOOL * pPassthrough) PURE;
  STDMETHOD(SetPassthroughMode)(THIS_ BOOL passthrough) PURE;

  // 外部ウィンドウ有効化
  STDMETHOD(GetExternalWindowEnabled)(THIS_ BOOL * pEnabled) PURE;
  STDMETHOD(SetExternalWindowEnabled)(THIS_ BOOL enabled) PURE;

  // 外部ウィンドウ位置
  STDMETHOD(GetExternalWindowPosition)(THIS_ int *pX, int *pY) PURE;
  STDMETHOD(SetExternalWindowPosition)(THIS_ int x, int y) PURE;

  // 外部ウィンドウサイズ
  STDMETHOD(GetExternalWindowSize)(THIS_ int *pWidth, int *pHeight) PURE;
  STDMETHOD(SetExternalWindowSize)(THIS_ int width, int height) PURE;

  // 外部ウィンドウリサイズアルゴリズム
  STDMETHOD(GetExternalWindowAlgorithm)(THIS_ ResizeAlgorithm * pAlgo) PURE;
  STDMETHOD(SetExternalWindowAlgorithm)(THIS_ ResizeAlgorithm algo) PURE;

  // 外部ウィンドウアスペクト比維持
  STDMETHOD(GetExternalWindowKeepAspect)(THIS_ BOOL * pKeep) PURE;
  STDMETHOD(SetExternalWindowKeepAspect)(THIS_ BOOL keep) PURE;

  // 外部ウィンドウパススルーモード
  STDMETHOD(GetExternalWindowPassthrough)(THIS_ BOOL * pPassthrough) PURE;
  STDMETHOD(SetExternalWindowPassthrough)(THIS_ BOOL passthrough) PURE;

  // 外部ウィンドウ最前面表示 (Topmost)
  STDMETHOD(GetExternalWindowTopmost)(THIS_ BOOL * pTopmost) PURE;
  STDMETHOD(SetExternalWindowTopmost)(THIS_ BOOL topmost) PURE;

  // 設定モード (外部ウィンドウ表示などを抑制)
  STDMETHOD(SetConfigurationMode)(THIS_ BOOL bConfigMode) PURE;

  // 明るさ制御 (0-100)
  STDMETHOD(GetBrightnessLR2)(THIS_ int *pBrightness) PURE;
  STDMETHOD(SetBrightnessLR2)(THIS_ int brightness) PURE;
  STDMETHOD(GetBrightnessExt)(THIS_ int *pBrightness) PURE;
  STDMETHOD(SetBrightnessExt)(THIS_ int brightness) PURE;

  // プロパティページの自動オープン設定
  STDMETHOD(GetAutoOpenSettings)(THIS_ BOOL * pAutoOpen) PURE;
  STDMETHOD(SetAutoOpenSettings)(THIS_ BOOL autoOpen) PURE;

  // 手動クローズ設定 (Manual Close Settings)
  STDMETHOD(GetCloseOnRightClick)(THIS_ BOOL * pClose) PURE;
  STDMETHOD(SetCloseOnRightClick)(THIS_ BOOL close) PURE;

  STDMETHOD(GetGamepadCloseEnabled)(THIS_ BOOL * pEnabled) PURE;
  STDMETHOD(SetGamepadCloseEnabled)(THIS_ BOOL enabled) PURE;

  STDMETHOD(GetGamepadID)(THIS_ int *pID) PURE;
  STDMETHOD(SetGamepadID)(THIS_ int id) PURE;

  STDMETHOD(GetGamepadButtonID)(THIS_ int *pBtnID) PURE;
  STDMETHOD(SetGamepadButtonID)(THIS_ int btnID) PURE;

  STDMETHOD(GetKeyboardCloseEnabled)(THIS_ BOOL * pEnabled) PURE;
  STDMETHOD(SetKeyboardCloseEnabled)(THIS_ BOOL enabled) PURE;

  STDMETHOD(GetKeyboardKeyCode)(THIS_ int *pKeyCode) PURE;
  STDMETHOD(SetKeyboardKeyCode)(THIS_ int keyCode) PURE;

  // 自動レターボックス除去 (Auto Remove Letterbox)
  STDMETHOD(GetAutoRemoveLetterbox)(THIS_ BOOL * pEnabled) PURE;
  STDMETHOD(SetAutoRemoveLetterbox)(THIS_ BOOL enabled) PURE;

  STDMETHOD(GetLetterboxThreshold)(THIS_ int *pThreshold) PURE;
  STDMETHOD(SetLetterboxThreshold)(THIS_ int threshold) PURE;

  STDMETHOD(GetLetterboxStability)(THIS_ int *pStability) PURE;
  STDMETHOD(SetLetterboxStability)(THIS_ int stability) PURE;

  // 統計情報のリセット (Reset Statistics)
  STDMETHOD(ResetPerformanceStatistics)(THIS) PURE;
};

//------------------------------------------------------------------------------
// CLR2BGAInputPin クラス
// 再帰接続（自分自身への接続）を防ぐためのカスタム入力ピン
//------------------------------------------------------------------------------
class CLR2BGAInputPin : public CTransformInputPin {
public:
  CLR2BGAInputPin(TCHAR *pObjectName, CTransformFilter *pTransformFilter,
                  HRESULT *phr, LPCWSTR pName)
      : CTransformInputPin(pObjectName, pTransformFilter, phr, pName) {}

  // CheckConnectをオーバーライドして上流フィルタを検証
  HRESULT CheckConnect(IPin *pPin) override;
};

//------------------------------------------------------------------------------
// CLR2BGAFilter クラス
// CTransformFilterを継承したビデオ変換フィルタ
//------------------------------------------------------------------------------

class CLR2BGAFilter : public CTransformFilter,
                      public ISpecifyPropertyPages,
                      public ILR2BGAFilterSettings {
public:
  // ファクトリメソッド
  static CUnknown *WINAPI CreateInstance(LPUNKNOWN pUnk, HRESULT *phr);

  // デストラクタ
  virtual ~CLR2BGAFilter();

  // IUnknown
  DECLARE_IUNKNOWN;
  STDMETHOD(NonDelegatingQueryInterface)(REFIID riid, void **ppv) override;

  //--------------------------------------------------------------------------
  // ISpecifyPropertyPages
  //--------------------------------------------------------------------------
  STDMETHOD(GetPages)(CAUUID *pPages) override;

  //--------------------------------------------------------------------------
  // ILR2BGAFilterSettings
  //--------------------------------------------------------------------------
  STDMETHOD(GetOutputSize)(int *pWidth, int *pHeight) override;
  STDMETHOD(SetOutputSize)(int width, int height) override;
  STDMETHOD(GetResizeAlgorithm)(ResizeAlgorithm *pAlgo) override;
  STDMETHOD(SetResizeAlgorithm)(ResizeAlgorithm algo) override;
  STDMETHOD(GetKeepAspectRatio)(BOOL *pKeep) override;
  STDMETHOD(SetKeepAspectRatio)(BOOL keep) override;
  STDMETHOD(GetDebugMode)(BOOL *pDebug) override;
  STDMETHOD(SetDebugMode)(BOOL debug) override;
  STDMETHOD(GetMaxFPS)(int *pMaxFPS) override;
  STDMETHOD(SetMaxFPS)(int maxFPS) override;
  STDMETHOD(GetLimitFPS)(BOOL *pEnabled) override;
  STDMETHOD(SetLimitFPS)(BOOL enabled) override;
  STDMETHOD(GetDummyMode)(BOOL *pDummy) override;
  STDMETHOD(SetDummyMode)(BOOL dummy) override;
  STDMETHOD(GetPassthroughMode)(BOOL *pPassthrough) override;
  STDMETHOD(SetPassthroughMode)(BOOL passthrough) override;
  STDMETHOD(GetExternalWindowEnabled)(BOOL *pEnabled) override;
  STDMETHOD(SetExternalWindowEnabled)(BOOL enabled) override;
  STDMETHOD(GetExternalWindowPosition)(int *pX, int *pY) override;
  STDMETHOD(SetExternalWindowPosition)(int x, int y) override;
  STDMETHOD(GetExternalWindowSize)(int *pWidth, int *pHeight) override;
  STDMETHOD(SetExternalWindowSize)(int width, int height) override;
  STDMETHOD(GetExternalWindowAlgorithm)(ResizeAlgorithm *pAlgo) override;
  STDMETHOD(SetExternalWindowAlgorithm)(ResizeAlgorithm algo) override;
  STDMETHOD(GetExternalWindowKeepAspect)(BOOL *pKeep) override;
  STDMETHOD(SetExternalWindowKeepAspect)(BOOL keep) override;
  STDMETHOD(GetExternalWindowPassthrough)(BOOL *pPassthrough) override;
  STDMETHOD(SetExternalWindowPassthrough)(BOOL passthrough) override;
  STDMETHOD(GetExternalWindowTopmost)(BOOL *pTopmost) override;
  STDMETHOD(SetExternalWindowTopmost)(BOOL topmost) override;

  STDMETHOD(SetConfigurationMode)(BOOL bConfigMode) override;

  STDMETHOD(GetBrightnessLR2)(int *pBrightness) override;
  STDMETHOD(SetBrightnessLR2)(int brightness) override;
  STDMETHOD(GetBrightnessExt)(int *pBrightness) override;
  STDMETHOD(SetBrightnessExt)(int brightness) override;
  STDMETHOD(GetAutoOpenSettings)(BOOL *pAutoOpen) override;
  STDMETHOD(SetAutoOpenSettings)(BOOL autoOpen) override;

  STDMETHOD(GetCloseOnRightClick)(BOOL *pClose) override;
  STDMETHOD(SetCloseOnRightClick)(BOOL close) override;

  STDMETHOD(GetGamepadCloseEnabled)(BOOL *pEnabled) override;
  STDMETHOD(SetGamepadCloseEnabled)(BOOL enabled) override;

  STDMETHOD(GetGamepadID)(int *pID) override;
  STDMETHOD(SetGamepadID)(int id) override;

  STDMETHOD(GetGamepadButtonID)(int *pBtnID) override;
  STDMETHOD(SetGamepadButtonID)(int btnID) override;

  STDMETHOD(GetKeyboardCloseEnabled)(BOOL *pEnabled) override;
  STDMETHOD(SetKeyboardCloseEnabled)(BOOL enabled) override;
  STDMETHOD(GetKeyboardKeyCode)(int *pKeyCode) override;
  STDMETHOD(SetKeyboardKeyCode)(int keyCode) override;

  STDMETHOD(GetAutoRemoveLetterbox)(BOOL *pAutoRemove) override;
  STDMETHOD(SetAutoRemoveLetterbox)(BOOL autoRemove) override;

  STDMETHOD(GetLetterboxThreshold)(int *pThreshold) override;
  STDMETHOD(SetLetterboxThreshold)(int threshold) override;
  STDMETHOD(GetLetterboxStability)(int *pStability) override;
  STDMETHOD(SetLetterboxStability)(int stability) override;
  STDMETHOD(ResetPerformanceStatistics)() override;

  LetterboxMode SafeAnalyzeFrame(BYTE *pBuffer, long width, long height,
                                 long stride, int bpp);

  //--------------------------------------------------------------------------
  // CTransformFilter Overrides
  //--------------------------------------------------------------------------

  // GetPinをオーバーライドして、カスタムピン名("In"/"Out")を使用 (ffdshow互換)
  CBasePin *GetPin(int n) override;

  // 入力メディアタイプのチェック
  HRESULT CheckInputType(const CMediaType *mtIn) override;

  // 入力・出力メディアタイプの組み合わせチェック
  HRESULT CheckTransform(const CMediaType *mtIn,
                         const CMediaType *mtOut) override;

  // 出力バッファサイズの決定
  HRESULT DecideBufferSize(IMemAllocator *pAlloc,
                           ALLOCATOR_PROPERTIES *pProp) override;

  // 出力メディアタイプの取得
  HRESULT GetMediaType(int iPosition, CMediaType *pMediaType) override;

  // フレーム変換処理 (Transform)
  HRESULT Transform(IMediaSample *pIn, IMediaSample *pOut) override;

  // ストリーミング開始
  HRESULT StartStreaming() override;

  // ストリーミング停止
  HRESULT StopStreaming() override;

  // ストリーム終了処理
  HRESULT EndOfStream() override;

  // LR2ウィンドウへのフォーカス復帰
  void FocusLR2Window();

private:
  // ヘルパー: フィルタグラフ情報の取得
  std::wstring GetFilterGraphInfo();

  // プライベートコンストラクタ
  CLR2BGAFilter(LPUNKNOWN pUnk, HRESULT *phr);

public:
  // ヘルパー: プロパティページ向けに設定オブジェクトを直接公開
  // (通常はインターフェース経由で管理されますが、内部アクセス用)
  LR2BGASettings *GetSettings() { return m_pSettings; }

private:
  // デバッグ情報の更新
  void UpdateDebugInfo();

  // Transform Helpers
  void ProcessLetterboxDetection(const BYTE* pSrcData, long actualDataLength, int srcWidth, int srcHeight, int srcStride, int srcBitCount, RECT& srcRect, RECT*& pSrcRect);
  HRESULT WaitFPSLimit(REFERENCE_TIME rtStart, REFERENCE_TIME rtEnd);
  HRESULT FillOutputBuffer(const BYTE* pSrcData, BYTE* pDstData, int srcWidth, int srcHeight, int srcStride, int srcBitCount,
                           int dstWidth, int dstHeight, int dstStride, const RECT* pSrcRect,
                           REFERENCE_TIME& rtStart, REFERENCE_TIME& rtEnd, IMediaSample* pOut);

  //--------------------------------------------------------------------------
  // メンバ変数
  //--------------------------------------------------------------------------
public:
  LR2BGASettings *m_pSettings; // 設定マネージャ
  LR2BGAWindow *m_pWindow;     // ウィンドウマネージャ

  // レターボックス検出 (Letterbox Detection)
  LR2BGALetterboxDetector m_lbDetector;
  std::mutex m_mtxLBMode;
  std::mutex m_mtxLBBuffer;
  LetterboxMode m_currentLBMode;

  // 非同期検出スレッド (Async Detection Thread)
  // C++11 std::thread を使用して実行
  std::thread m_threadLB;
  std::condition_variable m_cvLB;
  std::mutex m_mtxLBControl;
  bool m_bLBExit;
  bool m_bLBRequest;

  // 検出用フレームバッファ (コピー)
  std::vector<BYTE> m_lbBuffer;
  LONG m_lbWidth;
  LONG m_lbHeight;
  LONG m_lbStride;
  int m_lbBpp;
  bool m_lbRequestPending; // 処理中の場合にフレームを破棄するためのフラグ
  DWORD m_lastLBRequestTime;

  void LetterboxThread();
  // SEH (構造化例外処理) 用ヘルパー
  LetterboxMode SafeAnalyzeFrame(BYTE *pBuffer, size_t bufferSize, long width,
                                 long height, long stride, int bpp);

  FilterMode m_mode; // 動作モード

  // 入力フォーマット情報 (キャッシュ)
  int m_inputWidth;
  int m_inputHeight;
  int m_inputBitCount;

  REFERENCE_TIME m_lastOutputTime; // 最終出力フレーム時刻
  LONGLONG m_droppedFrames;        // FPS制限によりドロップされたフレーム数
  long m_exceptionCount;           // 黒帯検出例外発生回数

  // ダミーモード状態
  bool m_dummySent;               // ダミーフレーム送信済みフラグ
  REFERENCE_TIME m_lastDummyTime; // 最終ダミーフレーム送信時刻

  // 統計情報
  LONGLONG m_frameCount;       // 処理済みフレーム数 (出力)
  LONGLONG m_processedFrameCount; // 処理計測対象フレーム数 (スキップ含む)
  LONGLONG m_inputFrameCount;  // 受信フレーム数 (入力)
  LONGLONG m_totalProcessTime; // 総処理時間 (100ns単位)
  double m_avgProcessTime;     // 平均処理時間 (ms)
  double m_frameRate;          // 入力フレームレート
  double m_outputFrameRate;    // 実測出力フレームレート

  // 設定モード
  BOOL m_bConfigMode;

  // フィルタグラフ情報 (デバッグ表示用)
  std::wstring m_inputFilterName;
  std::wstring m_outputFilterName;
  std::wstring m_filterGraphInfo;

  // 設定値のラッチ (クラッシュ防止のためストリーミング開始時に固定)
  bool m_activePassthrough;
  bool m_activeDummy;
  int m_activeWidth;
  int m_activeHeight;

  // リサイズ用LUTバッファ (メモリ再確保抑制)
  std::vector<int> m_lutXIndices;
  std::vector<short> m_lutXWeights;
};

