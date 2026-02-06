#include "LR2BGALetterboxDetector.h"

//------------------------------------------------------------------------------
// 定数定義 (Constants)
//------------------------------------------------------------------------------
constexpr int kDefaultBlackThreshold = 16;      // デフォルト閾値: 明るさ16未満を黒とみなす
constexpr int kDefaultStabilityThreshold = 5;   // デフォルト安定化: 5回連続検出で確定

LR2BGALetterboxDetector::LR2BGALetterboxDetector()
    : m_currentMode(LB_MODE_ORIGINAL), m_pendingMode(LB_MODE_ORIGINAL),
      m_stabilityCounter(0),
      m_blackThreshold(kDefaultBlackThreshold) // デフォルト閾値: 明るさ16未満を黒とみなす
      ,
      m_stabilityThreshold(kDefaultStabilityThreshold) // デフォルト安定化: 5回連続検出で確定
      ,
      m_bRejected169(false), m_bRejected43(false) {
  // Init
}

LR2BGALetterboxDetector::~LR2BGALetterboxDetector() {
  // Delete
}

void LR2BGALetterboxDetector::SetParams(int threshold, int stabilityFrames) {
  m_blackThreshold = threshold;
  m_stabilityThreshold = stabilityFrames;
}

void LR2BGALetterboxDetector::Reset() {
  m_currentMode = LB_MODE_ORIGINAL;
  m_pendingMode = LB_MODE_ORIGINAL;
  m_stabilityCounter = 0;

  // 除外フラグのクリア
  m_bRejected169 = false;
  m_bRejected43 = false;

  {
      std::lock_guard<std::mutex> lock(m_mtxDebugInfo);
      m_lastDebugInfo = LetterboxDebugInfo(); // Clear
  }
}

LetterboxDebugInfo LR2BGALetterboxDetector::GetDebugInfo() {
  std::lock_guard<std::mutex> lock(m_mtxDebugInfo);
  LetterboxDebugInfo info = m_lastDebugInfo;
  return info;
}

