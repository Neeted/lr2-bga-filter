#include "LR2BGAImageProc.h"
#include "LR2BGACPU.h"

// Static Initializations
LR2BGAImageProc::ResizeFunc LR2BGAImageProc::pResizeNearest = LR2BGAImageProc::ResizeNearestNeighbor_Cpp;
LR2BGAImageProc::ResizeFunc LR2BGAImageProc::pResizeBilinear = LR2BGAImageProc::ResizeBilinear_Cpp;
bool LR2BGAImageProc::m_initialized = false;

void LR2BGAImageProc::Initialize() {
    if (m_initialized) return;

    // Default to Optimized C++ implementation (Fixed-point + LUT)
    pResizeNearest = ResizeNearestNeighbor_CppOpt;
    pResizeBilinear = ResizeBilinear_CppOpt;

    // Check CPU features and upgrade if possible
    if (LR2BGACPU::IsAVX2Supported()) {
        // pResizeNearest = ResizeNearestNeighbor_AVX2; // Future
        // pResizeBilinear = ResizeBilinear_AVX2;       // Future
    } 
    else if (LR2BGACPU::IsSSE41Supported()) {
        // pResizeNearest = ResizeNearestNeighbor_SSE41; // Future
        pResizeBilinear = ResizeBilinear_SSE41;
    }

    m_initialized = true;
}

//------------------------------------------------------------------------------
// LR2BGAImageProc.cpp
//
// 概要:
//   フィルタ内で使用される画像処理アルゴリズムのコレクションです。
//   実行環境のCPU機能（SSE4.1等）に応じて最適な実装を動的に選択します。
//
// 機能:
//   - リサイズ: 最近傍法 (Nearest Neighbor) および バイリニア法 (Bilinear)。
//   - アスペクト比計算: ソース矩形とターゲット矩形から最適な描画位置を算出。
//   - 色変換/明るさ調整: ピクセル単位の操作。
//
// 実装の詳細:
//   - CppOpt: 固定小数点演算とLUT（Look-Up Table）を使用した最適化版標準実装。
//   - SSE4.1: SSE4.1命令セットを使用したSIMD実装（Bilinear対応）。
//   - MultiThread: スレッドプールを使用した並列処理（Yループ分割）。
//
// パフォーマンスノート:
//   - CPUがSSE4.1をサポートしている場合、自動的にSIMD版が選択されます。
//   - マルチスレッド化により、マルチコアCPUでの処理効率が最大化されます。
//   - バッファオーバーランを防ぐため、ストライドや境界チェックを厳密に行う必要があります。
//-------------------------------------------------------------------------------

// ------------------------------------------------------------------------------
// CalculateResizeDimensions
// 入力サイズとターゲットサイズから、アスペクト比を考慮した実際の出力サイズと
// 描画開始オフセット（パディング）を計算します。
// ------------------------------------------------------------------------------
void LR2BGAImageProc::CalculateResizeDimensions(
    int srcWidth, int srcHeight,
    int targetWidth, int targetHeight,
    bool keepAspectRatio,
    int& outWidth, int& outHeight,
    int& offsetX, int& offsetY)
{
    if (!keepAspectRatio) {
        // Stretch to fill
        outWidth = targetWidth;
        outHeight = targetHeight;
        offsetX = 0;
        offsetY = 0;
        return;
    }

    // Keep aspect ratio
    float srcAspect = (float)srcWidth / srcHeight;
    float targetAspect = (float)targetWidth / targetHeight;

    if (srcAspect > targetAspect) {
        // Source is wider - fit to width
        outWidth = targetWidth;
        outHeight = (int)(targetWidth / srcAspect);
        offsetX = 0;
        offsetY = (targetHeight - outHeight) / 2;
    } else {
        // Source is taller - fit to height
        outHeight = targetHeight;
        outWidth = (int)(targetHeight * srcAspect);
        offsetX = (targetWidth - outWidth) / 2;
        offsetY = 0;
    }
}

