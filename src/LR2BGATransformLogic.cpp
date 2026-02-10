//------------------------------------------------------------------------------
// LR2BGATransformLogic.cpp
// LR2 BGA Filter - 映像変換ロジック 実装
//------------------------------------------------------------------------------

#include "LR2BGATransformLogic.h"
#include "LR2BGAImageProc.h"
#include "LR2BGAWindow.h" // m_pWindow のために必要 (将来的な利用含む)

//------------------------------------------------------------------------------
// コンストラクタ / デストラクタ
//------------------------------------------------------------------------------
LR2BGATransformLogic::LR2BGATransformLogic(LR2BGASettings* pSettings, LR2BGAWindow* pWindow)
    : m_pSettings(pSettings),
      m_pWindow(pWindow),
      m_currentLBMode(LB_MODE_ORIGINAL),
      m_bLBExit(false),
      m_bLBRequest(false),
      m_lbWidth(0),
      m_lbHeight(0),
      m_lbStride(0),
      m_lbBpp(0),
      m_lastLBRequestTime(0),
      m_lastOutputTime(0),
      m_droppedFrames(0),
      m_dummySent(false),
      m_lastDummyTime(0),
      m_activePassthrough(false),
      m_activeDummy(false),
      m_activeWidth(0),
      m_activeHeight(0)
{
}

LR2BGATransformLogic::~LR2BGATransformLogic() {
    StopStreaming();
    StopLetterboxThread();
}

//------------------------------------------------------------------------------
// 初期化・終了
//------------------------------------------------------------------------------
void LR2BGATransformLogic::StartStreaming(int inputWidth, int inputHeight, int inputBitCount,
                                          int outputWidth, int outputHeight) {
    // 設定のラッチ
    // ストリーミング中に設定が変更されても、バッファオーバーランなどを防ぐために
    // この時点での値を維持する
    m_activeWidth = outputWidth;
    m_activeHeight = outputHeight;

    // モード判定
    // 入出力サイズが一致し、かつアスペクト比維持不要ならパススルー
    bool isSizeSame = (inputWidth == outputWidth) && (inputHeight == outputHeight);
    // 32bit->24bit変換が必要な場合でもパススルー扱い（単純コピーで済む）
    
    // パススルー条件:
    //   1. 入出力サイズが完全一致
    //   2. アスペクト比維持がOFF（ONだと黒帯除去時に余白計算が発生するため）
    m_activePassthrough = isSizeSame && !m_pSettings->m_keepAspectRatio;

    // ダミーモード
    m_activeDummy = (inputWidth == 0 || inputHeight == 0); // 入力がない場合など

    // 状態リセット
    m_lastOutputTime = 0;
    m_droppedFrames = 0;
    m_dummySent = false;
    m_lastDummyTime = 0;

    // レターボックス関連
    m_lastLBRequestTime = 0;
    {
        std::lock_guard<std::mutex> lock(m_mtxLBMode);
        m_currentLBMode = LB_MODE_ORIGINAL;
    }

    // リサイズ用LUTの準備 (必要であれば再構築)
    // 実際には ImageProc 側で都度計算またはキャッシュされるが、
    // ここでメンバ変数として持っている vector をクリアしておくなど
    m_lutXIndices.clear();
    m_lutXWeights.clear();
}

void LR2BGATransformLogic::StopStreaming() {
    // 特になし
}

// ------------------------------------------------------------------------------
// レターボックス検出スレッド (Letterbox Thread)
//
// 役割:
//   メインの変換スレッドとは独立して、画像フレームの解析（黒帯検出）を行います。
//   画像解析は重い処理になる可能性があるため、分離することで再生のスムーズさを維持します。
//
// アーキテクチャ:
//   - イベント駆動型 (Event-Driven): m_bLBRequest フラグと Condition Variable
//   がシグナルされるまで待機します。
// ------------------------------------------------------------------------------
void LR2BGATransformLogic::StartLetterboxThread() {
    std::lock_guard<std::mutex> lock(m_mtxLBControl);
    if (m_threadLB.joinable()) {
        return; // 既に起動中
    }
    m_bLBExit = false;
    m_bLBRequest = false;
    m_threadLB = std::thread(&LR2BGATransformLogic::LetterboxThreadProc, this);
}

void LR2BGATransformLogic::StopLetterboxThread() {
    {
        std::lock_guard<std::mutex> lock(m_mtxLBControl);
        if (!m_threadLB.joinable()) return;
        m_bLBExit = true;
        m_cvLB.notify_one();
    }
    m_threadLB.join();
}