// -----------------------------------------------------------------------------
// フレーム解析のメインロジック
// 上下の黒帯を検出し、最適なアスペクト比モードを推奨します。
// -----------------------------------------------------------------------------
LetterboxMode LR2BGALetterboxDetector::AnalyzeFrame(const BYTE *pBuffer,
                                                    size_t bufferSize,
                                                    LONG width, LONG height,
                                                    LONG stride,
                                                    int bitsPerPixel) {
  LetterboxDebugInfo debugInfo;
  debugInfo.stabilityThreshold = m_stabilityThreshold;
  debugInfo.is43TopBlack = false;
  debugInfo.ratio43Top = -1.0f; // Skipped state
  debugInfo.is43BottomBlack = false;
  debugInfo.ratio43Bottom = -1.0f;

  if (!pBuffer || width <= 0 || height <= 0 || bufferSize == 0)
    return LB_MODE_ORIGINAL;

  // 現在のアスペクト比を計算
  float currentAspect = (float)width / height;

  // アスペクト比による事前除外
  // (構造的にレターボックスになり得ない場合をリジェクト)
  // 一度リジェクトされればフラグが立ち、以後のフレームでもチェックがスキップされる
  if (currentAspect >= 1.76f) { // 16:9 (1.77) 以上
    m_bRejected169 = true;
    m_bRejected43 = true;
  } else if (currentAspect >= 1.32f) { // 4:3 (1.33) 以上
    m_bRejected43 = true;
  }

  // この後の "Rejection Latch Logic" ブロックで
  // m_bRejectedの状態に応じた早期リターンが行われるため ここで return
  // LB_MODE_ORIGINAL する必要はない (フローを共通化)

  // -------------------------------------------------------------------------
  // 新検出ロジック (Refined Detection Logic)
  // -------------------------------------------------------------------------

  LetterboxMode detectedMode = LB_MODE_ORIGINAL;

  // 初期状態としてのデバッグ情報更新
  debugInfo.bRejected169 = m_bRejected169;
  debugInfo.bRejected43 = m_bRejected43;

  // 既に両方とも除外されている場合は、解析せずオリジナルモード確定
  if (m_bRejected169 && m_bRejected43) {
    detectedMode = LB_MODE_ORIGINAL;
  } else {
    bool is169Detected = false;

    // ---------------------------------------------------------------------
    // 1. Check 16:9
    // ---------------------------------------------------------------------
    if (!m_bRejected169) {
      // 幅を基準に、16:9である場合の理想の高さを計算
      LONG targetHeight169 = (LONG)(width / (16.0f / 9.0f));
      LONG barHeight169 = (height - targetHeight169) / 2;
      const LONG CHECK_MARGIN = 8;

      if (barHeight169 > CHECK_MARGIN) {
        LONG checkHeight = barHeight169 - CHECK_MARGIN;

        debugInfo.is169TopBlack =
            IsRegionBlack(pBuffer, bufferSize, width, height, stride,
                          bitsPerPixel, 0, checkHeight, &debugInfo.ratio169Top);
        debugInfo.is169BottomBlack = IsRegionBlack(
            pBuffer, bufferSize, width, height, stride, bitsPerPixel,
            height - checkHeight, height, &debugInfo.ratio169Bottom);

        if (debugInfo.is169TopBlack && debugInfo.is169BottomBlack) {
          // 16:9 黒帯検出成功
          is169Detected = true;

          // コンテンツ領域が暗いかどうかチェック (誤検出防止)
          if (IsContentAreaDark(pBuffer, bufferSize, width, height, stride,
                                bitsPerPixel, &debugInfo.centerBlackRatio)) {
            debugInfo.isCenterBlack = true;
            // 暗い場合は解析不能のため、現在のモードを維持する
            detectedMode = m_currentMode;
          } else {
            debugInfo.isCenterBlack = false;
            // 明るい場合は 16:9 モードと判定
            detectedMode = LB_MODE_16_9;
          }
        } else {
          // 黒帯領域にコンテンツがある -> 16:9ではない (恒常的に除外)
          m_bRejected169 = true;
          debugInfo.bRejected169 = true;
        }
      }
    }

    // ---------------------------------------------------------------------
    // 2. Check 4:3 (Only if 16:9 is not detected/rejected)
    // ---------------------------------------------------------------------
    // 16:9が検出された場合(is169Detected=true)は、より厳しいクロップを採用するため4:3チェックは行わない。
    // 16:9が除外された(m_bRejected169)か、現在たまたま16:9黒帯が見つからなかった場合にのみ4:3をチェックする。
    if (!is169Detected && !m_bRejected43) {
      LONG targetHeight43 = (LONG)(width / (4.0f / 3.0f));
      LONG barHeight43 = (height - targetHeight43) / 2;
      const LONG CHECK_MARGIN = 8;

      if (barHeight43 > CHECK_MARGIN) {
        LONG checkHeight = barHeight43 - CHECK_MARGIN;

        debugInfo.is43TopBlack =
            IsRegionBlack(pBuffer, bufferSize, width, height, stride,
                          bitsPerPixel, 0, checkHeight, &debugInfo.ratio43Top);
        debugInfo.is43BottomBlack = IsRegionBlack(
            pBuffer, bufferSize, width, height, stride, bitsPerPixel,
            height - checkHeight, height, &debugInfo.ratio43Bottom);

        if (debugInfo.is43TopBlack && debugInfo.is43BottomBlack) {
          // 4:3 黒帯検出成功
          if (IsContentAreaDark(pBuffer, bufferSize, width, height, stride,
                                bitsPerPixel, &debugInfo.centerBlackRatio)) {
            debugInfo.isCenterBlack = true;
            detectedMode = m_currentMode;
          } else {
            debugInfo.isCenterBlack = false;
            detectedMode = LB_MODE_4_3;
          }
        } else {
          // 黒帯領域にコンテンツがある -> 4:3ではない
          m_bRejected43 = true;
          debugInfo.bRejected43 = true;
          // 両方NGならオリジナル
          detectedMode = LB_MODE_ORIGINAL;
        }
      }
    }
  }

  debugInfo.detectedMode = detectedMode;

  // -------------------------------------------------------------------------
  // ヒステリシス制御 (チャタリング防止)
  // 検出結果が「今回のフレーム」だけで変わっても即座には適用しません。
  // 指定回数（m_stabilityThreshold）連続して同じモードが検出された場合のみ、モード変更を確定させます。
  // これにより、数フレームの一瞬のノイズやフラッシュで画角がカクつくのを防ぎます。
  // -------------------------------------------------------------------------
  if (detectedMode == m_pendingMode) {
    m_stabilityCounter++;
    if (m_stabilityCounter >= m_stabilityThreshold) {
      m_currentMode = detectedMode;
    }
  } else {
    // 検出モードが変わったため、カウンターをリセットして監視し直し
    m_pendingMode = detectedMode;
    m_stabilityCounter = 0;
  }

  debugInfo.stabilityCounter = m_stabilityCounter;

  {
      std::lock_guard<std::mutex> lock(m_mtxDebugInfo);
      m_lastDebugInfo = debugInfo;
  }

  return m_currentMode;
}