// ------------------------------------------------------------------------------
// Wrapper: ResizeNearestNeighbor
// ------------------------------------------------------------------------------
void LR2BGAImageProc::ResizeNearestNeighbor(
    const BYTE* pSrc, int srcWidth, int srcHeight, int srcStride, int srcBpp,
    BYTE* pDst, int dstWidth, int dstHeight, int dstStride, int dstBpp,
    int actualWidth, int actualHeight, int offsetX, int offsetY, 
    const RECT* pSrcRect, std::vector<int>& lutIndices)
{
    if (!m_initialized) Initialize();
    
    // Create dummy weights for function pointer compatibility
    static std::vector<short> dummyWeights; // Thread-safe enough as it's unused dummy and read-only context effectively
    // But to be 100% safe against concurrent writes if implementation changed to write to it:
    // The implementations (CppOpt, SSE41) Resize only if size < needed.
    // If we pass an empty vector, they might resize it.
    // If multiple threads call ResizeNearestNeighbor concurrently (which shouldn't happen for valid Filter graph usually, but still), 
    // sharing a static vector that gets resized is bad.
    // However, ResizeNearestNeighbor implementations (Cpp, CppOpt) do NOT use lutWeights.
    // So they won't resize it?
    // ResizeNearestNeighbor_CppOpt signature: std::vector<int>& lutIndices, std::vector<short>& lutWeights
    // But it only uses lutIndices.
    // So it won't touch lutWeights.
    // So a local dummy is fine.
    std::vector<short> dummyW;
    pResizeNearest(pSrc, srcWidth, srcHeight, srcStride, srcBpp, 
                   pDst, dstWidth, dstHeight, dstStride, dstBpp, 
                   actualWidth, actualHeight, offsetX, offsetY, pSrcRect, lutIndices, dummyW);
}

// ------------------------------------------------------------------------------
// Implementation: ResizeNearestNeighbor_Cpp
// ------------------------------------------------------------------------------
void LR2BGAImageProc::ResizeNearestNeighbor_Cpp(
    const BYTE* pSrc, int srcWidth, int srcHeight, int srcStride, int srcBpp,
    BYTE* pDst, int dstWidth, int dstHeight, int dstStride, int dstBpp,
    int actualWidth, int actualHeight, int offsetX, int offsetY, 
    const RECT* pSrcRect, std::vector<int>& lutIndices, std::vector<short>& lutWeights)
{
    int srcBytes = srcBpp / 8;
    int dstBytes = dstBpp / 8;

    // Determine source region
    RECT rect = { 0, 0, srcWidth, srcHeight };
    if (pSrcRect) rect = *pSrcRect;
    int srcRectW = rect.right - rect.left;
    int srcRectH = rect.bottom - rect.top;

    if (srcRectW <= 0 || srcRectH <= 0) return;

    // Fill background (black) if letterboxing
    if (actualWidth < dstWidth || actualHeight < dstHeight) {
        // This is simplified; assumes black background is already cleared or we need to clear strict areas
        // For performance, we assume caller clears buffer or we fill partial
        // Here we just loop through target lines
    }

    // ソース矩形に基づいてスケーリング係数を計算
    float scaleX = (float)srcRectW / actualWidth;
    float scaleY = (float)srcRectH / actualHeight;

    // Render loop
    for (int y = 0; y < actualHeight; y++) {
        int dstY = y + offsetY;
        if (dstY < 0 || dstY >= dstHeight) continue;

        int srcY = rect.top + (int)(y * scaleY);
        if (srcY >= rect.bottom) srcY = rect.bottom - 1;

        BYTE* pDstRow = pDst + dstY * dstStride;
        const BYTE* pSrcRow = pSrc + srcY * srcStride;

        for (int x = 0; x < actualWidth; x++) {
            int dstX = x + offsetX;
            if (dstX < 0 || dstX >= dstWidth) continue;

            int srcX = rect.left + (int)(x * scaleX);
            if (srcX >= rect.right) srcX = rect.right - 1;

            // Copy pixel
            pDstRow[dstX * dstBytes + 0] = pSrcRow[srcX * srcBytes + 0]; // B
            pDstRow[dstX * dstBytes + 1] = pSrcRow[srcX * srcBytes + 1]; // G
            pDstRow[dstX * dstBytes + 2] = pSrcRow[srcX * srcBytes + 2]; // R
        }
    }
}


