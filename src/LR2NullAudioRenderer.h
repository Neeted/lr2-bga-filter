//------------------------------------------------------------------------------
// LR2NullAudioRenderer.h
// LR2 BGA Null Audio Renderer - 音声ストリームを即座に破棄するNull Renderer
//------------------------------------------------------------------------------
#pragma once

#define NO_DSHOW_STRSAFE

#include <streams.h>

//------------------------------------------------------------------------------
// Filter GUID
// {64878B0F-CC73-484F-9B7B-47520B40C8F0}
//------------------------------------------------------------------------------
DEFINE_GUID(CLSID_LR2NullAudioRenderer, 0x64878b0f, 0xcc73, 0x484f, 0x9b, 0x7b, 0x47,
            0x52, 0x0b, 0x40, 0xc8, 0xf0);

//------------------------------------------------------------------------------
// CLR2NullAudioRenderer クラス
// CBaseRenderer を継承した音声専用 Null Renderer
//
// 設計意図:
//   - BGAファイルに含まれる問題のある音声トラック（低ビットレートVBR音声）が
//     原因で発生するカクつきを防止する
//   - 標準Null Rendererと異なり、プレゼンテーション時刻を待機しない
//   - サンプルを即座に破棄することでクロック遅延を回避
//------------------------------------------------------------------------------
class CLR2NullAudioRenderer : public CBaseRenderer {
public:
  // ファクトリメソッド
  static CUnknown *WINAPI CreateInstance(LPUNKNOWN pUnk, HRESULT *phr);

  // デストラクタ
  virtual ~CLR2NullAudioRenderer();

  //--------------------------------------------------------------------------
  // CBaseRenderer オーバーライド
  //--------------------------------------------------------------------------

  // メディアタイプのチェック - MEDIATYPE_Audio のみ受け入れ
  HRESULT CheckMediaType(const CMediaType *pmt) override;

  // サンプルのレンダリング - 即座に破棄（何もしない）
  HRESULT DoRenderSample(IMediaSample *pMediaSample) override;

  // サンプル受信時の待機を無効化
  // OnReceiveFirstSample は CBaseRenderer のデフォルト実装を使用

  // 参照クロックを使用しない設定（オプション）
  // これにより、サンプルのプレゼンテーション時刻待ちを完全にスキップ
  HRESULT ShouldDrawSampleNow(IMediaSample *pMediaSample,
                              REFERENCE_TIME *pStartTime,
                              REFERENCE_TIME *pEndTime) override;

private:
  // プライベートコンストラクタ
  CLR2NullAudioRenderer(LPUNKNOWN pUnk, HRESULT *phr);
};