// -----------------------------------------------------------------------------
// 指定領域の黒判定
// CPU負荷を抑えるため、全画素ではなく間引いてサンプリングを行います。
// -----------------------------------------------------------------------------
bool LR2BGALetterboxDetector::IsRegionBlack(const BYTE *pBuffer,
                                            size_t bufferSize, LONG width,
                                            LONG height, LONG stride, int bpp,
                                            LONG startY, LONG endY,
                                            float *outRatio) {
  if (outRatio)
    *outRatio = 0.0f;
  if (startY < 0)
    startY = 0;
  if (endY > height)
    endY = height;
  if (startY >= endY)
    return false;

  // サンプリング戦略:
  // 解像度に応じてサンプリング間隔を動的に調整
  // 低解像度(240p等)では細かく、高解像度(720p等)では粗くサンプリングする
  // 目安: 短辺の1/150程度 (240px -> 1.6 -> 1px / 720px -> 4.8 -> 4px)
  // 最大値は従来の5pxに制限
  int step = max(1, min(width, height) / 150);
  if (step > 5)
    step = 5;

  const int LINE_STEP = step;
  const int PIXEL_STEP = step;

  int bytesPerPixel = bpp / 8;
  int totalSampled = 0;
  int blackSampled = 0;

  // 安全のためバッファ終端アドレスを計算
  // ストライドが負の場合でも、ポインタ演算の結果がこの範囲内かチェックする必要がある
  const BYTE *pBufferEnd = pBuffer + bufferSize;

  for (int y = startY; y < endY; y += LINE_STEP) {
    // 該当行の先頭アドレス計算
    // ストライド (stride) はパディングを含んだ1行のバイト数
    LONG_PTR rowOffset = (LONG_PTR)y * stride;

    const BYTE *pRow = pBuffer + rowOffset;

    // 簡易的な行ポインタの範囲チェック
    if (pRow < pBuffer || pRow >= pBufferEnd) {
      // 範囲外の場合はこの行をスキップ
      continue;
    }

    for (int x = 0; x < width; x += PIXEL_STEP) {
      const BYTE *pPixel = pRow + (x * bytesPerPixel);

      // 厳密な境界チェック
      // RGB(A)成分を読み取るため、+3バイト先まで安全か確認
      if (pPixel < pBuffer || pPixel + 3 > pBufferEnd) {
        break; // この行の処理を中断
      }

      // 画素データ取得 (リトルエンディアン BGR)
      BYTE b = pPixel[0];
      BYTE g = pPixel[1];
      BYTE r = pPixel[2];

      // 輝度(Y)を計算して判定
      // Y = 0.299R + 0.587G + 0.114B
      // 高速化のため整数演算を使用: Y = (77*R + 150*G + 29*B) >> 8 (合計256)
      int yVal = (77 * r + 150 * g + 29 * b) >> 8;

      if (yVal < m_blackThreshold) {
        blackSampled++;
      }
      totalSampled++;
    }
  }

  if (totalSampled == 0)
    return false;

  float ratio = (float)blackSampled / totalSampled;
  if (outRatio)
    *outRatio = ratio;

  // 95%以上の画素が「黒」であれば、その領域は黒帯とみなす
  // (多少のノイズや字幕の端などが含まれていても許容するため100%にはしない)
  // 圧縮ノイズ対策として 99% -> 95% に緩和
  return ratio > 0.95f;
}

// -----------------------------------------------------------------------------
// コンテンツ領域の暗部判定
// 16:9のアスペクト比に相当する領域（上下の黒帯を除いた部分）を検査し、
// 一定割合以上（90%）が黒ければ「暗い場面」と判定します。
// -----------------------------------------------------------------------------
bool LR2BGALetterboxDetector::IsContentAreaDark(const BYTE *pBuffer,
                                                size_t bufferSize, LONG width,
                                                LONG height, LONG stride,
                                                int bpp, float *outRatio) {
  // 16:9 の場合のコンテンツ高さを計算
  LONG targetHeight169 = (LONG)(width / (16.0f / 9.0f));
  LONG barHeight = (height - targetHeight169) / 2;

  // マージン (AnalyzeFrame内の定数と同じく8px程度推奨だが、ここでは独自に定義)
  const LONG CHECK_MARGIN = 8;

  LONG startY = barHeight + CHECK_MARGIN;
  LONG endY = height - barHeight - CHECK_MARGIN;

  // コンテンツ領域が極端に狭い場合は全体をチェック（安全策）
  if (startY >= endY) {
    startY = 0;
    endY = height;
  }

  // 指定領域の黒画素率を取得
  float ratio = 0.0f;
  IsRegionBlack(pBuffer, bufferSize, width, height, stride, bpp, startY, endY,
                &ratio);

  if (outRatio)
    *outRatio = ratio;

  // 90%以上が黒ければ「暗い」とみなす
  return ratio > 0.50f;
}