//------------------------------------------------------------------------------
// Wrapper: ResizeBilinear
//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
// Wrapper: ResizeBilinear
//------------------------------------------------------------------------------
void LR2BGAImageProc::ResizeBilinear(
    const BYTE* pSrc, int srcW, int srcH, int srcStride, int srcBpp,
    BYTE* pDst, int dstW, int dstH, int dstStride, int dstBpp,
    int actualW, int actualH, int offX, int offY,
    const RECT* pSrcRect, std::vector<int>& lutIndices, std::vector<short>& lutWeights)
{
    if (!m_initialized) Initialize();
    pResizeBilinear(pSrc, srcW, srcH, srcStride, srcBpp, 
                    pDst, dstW, dstH, dstStride, dstBpp, 
                    actualW, actualH, offX, offY, pSrcRect, lutIndices, lutWeights);
}

//------------------------------------------------------------------------------
// Implementation: ResizeBilinear_Cpp
//------------------------------------------------------------------------------
void LR2BGAImageProc::ResizeBilinear_Cpp(
    const BYTE* pSrc, int srcW, int srcH, int srcStride, int srcBpp,
    BYTE* pDst, int dstW, int dstH, int dstStride, int dstBpp,
    int actualW, int actualH, int offX, int offY,
    const RECT* pSrcRect,
    std::vector<int>& lutIndices, std::vector<short>& lutWeights)
{
    int srcBytes = srcBpp / 8;
    int dstBytes = dstBpp / 8;

    // Determine source region
    RECT rect = { 0, 0, srcW, srcH };
    if (pSrcRect) rect = *pSrcRect;
    int srcRectW = rect.right - rect.left;
    int srcRectH = rect.bottom - rect.top;
    
    if (srcRectW <= 0 || srcRectH <= 0) return;

    // Bilinear needs -1 for interpolation range, but based on rect
    float scaleX = (float)(srcRectW - 1) / actualW; 
    float scaleY = (float)(srcRectH - 1) / actualH;
    if (actualW <= 1) scaleX = 0;
    if (actualH <= 1) scaleY = 0;

    for (int y = 0; y < actualH; y++) {
        int dstY = y + offY;
        if (dstY < 0 || dstY >= dstH) continue;

        float fy = y * scaleY;
        int y1 = rect.top + (int)fy;
        int y2 = y1 + 1;
        if (y2 >= rect.bottom) y2 = rect.bottom - 1;
        float dy = fy - (int)fy; // fractional part

        const BYTE* pSrcRow1 = pSrc + y1 * srcStride;
        const BYTE* pSrcRow2 = pSrc + y2 * srcStride;
        BYTE* pDstRow = pDst + dstY * dstStride;

        for (int x = 0; x < actualW; x++) {
            int dstX = x + offX;
            if (dstX < 0 || dstX >= dstW) continue;

            float fx = x * scaleX;
            int x1 = rect.left + (int)fx;
            int x2 = x1 + 1;
            if (x2 >= rect.right) x2 = rect.right - 1;
            float dx = fx - (int)fx; // fractional part

            // 4 neighbor pixels
            int idx1 = x1 * srcBytes;
            int idx2 = x2 * srcBytes;

            for (int c = 0; c < 3; c++) {
                float val1 = (float)pSrcRow1[idx1 + c];
                float val2 = (float)pSrcRow1[idx2 + c];
                float val3 = (float)pSrcRow2[idx1 + c];
                float val4 = (float)pSrcRow2[idx2 + c];

                float top = val1 * (1.0f - dx) + val2 * dx;
                float bottom = val3 * (1.0f - dx) + val4 * dx;
                float val = top * (1.0f - dy) + bottom * dy;

                pDstRow[dstX * dstBytes + c] = (BYTE)val;
            }
        }
    }
}

