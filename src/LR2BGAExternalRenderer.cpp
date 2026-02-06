// --------------------------------------------------------------------------------------
// LR2BGAExternalRenderer.cpp
// LR2 BGA Filter - 外部ウィンドウ描画エンジン 実装
// --------------------------------------------------------------------------------------

#include "LR2BGAExternalRenderer.h"
#include "LR2BGAImageProc.h"

// --------------------------------------------------------------------------------------
// コンストラクタ / デストラクタ
// --------------------------------------------------------------------------------------
LR2BGAExternalRenderer::LR2BGAExternalRenderer(LR2BGASettings* pSettings)
    : m_pSettings(pSettings)
    , m_bufWidth(0)
    , m_bufHeight(0)
    , m_bufStride(0)
{
}

LR2BGAExternalRenderer::~LR2BGAExternalRenderer()
{
    ClearBuffer();
}

// --------------------------------------------------------------------------------------
// UpdateFrame - ソースフレームをリサイズしてバッファに格納
// --------------------------------------------------------------------------------------
void LR2BGAExternalRenderer::UpdateFrame(const BYTE* pSrcData, int srcWidth, int srcHeight,
                                         int srcStride, int srcBitCount, const RECT* pSrcRect,
                                         HWND hExtWnd)
{
    if (!hExtWnd || !IsWindow(hExtWnd)) return;

    // 設定のスナップショットを取得
    LR2BGASettings::ExtWindowConfig cfg;
    m_pSettings->GetExtWindowConfig(cfg);

    int targetWidth = cfg.width;
    int targetHeight = cfg.height;

    if (cfg.passthrough) {
        // パススルー時：クロップサイズまたはソースサイズを使用
        if (pSrcRect) {
            targetWidth = pSrcRect->right - pSrcRect->left;
            targetHeight = pSrcRect->bottom - pSrcRect->top;
        } else {
            targetWidth = srcWidth;
            targetHeight = srcHeight;
        }
    }

    // バッファサイズ計算
    int dstStride = ((targetWidth * 3 + 3) & ~3);
    int dstSize = dstStride * targetHeight;

    // バッファサイズ変更 (排他制御)
    {
        std::lock_guard<std::mutex> lock(m_mtx);
        if ((int)m_buffer.size() != dstSize ||
            m_bufWidth != targetWidth ||
            m_bufHeight != targetHeight) {
            m_buffer.resize(dstSize);
            m_bufWidth = targetWidth;
            m_bufHeight = targetHeight;
            m_bufStride = dstStride;
        }
    }

    // パススルー時のウィンドウサイズ強制更新
    // デッドロック回避のため、ロック外で実行
    if (cfg.passthrough && hExtWnd) {
        RECT rc;
        GetWindowRect(hExtWnd, &rc);
        int currentW = rc.right - rc.left;
        int currentH = rc.bottom - rc.top;
        if (currentW != targetWidth || currentH != targetHeight) {
            SetWindowPos(hExtWnd, NULL, 0, 0, targetWidth, targetHeight,
                SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }

    // リサイズ/コピー処理
    {
        std::lock_guard<std::mutex> lock(m_mtx);

        // クロップ範囲の考慮
        int effectiveSrcW = srcWidth;
        int effectiveSrcH = srcHeight;
        if (pSrcRect) {
            effectiveSrcW = pSrcRect->right - pSrcRect->left;
            effectiveSrcH = pSrcRect->bottom - pSrcRect->top;
        }

        // 描画サイズとオフセット計算
        int outWidth, outHeight, offsetX, offsetY;

        if (cfg.passthrough) {
            outWidth = effectiveSrcW;
            outHeight = effectiveSrcH;
            offsetX = 0;
            offsetY = 0;
        } else {
            LR2BGAImageProc::CalculateResizeDimensions(
                effectiveSrcW, effectiveSrcH, targetWidth, targetHeight,
                cfg.keepAspect,
                outWidth, outHeight, offsetX, offsetY);
        }

        // 背景クリア (レターボックス用)
        if (outWidth < targetWidth || outHeight < targetHeight) {
            memset(m_buffer.data(), 0, m_buffer.size());
        }

        // リサイズ実行
        if (cfg.algo == RESIZE_NEAREST) {
            LR2BGAImageProc::ResizeNearestNeighbor(
                pSrcData, srcWidth, srcHeight, srcStride, srcBitCount,
                m_buffer.data(), targetWidth, targetHeight, dstStride, 24,
                outWidth, outHeight, offsetX, offsetY, pSrcRect, m_lutXIndices);
        } else {
            LR2BGAImageProc::ResizeBilinear(
                pSrcData, srcWidth, srcHeight, srcStride, srcBitCount,
                m_buffer.data(), targetWidth, targetHeight, dstStride, 24,
                outWidth, outHeight, offsetX, offsetY, pSrcRect, m_lutXIndices, m_lutXWeights);
        }
    }

    // ウィンドウ再描画要求
    InvalidateRect(hExtWnd, NULL, FALSE);
}

// --------------------------------------------------------------------------------------
// UpdateOverlay - 輝度調整用オーバーレイの透明度を更新
// --------------------------------------------------------------------------------------
void LR2BGAExternalRenderer::UpdateOverlay(HWND hOverlayWnd, HWND hExtWnd)
{
    if (!hOverlayWnd || !IsWindow(hOverlayWnd)) return;

    // 不透明度計算: 0 (透明) ～ 255 (不透明)
    // Brightness: 100 (最大輝度) -> Alpha 0 (透明)
    // Brightness: 0 (真っ黒) -> Alpha 255 (完全黒オーバーレイ)
    int alpha = 255 - (m_pSettings->m_brightnessExt * 255 / 100);
    if (alpha < 0) alpha = 0;
    if (alpha > 255) alpha = 255;

    SetLayeredWindowAttributes(hOverlayWnd, 0, (BYTE)alpha, LWA_ALPHA);

    // 位置同期
    if (hExtWnd && IsWindow(hExtWnd)) {
        RECT rect;
        GetWindowRect(hExtWnd, &rect);

        HWND hWndInsertAfter = m_pSettings->m_extWindowTopmost ? HWND_TOPMOST : HWND_NOTOPMOST;
        SetWindowPos(hOverlayWnd, hWndInsertAfter,
            rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
            SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_SHOWWINDOW);
    }
}

// --------------------------------------------------------------------------------------
// UpdateWindowPos - ウィンドウの位置・サイズ・Topmost設定を適用
// --------------------------------------------------------------------------------------
void LR2BGAExternalRenderer::UpdateWindowPos(HWND hExtWnd, HWND hOverlayWnd)
{
    if (!hExtWnd || !IsWindow(hExtWnd)) return;

    m_pSettings->Lock();
    int x = m_pSettings->m_extWindowX;
    int y = m_pSettings->m_extWindowY;
    int w = m_pSettings->m_extWindowWidth;
    int h = m_pSettings->m_extWindowHeight;
    BOOL topmost = m_pSettings->m_extWindowTopmost;
    m_pSettings->Unlock();

    HWND hWndInsertAfter = topmost ? HWND_TOPMOST : HWND_NOTOPMOST;

    SetWindowPos(hExtWnd, hWndInsertAfter, x, y, w, h, SWP_NOACTIVATE);

    // オーバーレイも同期
    if (hOverlayWnd && IsWindow(hOverlayWnd)) {
        SetWindowPos(hOverlayWnd, hWndInsertAfter, x, y, w, h,
            SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    }
}

// --------------------------------------------------------------------------------------
// Paint - WM_PAINT から呼び出される描画処理
// --------------------------------------------------------------------------------------
void LR2BGAExternalRenderer::Paint(HDC hdc, HWND hwnd)
{
    std::lock_guard<std::mutex> lock(m_mtx);

    if (m_buffer.size() > 0 && m_bufWidth > 0 && m_bufHeight > 0) {
        BITMAPINFO bmi = {0};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = m_bufWidth;
        bmi.bmiHeader.biHeight = m_bufHeight;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 24;
        bmi.bmiHeader.biCompression = BI_RGB;

        RECT rect;
        GetClientRect(hwnd, &rect);
        SetStretchBltMode(hdc, COLORONCOLOR);
        StretchDIBits(hdc, 0, 0, rect.right, rect.bottom,
            0, 0, m_bufWidth, m_bufHeight,
            m_buffer.data(), &bmi, DIB_RGB_COLORS, SRCCOPY);
    } else {
        // バッファがまだ無い場合は黒背景
        RECT rect;
        GetClientRect(hwnd, &rect);
        FillRect(hdc, &rect, (HBRUSH)GetStockObject(BLACK_BRUSH));
    }
}

// --------------------------------------------------------------------------------------
// ClearBuffer - バッファクリア
// --------------------------------------------------------------------------------------
void LR2BGAExternalRenderer::ClearBuffer()
{
    std::lock_guard<std::mutex> lock(m_mtx);
    m_buffer.clear();
    m_bufWidth = 0;
    m_bufHeight = 0;
    m_bufStride = 0;
    m_lutXIndices.clear();
    m_lutXWeights.clear();
}
