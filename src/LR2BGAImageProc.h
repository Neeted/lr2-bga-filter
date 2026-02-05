#pragma once
#include <windows.h>
#include <vector>

class LR2BGAImageProc {
public:
  // アスペクト比を維持したリサイズ後の寸法とオフセットを計算する
  static void CalculateResizeDimensions(
      int srcWidth, int srcHeight,
      int targetWidth, int targetHeight,
      bool keepAspectRatio,
      int& outWidth, int& outHeight,
      int& offsetX, int& offsetY);

  // 最近傍補間（ニアレストネイバー）によるリサイズ（高速、ドット絵向き）
  // RGB32/24入力 -> RGB24出力
  static void ResizeNearestNeighbor(
      const BYTE* pSrc, int srcWidth, int srcHeight, int srcStride, int srcBpp,
      BYTE* pDst, int dstWidth, int dstHeight, int dstStride, int dstBpp,
      int actualWidth, int actualHeight, int offsetX, int offsetY, 
      const RECT* pSrcRect, std::vector<int>& lutIndices);

  // 双線形補間（バイリニア）によるリサイズ（高品質、写真・実写向き）
  // RGB32/24入力 -> RGB24出力
  static void ResizeBilinear(
      const BYTE* pSrc, int srcWidth, int srcHeight, int srcStride, int srcBpp,
      BYTE* pDst, int dstWidth, int dstHeight, int dstStride, int dstBpp,
      int actualWidth, int actualHeight, int offsetX, int offsetY, 
      const RECT* pSrcRect, std::vector<int>& lutIndices, std::vector<short>& lutWeights);

  // 明るさ調整 (In-place処理)
  // RGB24バッファの各画素値を指定されたパーセンテージ(0-100)で暗くします
  static void ApplyBrightness(BYTE* pData, int width, int height, int stride, int brightness);

  // 初期化 (CPU機能判定と関数ポインタ設定)
  static void Initialize();

private:
  // 関数ポインタ型定義
  typedef void (*ResizeFunc)(const BYTE* pSrc, int srcW, int srcH, int srcStr, int srcBpp,
                             BYTE* pDst, int dstW, int dstH, int dstStr, int dstBpp,
                             int actW, int actH, int offX, int offY, const RECT* pSrcRect,
                             std::vector<int>& lutI, std::vector<short>& lutW);

  // 実装関数 (C++ Pure)
  static void ResizeNearestNeighbor_Cpp(const BYTE* pSrc, int srcW, int srcH, int srcStr, int srcBpp,
                                        BYTE* pDst, int dstW, int dstH, int dstStr, int dstBpp,
                                        int actW, int actH, int offX, int offY, const RECT* pSrcRect,
                                        std::vector<int>& lutI, std::vector<short>& lutW);

  static void ResizeBilinear_Cpp(const BYTE* pSrc, int srcW, int srcH, int srcStr, int srcBpp,
                                 BYTE* pDst, int dstW, int dstH, int dstStr, int dstBpp,
                                 int actW, int actH, int offX, int offY, const RECT* pSrcRect,
                                 std::vector<int>& lutI, std::vector<short>& lutW);

  // Optimized C++ Implementations (Fixed-point + LUT)
  static void ResizeNearestNeighbor_CppOpt(const BYTE* pSrc, int srcW, int srcH, int srcStr, int srcBpp,
                                           BYTE* pDst, int dstW, int dstH, int dstStr, int dstBpp,
                                           int actW, int actH, int offX, int offY, const RECT* pSrcRect,
                                           std::vector<int>& lutI, std::vector<short>& lutW);

  static void ResizeBilinear_CppOpt(const BYTE* pSrc, int srcW, int srcH, int srcStr, int srcBpp,
                                    BYTE* pDst, int dstW, int dstH, int dstStr, int dstBpp,
                                    int actW, int actH, int offX, int offY, const RECT* pSrcRect,
                                    std::vector<int>& lutI, std::vector<short>& lutW);

  // SSE4.1 Implementations
  static void ResizeBilinear_SSE41(const BYTE* pSrc, int srcW, int srcH, int srcStr, int srcBpp,
                                   BYTE* pDst, int dstW, int dstH, int dstStr, int dstBpp,
                                   int actW, int actH, int offX, int offY, const RECT* pSrcRect,
                                   std::vector<int>& lutI, std::vector<short>& lutW);

  // 関数ポインタ (Dispatch Target)
  static ResizeFunc pResizeNearest;
  static ResizeFunc pResizeBilinear;
  static bool m_initialized;
};


