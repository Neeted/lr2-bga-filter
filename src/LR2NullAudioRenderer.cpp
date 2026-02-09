//------------------------------------------------------------------------------
// LR2NullAudioRenderer.cpp
// LR2 BGA Null Audio Renderer - 音声ストリームを即座に破棄するNull Renderer
//------------------------------------------------------------------------------

#define NO_DSHOW_STRSAFE

#include "LR2NullAudioRenderer.h"

//------------------------------------------------------------------------------
// フィルタ情報 (Filter Information)
//------------------------------------------------------------------------------

// 入力メディアタイプ: MEDIATYPE_Audio, MEDIASUBTYPE_NULL (全サブタイプ受け入れ)
static const AMOVIESETUP_MEDIATYPE sudInputTypes[] = {
    {&MEDIATYPE_Audio, &MEDIASUBTYPE_NULL}};

// 入力ピン定義
static const AMOVIESETUP_PIN sudPins[] = {
    {const_cast<LPWSTR>(L"Input"), // ピン名
     FALSE,                        // レンダリングされる
     FALSE,                        // 出力ピンではない
     FALSE,                        // ゼロインスタンス許可しない
     FALSE,                        // 複数インスタンス許可しない
     &CLSID_NULL,                  // 接続先フィルタ
     NULL,                         // 接続先ピン名
     1,                            // メディアタイプ数
     sudInputTypes}                // メディアタイプ配列
};

// フィルタ定義
AMOVIESETUP_FILTER sudNullAudioFilter = {
    &CLSID_LR2NullAudioRenderer,    // CLSID
    L"LR2 BGA Null Audio Renderer", // フィルタ名
    0xfff00000,                     // Merit (最高優先度)
    1,                              // ピン数
    sudPins                         // ピン配列
};

//------------------------------------------------------------------------------
// CLR2NullAudioRenderer 実装
//------------------------------------------------------------------------------

CLR2NullAudioRenderer::CLR2NullAudioRenderer(LPUNKNOWN pUnk, HRESULT *phr)
    : CBaseRenderer(CLSID_LR2NullAudioRenderer,
                    NAME("LR2 BGA Null Audio Renderer"), pUnk, phr) {
  // 初期化は親クラスに委譲
}

CLR2NullAudioRenderer::~CLR2NullAudioRenderer() {
  // クリーンアップは親クラスに委譲
}

//------------------------------------------------------------------------------
// CreateInstance - ファクトリメソッド
//------------------------------------------------------------------------------
CUnknown *WINAPI CLR2NullAudioRenderer::CreateInstance(LPUNKNOWN pUnk,
                                                       HRESULT *phr) {
  CLR2NullAudioRenderer *pRenderer = new CLR2NullAudioRenderer(pUnk, phr);
  if (pRenderer == NULL && phr) {
    *phr = E_OUTOFMEMORY;
  }
  return pRenderer;
}

//------------------------------------------------------------------------------
// CheckMediaType - メディアタイプのチェック
// MEDIATYPE_Audio であれば全て受け入れる
//------------------------------------------------------------------------------
HRESULT CLR2NullAudioRenderer::CheckMediaType(const CMediaType *pmt) {
  CheckPointer(pmt, E_POINTER);

  // MEDIATYPE_Audio のみ受け入れ
  if (pmt->majortype != MEDIATYPE_Audio) {
    return E_FAIL;
  }

  // サブタイプは問わない（AAC, MP3, PCM 等すべて受け入れ）
  return S_OK;
}

//------------------------------------------------------------------------------
// DoRenderSample - サンプルのレンダリング
// 即座に破棄（何もしない）
//------------------------------------------------------------------------------
HRESULT CLR2NullAudioRenderer::DoRenderSample(IMediaSample *pMediaSample) {
  // サンプルを無視して即座に成功を返す
  // これにより音声データは破棄され、クロック遅延も発生しない
  UNREFERENCED_PARAMETER(pMediaSample);
  return S_OK;
}

//------------------------------------------------------------------------------
// ShouldDrawSampleNow - サンプルの描画タイミング判定
// 常に S_OK を返すことで、プレゼンテーション時刻を待たずに即座に処理
//------------------------------------------------------------------------------
HRESULT
CLR2NullAudioRenderer::ShouldDrawSampleNow(IMediaSample *pMediaSample,
                                           REFERENCE_TIME *pStartTime,
                                           REFERENCE_TIME *pEndTime) {
  UNREFERENCED_PARAMETER(pMediaSample);
  UNREFERENCED_PARAMETER(pStartTime);
  UNREFERENCED_PARAMETER(pEndTime);

  // S_OK: 即座に描画（=破棄）する
  // S_FALSE: プレゼンテーション時刻まで待機する（標準動作）
  // これにより、音声レンダラーによるクロック遅延を完全に回避
  return S_OK;
}