void LR2BGATransformLogic::ResetLetterboxState() {
    m_lbDetector.Reset();
    {
        std::lock_guard<std::mutex> lock(m_mtxLBMode);
        m_currentLBMode = LB_MODE_ORIGINAL;
    }
}

// ------------------------------------------------------------------------------
// LetterboxThread - 非同期解析スレッド
// ------------------------------------------------------------------------------
void LR2BGATransformLogic::LetterboxThreadProc() {
    std::vector<BYTE> workBuffer;

    while (true) {
        // 待機
        {
            std::unique_lock<std::mutex> lock(m_mtxLBControl);
            m_cvLB.wait(lock, [this] { return m_bLBRequest || m_bLBExit; });

            if (m_bLBExit) break;
            m_bLBRequest = false;
        }

        // パラメータ取得
        int w = 0, h = 0, s = 0, bpp = 0;
        size_t srcSize = 0;
        {
            std::lock_guard<std::mutex> lock(m_mtxLBBuffer);
            w = m_lbWidth;
            h = m_lbHeight;
            s = m_lbStride;
            bpp = m_lbBpp;
            srcSize = m_lbBuffer.size();
        }

        if (w <= 0 || h <= 0 || s <= 0 || srcSize == 0) continue;

        // 作業用バッファ準備
        try {
            if (workBuffer.size() < srcSize) {
                workBuffer.resize(srcSize);
            }
        } catch (...) {
            continue;
        }

        // コピーサイズ計算
        int calcSize = s * h;
        int copySize = (calcSize > (int)srcSize) ? (int)srcSize : calcSize;

        if (copySize > 0) {
            std::lock_guard<std::mutex> lock(m_mtxLBBuffer);
            if (m_lbBuffer.size() >= (size_t)copySize) {
                CopyMemory(workBuffer.data(), m_lbBuffer.data(), copySize);
            }
        }

        if (copySize <= 0) continue;
        
        // 解析実行 (SEHなし、直接呼び出し)
        LetterboxMode mode = m_lbDetector.AnalyzeFrame(workBuffer.data(), (size_t)calcSize, w, h, s, bpp);

        {
            std::lock_guard<std::mutex> lock(m_mtxLBMode);
            m_currentLBMode = mode;
        }
    }
}

// ------------------------------------------------------------------------------
// Helper: ProcessLetterboxDetection - 黒帯検出ロジックの実装
//
// 役割:
//   入力フレームの一部（または全体）を解析用バッファにコピーし、別スレッドでの解析を依頼します。
//   また、現在の検出状態（m_currentLBMode）に基づいて、切り出し範囲（Source Rect）を調整します。
//
// 処理の詳細:
//   1. 頻度制御: 負荷軽減のため、約200msごとに1回のみ解析をリクエストします。
//   2. バッファコピー: ワーカースレッドが安全に参照できるようにデータをコピーします。
//   3. モード反映: 検出されたレターボックスモード（16:9, 4:3等）に従い、srcRectの上下を削ります。
//
// 引数:
//   pSrcData         : 入力画像のデータポインタ
//   actualDataLength : 実際のデータ長 (安全性チェック用)
//   srcWidth/Height  : 入力画像の幅・高さ
//   srcStride        : ストライド (負の値の可能性あり)
//   srcBitCount      : ビット深度
//   srcRect          : 修正される矩形構造体 (参照)
//   pSrcRect         : 修正された場合にセットされるポインタ (参照)
// ------------------------------------------------------------------------------
void LR2BGATransformLogic::ProcessLetterboxDetection(const BYTE* pSrcData, long actualDataLength,
                                                     int srcWidth, int srcHeight, int srcStride, int srcBitCount,
                                                     RECT& srcRect, RECT*& pSrcRect) {
    if (!m_pSettings->m_autoRemoveLetterbox) {
        return;
    }

    // 頻度制限
    DWORD now = GetTickCount();
    if (now - m_lastLBRequestTime < kTransformLetterboxCheckIntervalMs) {
        // Skip
    } else {
        m_lastLBRequestTime = now;
        LONG absSrcStride = std::abs(srcStride);
        int calcSize = absSrcStride * srcHeight;
        int safeSize = (actualDataLength > 0 && actualDataLength < calcSize) ? (int)actualDataLength : calcSize;

        if (pSrcData && safeSize > 0) {
            {
                std::lock_guard<std::mutex> lock(m_mtxLBBuffer);
                if (m_lbBuffer.size() < (size_t)safeSize) {
                    m_lbBuffer.resize(safeSize);
                }
                CopyMemory(m_lbBuffer.data(), pSrcData, safeSize);
                m_lbWidth = srcWidth;
                m_lbHeight = srcHeight;
                m_lbStride = absSrcStride;
                m_lbBpp = srcBitCount;
            }
            {
                std::lock_guard<std::mutex> lock(m_mtxLBControl);
                m_bLBRequest = true;
                m_cvLB.notify_one();
            }
        }
    }

    // 結果適用
    LetterboxMode mode;
    {
        std::lock_guard<std::mutex> lock(m_mtxLBMode);
        mode = m_currentLBMode;
    }

    if (mode != LB_MODE_ORIGINAL) {
        float targetAspect = (mode == LB_MODE_16_9) ? (16.0f / 9.0f) : (4.0f / 3.0f);
        LONG targetH = (LONG)(srcWidth / targetAspect);
        LONG barH = (srcHeight - targetH) / 2;
        if (barH > 0) {
            srcRect.top = barH;
            srcRect.bottom = srcHeight - barH;
            pSrcRect = &srcRect;
        }
    }
}

