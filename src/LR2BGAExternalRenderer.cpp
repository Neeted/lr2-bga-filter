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
    , m_lastAppliedWndWidth(0)
    , m_lastAppliedWndHeight(0)
    , m_hasAppliedWndSize(false)
    , m_repaintQueued(false)
    , m_cachedBmiWidth(0)
    , m_cachedBmiHeight(0)
    , m_hasCachedBmi(false)
{
    ZeroMemory(&m_cachedBmi, sizeof(m_cachedBmi));
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

    // パススルー時のウィンドウサイズ更新
    // 毎フレームのGetWindowRect/SetWindowPosはコストが高いため、
    // 直近適用サイズとの差分がある場合のみ適用する。
    if (cfg.passthrough && hExtWnd) {
        if (!m_hasAppliedWndSize ||
            m_lastAppliedWndWidth != targetWidth ||
            m_lastAppliedWndHeight != targetHeight) {
            SetWindowPos(hExtWnd, NULL, 0, 0, targetWidth, targetHeight,
                SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
            m_lastAppliedWndWidth = targetWidth;
            m_lastAppliedWndHeight = targetHeight;
            m_hasAppliedWndSize = true;
        }
    } else {
        m_hasAppliedWndSize = false;
    }

    // リサイズ/コピー処理（重い処理はロック外）
    std::vector<BYTE> nextBuffer;
    nextBuffer.resize(dstSize);

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
        memset(nextBuffer.data(), 0, nextBuffer.size());
    }

    // リサイズ実行
    if (cfg.algo == RESIZE_NEAREST) {
        LR2BGAImageProc::ResizeNearestNeighbor(
            pSrcData, srcWidth, srcHeight, srcStride, srcBitCount,
            nextBuffer.data(), targetWidth, targetHeight, dstStride, 24,
            outWidth, outHeight, offsetX, offsetY, pSrcRect, m_lutXIndices);
    } else {
        LR2BGAImageProc::ResizeBilinear(
            pSrcData, srcWidth, srcHeight, srcStride, srcBitCount,
            nextBuffer.data(), targetWidth, targetHeight, dstStride, 24,
            outWidth, outHeight, offsetX, offsetY, pSrcRect, m_lutXIndices, m_lutXWeights);
    }

    // 描画用バッファへ反映（短時間ロック）
    {
        std::lock_guard<std::mutex> lock(m_mtx);
        m_buffer.swap(nextBuffer);
        m_bufWidth = targetWidth;
        m_bufHeight = targetHeight;
        m_bufStride = dstStride;

        // BITMAPINFOキャッシュを更新
        if (!m_hasCachedBmi ||
            m_cachedBmiWidth != m_bufWidth ||
            m_cachedBmiHeight != m_bufHeight) {
            ZeroMemory(&m_cachedBmi, sizeof(m_cachedBmi));
            m_cachedBmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            m_cachedBmi.bmiHeader.biWidth = m_bufWidth;
            m_cachedBmi.bmiHeader.biHeight = m_bufHeight;
            m_cachedBmi.bmiHeader.biPlanes = 1;
            m_cachedBmi.bmiHeader.biBitCount = 24;
            m_cachedBmi.bmiHeader.biCompression = BI_RGB;
            m_cachedBmiWidth = m_bufWidth;
            m_cachedBmiHeight = m_bufHeight;
            m_hasCachedBmi = true;
        }
    }

    // ウィンドウ再描画要求
    // 未処理の再描画要求が既にある場合は投入を抑制する。
    if (!m_repaintQueued.exchange(true)) {
        InvalidateRect(hExtWnd, NULL, FALSE);
    }
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
    // WM_PAINT 受理時点で「未処理要求あり」は解消されたとみなす。
    // Paint実行中に新フレームが来た場合は UpdateFrame 側が再度 true にして再投入する。
    m_repaintQueued.store(false);

    std::lock_guard<std::mutex> lock(m_mtx);

    if (m_buffer.size() > 0 && m_bufWidth > 0 && m_bufHeight > 0) {
        RECT rect;
        GetClientRect(hwnd, &rect);
        int clientW = rect.right - rect.left;
        int clientH = rect.bottom - rect.top;

        if (m_hasCachedBmi && clientW == m_bufWidth && clientH == m_bufHeight) {
            // 伸縮不要な場合は SetDIBitsToDevice を使って軽量描画
            SetDIBitsToDevice(
                hdc, 0, 0, m_bufWidth, m_bufHeight,
                0, 0, 0, m_bufHeight,
                m_buffer.data(), &m_cachedBmi, DIB_RGB_COLORS);
        } else if (m_hasCachedBmi) {
            SetStretchBltMode(hdc, COLORONCOLOR);
            StretchDIBits(hdc, 0, 0, clientW, clientH,
                0, 0, m_bufWidth, m_bufHeight,
                m_buffer.data(), &m_cachedBmi, DIB_RGB_COLORS, SRCCOPY);
        }
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
    m_lastAppliedWndWidth = 0;
    m_lastAppliedWndHeight = 0;
    m_hasAppliedWndSize = false;
    m_repaintQueued.store(false);
    ZeroMemory(&m_cachedBmi, sizeof(m_cachedBmi));
    m_cachedBmiWidth = 0;
    m_cachedBmiHeight = 0;
    m_hasCachedBmi = false;
    m_lutXIndices.clear();
    m_lutXWeights.clear();
}
