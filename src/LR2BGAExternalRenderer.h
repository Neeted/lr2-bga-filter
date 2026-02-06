// --------------------------------------------------------------------------------------
// LR2BGAExternalRenderer.h
// LR2 BGA Filter - 外部ウィンドウ描画エンジン
// --------------------------------------------------------------------------------------
//
// 概要:
//   外部ウィンドウへの映像描画ロジックを担当するクラスです。
//   LR2BGAWindow から描画責務を分離し、テスト可能な形で設計しています。
//
// 責務:
//   1. フレームバッファ管理: リサイズ済み画像データの保持
//   2. リサイズ処理: LR2BGAImageProc を使用した画像変換
//   3. GDI描画: StretchDIBits を使用したウィンドウ描画
//   4. オーバーレイ更新: 輝度調整用半透明レイヤーの制御
//
// 注意:
//   このクラスは HWND を所有しません。ウィンドウ生成・破棄は LR2BGAWindow が担当します。
// --------------------------------------------------------------------------------------
#pragma once

#include <windows.h>
#include <vector>
#include <mutex>
#include "LR2BGASettings.h"

// --------------------------------------------------------------------------------------
// LR2BGAExternalRenderer クラス
// --------------------------------------------------------------------------------------
class LR2BGAExternalRenderer {
public:
    LR2BGAExternalRenderer(LR2BGASettings* pSettings);
    ~LR2BGAExternalRenderer();

    // --------------------------------------------------------------------------
    // フレーム更新
    // --------------------------------------------------------------------------
    // ソースフレームをリサイズしてバッファに格納
    void UpdateFrame(const BYTE* pSrcData, int srcWidth, int srcHeight,
                     int srcStride, int srcBitCount, const RECT* pSrcRect,
                     HWND hExtWnd);

    // --------------------------------------------------------------------------
    // オーバーレイ・位置更新
    // --------------------------------------------------------------------------
    // 輝度調整用オーバーレイの透明度を更新
    void UpdateOverlay(HWND hOverlayWnd, HWND hExtWnd);
    // ウィンドウの位置・サイズ・Topmost設定を適用
    void UpdateWindowPos(HWND hExtWnd, HWND hOverlayWnd);

    // --------------------------------------------------------------------------
    // GDI描画
    // --------------------------------------------------------------------------
    // WM_PAINT から呼び出される描画処理
    void Paint(HDC hdc, HWND hwnd);

    // --------------------------------------------------------------------------
    // バッファクリア
    // --------------------------------------------------------------------------
    void ClearBuffer();

private:
    LR2BGASettings* m_pSettings;

    // 描画バッファ
    std::vector<BYTE> m_buffer;
    int m_bufWidth;
    int m_bufHeight;
    int m_bufStride;
    std::mutex m_mtx;

    // リサイズ用LUT (キャッシュ)
    std::vector<int> m_lutXIndices;
    std::vector<short> m_lutXWeights;
};