//------------------------------------------------------------------------------
// Implementation: ResizeNearestNeighbor_CppOpt (Pre-calculated Indices)
//------------------------------------------------------------------------------
void LR2BGAImageProc::ResizeNearestNeighbor_CppOpt(
    const BYTE* pSrc, int srcW, int srcH, int srcStride, int srcBpp,
    BYTE* pDst, int dstW, int dstH, int dstStride, int dstBpp,
    int actualW, int actualH, int offX, int offY,
    const RECT* pSrcRect, std::vector<int>& lutIndices, std::vector<short>& lutWeights)
{
    int srcBytes = srcBpp / 8;
    int dstBytes = dstBpp / 8;

    RECT rect = { 0, 0, srcW, srcH };
    if (pSrcRect) rect = *pSrcRect;
    int srcRectW = rect.right - rect.left;
    int srcRectH = rect.bottom - rect.top;

    if (srcRectW <= 0 || srcRectH <= 0 || actualW <= 0 || actualH <= 0) return;

    // Pre-calculate X indices
    if (lutIndices.size() < (size_t)actualW) lutIndices.resize(actualW);
    
    float scaleX = (float)srcRectW / actualW;
    
    for (int x = 0; x < actualW; x++) {
        int srcX = rect.left + (int)(x * scaleX);
        if (srcX >= rect.right) srcX = rect.right - 1;
        lutIndices[x] = srcX * srcBytes;
    }

    float scaleY = (float)srcRectH / actualH;

    for (int y = 0; y < actualH; y++) {
        int dstY = y + offY;
        if (dstY < 0 || dstY >= dstH) continue;

        int srcY = rect.top + (int)(y * scaleY);
        if (srcY >= rect.bottom) srcY = rect.bottom - 1;

        BYTE* pDstRow = pDst + dstY * dstStride;
        const BYTE* pSrcRow = pSrc + srcY * srcStride;

        for (int x = 0; x < actualW; x++) {
            int dstX = x + offX;
            if (dstX < 0 || dstX >= dstW) continue;

            int srcOffset = lutIndices[x];
            
            // Unroll loop for known 24-bit (3 bytes)
            pDstRow[dstX * dstBytes + 0] = pSrcRow[srcOffset + 0];
            pDstRow[dstX * dstBytes + 1] = pSrcRow[srcOffset + 1];
            pDstRow[dstX * dstBytes + 2] = pSrcRow[srcOffset + 2];
        }
    }
}

#include "LR2BGAThreadPool.h"

