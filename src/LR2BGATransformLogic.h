//------------------------------------------------------------------------------
// LR2BGATransformLogic.h
// LR2 BGA Filter - 映像変換ロジック
//------------------------------------------------------------------------------
//
// 概要:
//   CLR2BGAFilter から抽出された映像処理ロジックを担当するクラスです。
//   DirectShow 固有の処理から分離し、テスト可能な形で設計しています。
//
// 責務:
//   1. レターボックス検出: 黒帯解析と切り出し範囲の決定
//   2. FPS制限: フレームレート上限の管理
//   3. フレーム変換: リサイズ、フォーマット変換、明るさ調整
//
// 注意:
//   このクラスは DirectShow インターフェースに依存しません。
//------------------------------------------------------------------------------
#pragma once

#include <windows.h>
#include <vector>
#include <mutex>
#include <thread>
#include <condition_variable>

#include "LR2BGALetterboxDetector.h"
#include "LR2BGASettings.h"
#include "LR2BGATypes.h"

// 前方宣言
class LR2BGAWindow;

//------------------------------------------------------------------------------
// 定数定義 (Constants)
//------------------------------------------------------------------------------
constexpr DWORD kTransformLetterboxCheckIntervalMs = 200;  // 黒帯検出の実行間隔
constexpr DWORD kTransformMaxSleepMs = 1000;               // FPS制限時の最大スリープ時間

//------------------------------------------------------------------------------
// LR2BGATransformLogic クラス
//------------------------------------------------------------------------------
class LR2BGATransformLogic {
public:
    LR2BGATransformLogic(LR2BGASettings* pSettings, LR2BGAWindow* pWindow);
    ~LR2BGATransformLogic();

    //--------------------------------------------------------------------------
    // 初期化・終了
    //--------------------------------------------------------------------------
    // ストリーミング開始時に呼び出す (設定のラッチ)
    void StartStreaming(int inputWidth, int inputHeight, int inputBitCount,
                        int outputWidth, int outputHeight);
    // ストリーミング終了時に呼び出す
    void StopStreaming();

    //--------------------------------------------------------------------------
    // レターボックス検出
    //--------------------------------------------------------------------------
    // 検出スレッドを開始
    void StartLetterboxThread();
    // 検出スレッドを停止
    void StopLetterboxThread();
    // レターボックス状態をリセット
    void ResetLetterboxState();
    // ダミー送信フラグをリセット
    void ResetDummySent() { m_dummySent = false; }

    // フレームを解析してソース矩形を調整
    void ProcessLetterboxDetection(const BYTE* pSrcData, long actualDataLength,
                                   int srcWidth, int srcHeight, int srcStride, int srcBitCount,
                                   RECT& srcRect, RECT*& pSrcRect);
    // 現在の検出モードを取得
    LetterboxMode GetCurrentLetterboxMode() const;
    // 検出器への直接アクセス (デバッグ情報取得用)
    LR2BGALetterboxDetector& GetDetector() { return m_lbDetector; }

    //--------------------------------------------------------------------------
    // FPS制限
    //--------------------------------------------------------------------------
    // フレームをスキップすべきか判定 (S_OK=続行, S_FALSE=スキップ)
    HRESULT WaitFPSLimit(REFERENCE_TIME rtStart, REFERENCE_TIME rtEnd);

    //--------------------------------------------------------------------------
    // フレーム変換
    //--------------------------------------------------------------------------
    // 入力バッファを変換して出力バッファへ書き込み
    // 戻り値: S_OK=成功, S_FALSE=スキップ
    HRESULT FillOutputBuffer(const BYTE* pSrcData, BYTE* pDstData,
                             int srcWidth, int srcHeight, int srcStride, int srcBitCount,
                             int dstWidth, int dstHeight, int dstStride, const RECT* pSrcRect,
                             REFERENCE_TIME& rtStart, REFERENCE_TIME& rtEnd,
                             long& outActualDataLength);

    //--------------------------------------------------------------------------
    // 統計情報
    //--------------------------------------------------------------------------
    LONGLONG GetDroppedFrames() const { return m_droppedFrames; }
    void ResetStatistics();

private:
    // レターボックス検出スレッド本体
    void LetterboxThreadProc();

    //--------------------------------------------------------------------------
    // メンバ変数
    //--------------------------------------------------------------------------
    LR2BGASettings* m_pSettings;
    LR2BGAWindow* m_pWindow;

    // レターボックス検出
    LR2BGALetterboxDetector m_lbDetector;
    std::mutex m_mtxLBMode;
    std::mutex m_mtxLBBuffer;
    LetterboxMode m_currentLBMode;

    // 非同期検出スレッド
    std::thread m_threadLB;
    std::condition_variable m_cvLB;
    std::mutex m_mtxLBControl;
    bool m_bLBExit;
    bool m_bLBRequest;

    // 検出用フレームバッファ
    std::vector<BYTE> m_lbBuffer;
    LONG m_lbWidth;
    LONG m_lbHeight;
    LONG m_lbStride;
    int m_lbBpp;
    DWORD m_lastLBRequestTime;

    // FPS制限
    REFERENCE_TIME m_lastOutputTime;
    LONGLONG m_droppedFrames;

    // ダミーモード状態
    bool m_dummySent;
    REFERENCE_TIME m_lastDummyTime;

    // 設定のラッチ (ストリーミング中は固定)
    bool m_activePassthrough;
    bool m_activeDummy;
    int m_activeWidth;
    int m_activeHeight;

    // リサイズ用LUTバッファ
    std::vector<int> m_lutXIndices;
    std::vector<short> m_lutXWeights;
};