LetterboxMode LR2BGATransformLogic::GetCurrentLetterboxMode() const {
    // m_mtxLBMode は mutable 宣言されているため、const メソッド内でもロック可能
    std::lock_guard<std::mutex> lock(m_mtxLBMode);
    return m_currentLBMode;
}

// ------------------------------------------------------------------------------
// Helper: WaitFPSLimit - FPS制限の実装
//
// 役割:
//   設定された最大FPS (m_maxFPS) を超えないように制御します。
//   前回出力時刻からの経過時間をチェックし、必要に応じて Sleep で待機します。
//
// 戻り値:
//   S_OK    : 処理続行 (FPS制限内、または制限なし)
//   S_FALSE : フレームスキップ (FPS制限によりドロップすべき)
// ------------------------------------------------------------------------------
HRESULT LR2BGATransformLogic::WaitFPSLimit(REFERENCE_TIME rtStart, REFERENCE_TIME rtEnd) {
    if (!m_pSettings->m_limitFPSEnabled || m_pSettings->m_maxFPS <= 0) {
        return S_OK;
    }

    REFERENCE_TIME minInterval = 10000000LL / m_pSettings->m_maxFPS;
    if (m_lastOutputTime > 0 && (rtStart - m_lastOutputTime) < minInterval) {
        DWORD waitMs = 0;
        if (rtEnd > rtStart) {
            waitMs = (DWORD)((rtEnd - rtStart) / 10000);
        }
        if (waitMs > 0 && waitMs < kTransformMaxSleepMs) {
            Sleep(waitMs);
        }
        m_droppedFrames++;
        return S_FALSE; // Skip
    }
    m_lastOutputTime = rtStart;
    return S_OK;
}