//------------------------------------------------------------------------------
// Implementation: ResizeBilinear_SSE41 (128-bit SIMD + Multithreading)
//------------------------------------------------------------------------------
void LR2BGAImageProc::ResizeBilinear_SSE41(
    const BYTE* pSrc, int srcW, int srcH, int srcStride, int srcBpp,
    BYTE* pDst, int dstW, int dstH, int dstStride, int dstBpp,
    int actualW, int actualH, int offX, int offY,
    const RECT* pSrcRect, std::vector<int>& lutIndices, std::vector<short>& lutWeights)
{
    // Basic verification
    if (actualW <= 0 || actualH <= 0) return;

    RECT rect = { 0, 0, srcW, srcH };
    if (pSrcRect) rect = *pSrcRect;
    int srcRectW = rect.right - rect.left;
    int srcRectH = rect.bottom - rect.top;

    int srcBytes = srcBpp / 8;
    int dstBytes = dstBpp / 8;

    const int PRECISION_BITS = 11;
    const int PRECISION_SCALE = 1 << PRECISION_BITS; // 2048

    // Pre-calculate X indices and weights
    if (lutIndices.size() < (size_t)actualW) lutIndices.resize(actualW);
    if (lutWeights.size() < (size_t)(actualW * 2)) lutWeights.resize(actualW * 2);

    float scaleX = (float)(srcRectW - 1) / actualW;
    if (actualW <= 1) scaleX = 0;

    for (int x = 0; x < actualW; x++) {
        float fx = x * scaleX;
        int x1 = rect.left + (int)fx;
        if (x1 >= rect.right - 1) x1 = rect.right - 2;
        if (x1 < rect.left) x1 = rect.left;

        lutIndices[x] = x1 * srcBytes;

        float dx = fx - (int)fx;
        int w = (int)(dx * PRECISION_SCALE);
        int inv_w = PRECISION_SCALE - w;
        
        lutWeights[x * 2 + 0] = (short)inv_w;
        lutWeights[x * 2 + 1] = (short)w;
    }

    float scaleY = (float)(srcRectH - 1) / actualH;
    if (actualH <= 1) scaleY = 0;

    // Parallel execution of Y lines
    LR2BGAThreadPool::Instance().ParallelFor(0, actualH, [&](int startY, int endY) {
        for (int y = startY; y < endY; y++) {
            int dstY = y + offY;
            if (dstY < 0 || dstY >= dstH) continue;

            float fy = y * scaleY;
            int y1 = rect.top + (int)fy;
            if (y1 >= rect.bottom - 1) y1 = rect.bottom - 2;
            if (y1 < rect.top) y1 = rect.top;

            float dy = fy - (int)fy;
            int w_y = (int)(dy * PRECISION_SCALE);
            int inv_w_y = PRECISION_SCALE - w_y;

            const BYTE* pSrcRow1 = pSrc + y1 * srcStride;
            const BYTE* pSrcRow2 = pSrc + (y1 + 1) * srcStride;
            BYTE* pDstRow = pDst + dstY * dstStride;

            // Prepare Y weights in XMM register
            __m128i v_inv_wy = _mm_set1_epi32(inv_w_y);
            __m128i v_wy = _mm_set1_epi32(w_y);

            int x = 0;
            
            // SIMD Loop (Process 2 pixels at a time)
            for (; x <= actualW - 2; x += 2) {
                int dstX = x + offX;
                if (dstX < 0 || dstX >= dstW - 1) {
                    goto ScalarFallback_Thread; 
                }

                // Indexes for 2 pixels
                int idx0 = lutIndices[x];
                int idx1 = lutIndices[x+1];

                // Weights for 2 pixels
                short iwx0 = lutWeights[x*2];
                short wx0  = lutWeights[x*2+1];
                short iwx1 = lutWeights[(x+1)*2];
                short wx1  = lutWeights[(x+1)*2+1];

                // Prepare X weight vectors (interleaved for PMADDWD)
                __m128i v_wx0 = _mm_set_epi16(0, 0, wx0, iwx0, wx0, iwx0, wx0, iwx0);
                __m128i v_wx1 = _mm_set_epi16(0, 0, wx1, iwx1, wx1, iwx1, wx1, iwx1);

                // Load 4 pixels
                int p0_TL = *(int*)(pSrcRow1 + idx0);
                int p0_TR = *(int*)(pSrcRow1 + idx0 + srcBytes);
                int p0_BL = *(int*)(pSrcRow2 + idx0);
                int p0_BR = *(int*)(pSrcRow2 + idx0 + srcBytes);

                int p1_TL = *(int*)(pSrcRow1 + idx1);
                int p1_TR = *(int*)(pSrcRow1 + idx1 + srcBytes);
                int p1_BL = *(int*)(pSrcRow2 + idx1);
                int p1_BR = *(int*)(pSrcRow2 + idx1 + srcBytes);

                // Pixel 0
                __m128i v_p0_T = _mm_cvtepu8_epi16(_mm_unpacklo_epi8(_mm_cvtsi32_si128(p0_TL), _mm_cvtsi32_si128(p0_TR))); 
                __m128i v_top0 = _mm_madd_epi16(v_p0_T, v_wx0); 

                __m128i v_p0_B = _mm_cvtepu8_epi16(_mm_unpacklo_epi8(_mm_cvtsi32_si128(p0_BL), _mm_cvtsi32_si128(p0_BR)));
                __m128i v_btm0 = _mm_madd_epi16(v_p0_B, v_wx0);

                __m128i v_res0 = _mm_add_epi32(_mm_mullo_epi32(v_top0, v_inv_wy), _mm_mullo_epi32(v_btm0, v_wy));
                v_res0 = _mm_srai_epi32(v_res0, PRECISION_BITS * 2);
                
                __m128i v_out0 = _mm_packus_epi16(_mm_packus_epi32(v_res0, _mm_setzero_si128()), _mm_setzero_si128());
                int val0 = _mm_cvtsi128_si32(v_out0);
                
                *(short*)(pDstRow + dstX * dstBytes) = (short)val0; 
                *(pDstRow + dstX * dstBytes + 2) = (BYTE)(val0 >> 16); 

                // Pixel 1
                __m128i v_p1_T = _mm_cvtepu8_epi16(_mm_unpacklo_epi8(_mm_cvtsi32_si128(p1_TL), _mm_cvtsi32_si128(p1_TR)));
                __m128i v_top1 = _mm_madd_epi16(v_p1_T, v_wx1);
                
                __m128i v_p1_B = _mm_cvtepu8_epi16(_mm_unpacklo_epi8(_mm_cvtsi32_si128(p1_BL), _mm_cvtsi32_si128(p1_BR)));
                __m128i v_btm1 = _mm_madd_epi16(v_p1_B, v_wx1);

                __m128i v_res1 = _mm_add_epi32(_mm_mullo_epi32(v_top1, v_inv_wy), _mm_mullo_epi32(v_btm1, v_wy));
                v_res1 = _mm_srai_epi32(v_res1, PRECISION_BITS * 2);

                __m128i v_out1 = _mm_packus_epi16(_mm_packus_epi32(v_res1, _mm_setzero_si128()), _mm_setzero_si128());
                int val1 = _mm_cvtsi128_si32(v_out1);
                
                int dstX1 = dstX + 1;
                *(short*)(pDstRow + dstX1 * dstBytes) = (short)val1;
                *(pDstRow + dstX1 * dstBytes + 2) = (BYTE)(val1 >> 16);
                
                continue; 

                ScalarFallback_Thread:
                for (int k = 0; k < 2; k++) { 
                    int cx = x + k;
                    if (cx >= actualW) break;

                    int dstX_scalar = cx + offX;
                    if (dstX_scalar < 0 || dstX_scalar >= dstW) continue;

                    int idx = lutIndices[cx];
                    int w_x_s = lutWeights[cx * 2 + 1];
                    int inv_w_x_s = lutWeights[cx * 2 + 0];

                    const BYTE* s1 = pSrcRow1 + idx;
                    const BYTE* s2 = pSrcRow2 + idx;
                    
                    for (int c = 0; c < 3; c++) {
                        int top = (s1[c] * inv_w_x_s + s1[c + srcBytes] * w_x_s) >> PRECISION_BITS;
                        int bottom = (s2[c] * inv_w_x_s + s2[c + srcBytes] * w_x_s) >> PRECISION_BITS;
                        int final_val = (top * inv_w_y + bottom * w_y) >> PRECISION_BITS;
                        pDstRow[dstX_scalar * dstBytes + c] = (BYTE)final_val;
                    }
                }
            }

            // Tail loop
            for (; x < actualW; x++) {
                int dstX = x + offX;
                if (dstX < 0 || dstX >= dstW) continue;

                int idx = lutIndices[x];
                int w_x = lutWeights[x * 2 + 1];
                int inv_w_x = lutWeights[x * 2 + 0];

                const BYTE* s1 = pSrcRow1 + idx;
                const BYTE* s2 = pSrcRow2 + idx;

                for (int c = 0; c < 3; c++) {
                    int top = (s1[c] * inv_w_x + s1[c + srcBytes] * w_x) >> PRECISION_BITS;
                    int bottom = (s2[c] * inv_w_x + s2[c + srcBytes] * w_x) >> PRECISION_BITS;
                    int final_val = (top * inv_w_y + bottom * w_y) >> PRECISION_BITS;
                    pDstRow[dstX * dstBytes + c] = (BYTE)final_val;
                }
            }
        }
    }); // End ParallelFor
}

