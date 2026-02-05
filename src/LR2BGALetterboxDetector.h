#pragma once
#include <windows.h>
#include <mutex>

// 検出モード (Letterbox Detection Modes)
enum LetterboxMode {
    LB_MODE_ORIGINAL = 0, // 加工なし (黒帯なし、または検出されず)
    LB_MODE_16_9,         // 16:9 のレターボックスを検出 (上下黒帯をカット)
    LB_MODE_4_3           // 4:3 のレターボックスを検出 (上下黒帯をカット)
};

// デバッグ用情報構造体
struct LetterboxDebugInfo {
    int stabilityCounter = 0;
    int stabilityThreshold = 0;
    
    bool isCenterBlack = false;
    float centerBlackRatio = 0.0f;
    
    bool is169TopBlack = false;
    float ratio169Top = 0.0f;
    bool is169BottomBlack = false;
    float ratio169Bottom = 0.0f;
    
    bool is43TopBlack = false;
    float ratio43Top = 0.0f;
    bool is43BottomBlack = false;
    float ratio43Bottom = 0.0f;
    
    // 除外ステータス (Rejection Status)
    // 一度でも黒帯領域にコンテンツが描画された場合、そのモードは恒久的に除外される
    bool bRejected169 = false;
    bool bRejected43 = false;
    
    LetterboxMode detectedMode = LB_MODE_ORIGINAL;
};

// -----------------------------------------------------------------------------
// LR2BGALetterboxDetector
// 
// 映像フレーム内の「黒帯（レターボックス）」を自動検出するためのクラスです。
// フレームバッファを解析し、上下に黒い領域が存在するかどうかを判定します。
// ちらつき防止のためのヒステリシス制御（安定化機能）も備えています。
// -----------------------------------------------------------------------------
class LR2BGALetterboxDetector
{
public:
    LR2BGALetterboxDetector();
    ~LR2BGALetterboxDetector();

    // メイン解析関数 (スレッドセーフ設計)
    // ワーカースレッドから呼び出され、推奨されるレターボックスモードを返します。
    // 引数:
    //   pBuffer: 画像データへのポインタ
    //   bufferSize: バッファサイズ
    //   width, height: 画像解像度
    //   stride: 1行あたりのバイト数
    //   bitsPerPixel: ビット深度 (24 or 32)
    LetterboxMode AnalyzeFrame(const BYTE* pBuffer, size_t bufferSize, LONG width, LONG height, LONG stride, int bitsPerPixel);

    // 現在確定している（安定した）モードを取得します
    LetterboxMode GetCurrentMode() const { return m_currentMode; }

    // デバッグ情報を取得（スレッドセーフ）
    LetterboxDebugInfo GetDebugInfo();

    // 状態をリセットします (解像度変更時などに使用)
    void Reset();

    // 検出パラメータを設定します
    // threshold: 黒とみなす輝度閾値 (0-255)
    // stabilityFrames: モード切替を確定するために必要な連続一致フレーム数
    void SetParams(int threshold, int stabilityFrames);

private:
    // ヘルパー: 指定されたY範囲（行範囲）がすべて「黒」かどうかを判定します
    // outRatio: 黒画素の割合 (0.0 - 1.0) を書き戻すポインタ (nullptr可)
    bool IsRegionBlack(const BYTE* pBuffer, size_t bufferSize, LONG width, LONG height, LONG stride, int bpp, LONG startY, LONG endY, float* outRatio = nullptr);
    
    // ヘルパー: コンテンツ領域（16:9相当）が「暗い」かどうかを判定します
    // 暗転時やフェードアウト時に誤ってレターボックスと判定するのを防ぐために使用します
    // outRatio: 黒画素の割合 (0.0 - 1.0) を書き戻すポインタ (nullptr可)
    bool IsContentAreaDark(const BYTE* pBuffer, size_t bufferSize, LONG width, LONG height, LONG stride, int bpp, float* outRatio = nullptr);

    // 現在確定しているモード
    LetterboxMode m_currentMode;
    
    // 除外フラグ (Rejection Flags)
    bool m_bRejected169;
    bool m_bRejected43;

    // ヒステリシス制御用変数
    // m_pendingMode: 現在検出されているが、まだ確定していない（安定待ちの）モード
    LetterboxMode m_pendingMode;
    // m_stabilityCounter: 同じモードが連続して検出された回数
    int m_stabilityCounter;
    
    // 設定パラメータ
    int m_stabilityThreshold; // 確定に必要なフレーム数
    int m_blackThreshold;     // 黒判定の閾値

    // デバッグ情報
    LetterboxDebugInfo m_lastDebugInfo;
    std::mutex m_mtxDebugInfo; // デバッグ情報アクセス保護用
};