// ------------------------------------------------------------------------------
// Helper: FillOutputBuffer - 出力バッファへの描画処理
//
// 役割:
//   現在の動作モード（ダミー、パススルー、リサイズ）に応じて、
//   入力データを加工して出力バッファへ書き込みます。
//
// 処理パターン:
//   1. ダミーモード: 1x1の黒画像を生成（一度だけ送信し、以降はスキップ）。
//   2. パススルー: リサイズを行わず、フォーマット変換（RGB32->24）のみ実施。
//   3. リサイズ: 
//      - アスペクト比維持設定を考慮して出力サイズを計算。
//      - 指定されたアルゴリズム（最近傍法/バイリニア法）でリサイズ実行。
//      - 余白（レターボックス）が生じる場合は黒で塗りつぶし。
//   4. 明るさ調整: LR2用の明度設定を適用。
//
// 引数:
//   pSrcData / pDstData : 入出力バッファポインタ
//   srcWidth...dstStride: 入出力の画像パラメータ
//   pSrcRect            : 切り出し範囲（nullptrの場合は全体）
//   rtStart / rtEnd     : タイムスタンプ参照（更新用）
//   pOut                : 出力サンプル（データ長設定用）
// ------------------------------------------------------------------------------
HRESULT LR2BGATransformLogic::FillOutputBuffer(const BYTE* pSrcData, BYTE* pDstData,
                                               int srcWidth, int srcHeight, int srcStride, int srcBitCount,
                                               int dstWidth, int dstHeight, int dstStride, const RECT* pSrcRect,
                                               REFERENCE_TIME& rtStart, REFERENCE_TIME& rtEnd,
                                               long& outActualDataLength) {
    // -------------------------------------------------------------------------
    // ダミーモード処理
    // 入力がない場合（1x1黒画像）、一度だけ黒フレームを出力してスキップ
    // -------------------------------------------------------------------------
    if (m_activeDummy) {
        if (!m_dummySent) {
            ZeroMemory(pDstData, dstStride * dstHeight);
            outActualDataLength = dstStride * dstHeight;
            m_dummySent = true;
            m_lastDummyTime = rtStart;
            return S_OK;
        } else {
            // 2フレーム目以降はスキップ（プレゼンテーション時間だけ待機）
            DWORD waitMs = 0;
            if (rtEnd > rtStart) waitMs = (DWORD)((rtEnd - rtStart) / 10000);
            if (waitMs > 0 && waitMs < kTransformMaxSleepMs) Sleep(waitMs);
            return S_FALSE;
        }
    }

    // -------------------------------------------------------------------------
    // パススルー判定
    // m_activePassthrough は StartStreaming でラッチ済み
    // -------------------------------------------------------------------------
    bool isPassthrough = m_activePassthrough;

    if (isPassthrough) {
        // パススルー時も出力バッファサイズを超えないように制限
        int copyHeight = (srcHeight < dstHeight) ? srcHeight : dstHeight;
        int copyWidth = (srcWidth < dstWidth) ? srcWidth : dstWidth;
        
        if (srcBitCount == 32) {
             for (int y = 0; y < copyHeight; y++) {
                const BYTE *pSrcRow = pSrcData + y * srcStride;
                BYTE *pDstRow = pDstData + y * dstStride;
                for (int x = 0; x < copyWidth; x++) {
                    // RGB32 -> RGB24
                    pDstRow[x * 3 + 0] = pSrcRow[x * 4 + 0];
                    pDstRow[x * 3 + 1] = pSrcRow[x * 4 + 1];
                    pDstRow[x * 3 + 2] = pSrcRow[x * 4 + 2];
                }
            }
        } else {
            // RGB24 -> RGB24
            int copyW = copyWidth * 3;
            if (copyW > dstStride) copyW = dstStride;
            if (copyW > srcStride) copyW = srcStride;
            for (int y = 0; y < copyHeight; y++) {
                CopyMemory(pDstData + y * dstStride, pSrcData + y * srcStride, copyW);
            }
        }
    } 
    // Resize
    else {
        int effectiveSrcW = srcWidth;
        int effectiveSrcH = srcHeight;
        if (pSrcRect) {
            effectiveSrcW = pSrcRect->right - pSrcRect->left;
            effectiveSrcH = pSrcRect->bottom - pSrcRect->top;
        }

        int actualW, actualH, offX, offY;
        LR2BGAImageProc::CalculateResizeDimensions(
            effectiveSrcW, effectiveSrcH, dstWidth, dstHeight,
            (m_pSettings->m_keepAspectRatio != FALSE), 
            actualW, actualH, offX, offY);

        if (actualW < dstWidth || actualH < dstHeight) {
            ZeroMemory(pDstData, dstStride * dstHeight);
        }

        if (m_pSettings->m_resizeAlgo == RESIZE_NEAREST) {
            LR2BGAImageProc::ResizeNearestNeighbor(
                pSrcData, srcWidth, srcHeight, srcStride, srcBitCount, pDstData,
                dstWidth, dstHeight, dstStride, 24, actualW, actualH, offX, offY,
                pSrcRect, m_lutXIndices);
        } else {
            LR2BGAImageProc::ResizeBilinear(
                pSrcData, srcWidth, srcHeight, srcStride, srcBitCount, pDstData,
                dstWidth, dstHeight, dstStride, 24, actualW, actualH, offX, offY,
                pSrcRect, m_lutXIndices, m_lutXWeights);
        }
    }

    // Brightness
    if (m_pSettings->m_brightnessLR2 < 100) {
        LR2BGAImageProc::ApplyBrightness(pDstData, dstWidth, dstHeight, dstStride, (int)m_pSettings->m_brightnessLR2);
    }

    outActualDataLength = dstStride * dstHeight;
    return S_OK;
}

void LR2BGATransformLogic::ResetStatistics() {
    m_droppedFrames = 0;
}