void LR2BGAImageProc::ResizeBilinear_CppOpt(
    const BYTE* pSrc, int srcW, int srcH, int srcStride, int srcBpp,
    BYTE* pDst, int dstW, int dstH, int dstStride, int dstBpp,
    int actualW, int actualH, int offX, int offY,
    const RECT* pSrcRect, std::vector<int>& lutIndices, std::vector<short>& lutWeights)
{
    int srcBytes = srcBpp / 8;
    int dstBytes = dstBpp / 8;

    RECT rect = { 0, 0, srcW, srcH };
    if (pSrcRect) rect = *pSrcRect;
    int srcRectW = rect.right - rect.left;
    int srcRectH = rect.bottom - rect.top;

    // Validation
    if (srcRectW <= 0 || srcRectH <= 0 || actualW <= 0 || actualH <= 0) return;

    // Fixed-point precision (11 bits = 2048)
    const int PRECISION_BITS = 11;
    const int PRECISION_SCALE = 1 << PRECISION_BITS;

    // Pre-calculate X indices and weights
    if (lutIndices.size() < (size_t)actualW) lutIndices.resize(actualW);
    // CppOpt uses 1 weight per pixel, but vector might be reused by SSE41 (2 weights).
    // We only need actualW size here.
    if (lutWeights.size() < (size_t)actualW) lutWeights.resize(actualW);

    float scaleX = (float)(srcRectW - 1) / actualW;
    if (actualW <= 1) scaleX = 0;

    for (int x = 0; x < actualW; x++) {
        float fx = x * scaleX;
        int x1 = rect.left + (int)fx;
        // Clamp
        if (x1 >= rect.right - 1) x1 = rect.right - 2; 
        if (x1 < rect.left) x1 = rect.left;
        
        lutIndices[x] = x1 * srcBytes;
        
        float dx = fx - (int)fx;
        lutWeights[x] = (short)(dx * PRECISION_SCALE);
    }

    float scaleY = (float)(srcRectH - 1) / actualH;
    if (actualH <= 1) scaleY = 0;

    for (int y = 0; y < actualH; y++) {
        int dstY = y + offY;
        if (dstY < 0 || dstY >= dstH) continue;

        float fy = y * scaleY;
        int y1 = rect.top + (int)fy;
        if (y1 >= rect.bottom - 1) y1 = rect.bottom - 2;
        if (y1 < rect.top) y1 = rect.top;
        
        float dy = fy - (int)fy;
        int w_y = (int)(dy * PRECISION_SCALE); 
        int inv_w_y = PRECISION_SCALE - w_y;

        const BYTE* pSrcRow1 = pSrc + y1 * srcStride;
        const BYTE* pSrcRow2 = pSrc + (y1 + 1) * srcStride;
        BYTE* pDstRow = pDst + dstY * dstStride;

        for (int x = 0; x < actualW; x++) {
            int dstX = x + offX;
            if (dstX < 0 || dstX >= dstW) continue;

            int idx = lutIndices[x];
            int w_x = lutWeights[x];
            int inv_w_x = PRECISION_SCALE - w_x;

            const BYTE* s1 = pSrcRow1 + idx;
            const BYTE* s2 = pSrcRow2 + idx;
            
            // RGB Loop
            for (int c = 0; c < 3; c++) {
                // Top row interpolation
                // val = (val1 * inv_w_x + val2 * w_x)
                int top = (s1[c] * inv_w_x + s1[c + srcBytes] * w_x) >> PRECISION_BITS;
                int bottom = (s2[c] * inv_w_x + s2[c + srcBytes] * w_x) >> PRECISION_BITS;

                // Combine Y
                int final_val = (top * inv_w_y + bottom * w_y) >> PRECISION_BITS;

                pDstRow[dstX * dstBytes + c] = (BYTE)final_val;
            }
        }
    }
}

// ------------------------------------------------------------------------------
// ApplyBrightness
// RGB24バッファに対して、指定された明るさ係数（0-100%）を適用します。
// 処理はインプレース（入力バッファを直接書き換え）で行われます。
// ------------------------------------------------------------------------------
void LR2BGAImageProc::ApplyBrightness(BYTE* pData, int width, int height, int stride, int brightness)
{
    if (brightness >= 100) return; // No change
    if (brightness <= 0) {
        // Blackout
        // Check if pitch is tight
        if (stride == width * 3) {
            ZeroMemory(pData, stride * height);
        } else {
            for (int y = 0; y < height; y++) {
                ZeroMemory(pData + y * stride, width * 3);
            }
        }
        return;
    }

    // Lookup table for speed (0-255 -> 0-255)
    BYTE lut[256];
    for (int i = 0; i < 256; i++) {
        lut[i] = (BYTE)(i * brightness / 100);
    }

    for (int y = 0; y < height; y++) {
        BYTE* pRow = pData + y * stride;
        for (int x = 0; x < width * 3; x++) {
            pRow[x] = lut[pRow[x]];
        }
    }
}


