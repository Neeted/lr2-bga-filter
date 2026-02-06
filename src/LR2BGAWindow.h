#pragma once
#include <windows.h>
#include <vector>
#include <string>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <atomic>
#include <memory>
#include <streams.h>
#include <dvdmedia.h> // For VIDEOINFOHEADER
#include "LR2BGASettings.h"
#include "LR2BGALetterboxDetector.h" // For LetterboxDebugInfo
#include "LR2BGAExternalRenderer.h"

//------------------------------------------------------------------------------
// クラス: LR2BGAWindow
// 
// 概要:
//   LR2 (Lunatic Rave 2) の外部ウィンドウ、オーバーレイ、デバッグウィンドウの
//   作成、管理、メッセージ処理を担当するクラスです。
//   Windows API (Win32 API) を直接使用してウィンドウを制御します。
//
// 主な責務:
//   1. 外部ビューアウィンドウ (External Window) の管理
//      - BGA映像を別ウィンドウに表示する機能。
//      - ウィンドウの位置、サイズ、アスペクト比の維持。
//      - キーボード/ゲームパッド入力によるウィンドウ閉鎖機能。
//
//   2. オーバーレイウィンドウ (Overlay Window) の管理
//      - 外部ビューアウィンドウの上に明るさ調整用のオーバーレイを表示する機能。
//
//   3. デバッグウィンドウ (Debug Window) の管理
//      - フィルタの動作状況、FPS、ドロップフレーム数、黒帯判定の詳細などを表示。
//      - 開発およびトラブルシューティング用。
//
// 設計詳細:
//   - 各ウィンドウは個別のスレッドではなく、フィルタスレッドまたはメッセージポンプを持つスレッドで動作します。
//     (注意: DirectShowフィルタ内でのウィンドウメッセージ処理はブロッキングに注意が必要)
//   - 設定マネージャ (ILR2BGAFilterSettings) への参照を持ち、設定変更を反映します。
//
// スレッド安全性と同期設計:
//   - 本クラスは複数のスレッドからアクセスされます（DirectShowフィルタスレッド、ウィンドウメッセージスレッド、入力監視スレッド）。
//   - 競合を防ぐため、共有リソース（描画バッファ、デバッグ情報、設定等）へのアクセスは `std::mutex` で保護しています。
//   - ウィンドウハンドル (`HWND`) の操作は、原則としてそのウィンドウを作成したスレッド内で行う必要があります（`PostMessage` による非同期制御を推奨）。
//
//------------------------------------------------------------------------------
class LR2BGAWindow {
public:
    LR2BGAWindow(LR2BGASettings* pSettings);
    ~LR2BGAWindow();

    // 外部ウィンドウ管理
    void ShowExternalWindow();      // 外部ウィンドウを作成・表示
    void CloseExternalWindow();     // 外部ウィンドウを破棄
    // 外部ウィンドウへの映像更新（描画）
    void UpdateExternalWindow(const BYTE* pSrcData, int srcWidth, int srcHeight, int srcStride, int srcBitCount, const RECT* pSrcRect = NULL);
    void UpdateExternalWindowPos(); // ウィンドウ位置・サイズ・Topmost設定の反映
    void UpdateOverlayWindow();     // オーバーレイ（明るさ調整用黒レイヤー）の更新
    
    // 手動クローズ処理
    void OnManualClose();
    
    // デバッグウィンドウ管理
    void ShowDebugWindow();         // デバッグウィンドウを表示
    // デバッグ情報の更新（テキスト生成と描画）
    void UpdateDebugInfo(
        const std::wstring& inputFilter, const std::wstring& outputFilter,
        const std::wstring& filterGraphInfo,
        int inputWidth, int inputHeight, int inputBitCount,
        int outputWidth, int outputHeight,
        double frameRate, double outputFrameRate,
        long long frameCount, long long droppedFrames,
        double avgTime,
        const LetterboxDebugInfo& lbInfo);

    // LR2連携: LR2本体ウィンドウへのフォーカス復帰
    void FocusLR2Window();

private:
    static LRESULT CALLBACK ExtWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    void ExtWindowThread(); // Member function
    static LRESULT CALLBACK DebugWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    void DebugWindowThread(); // Member function
    
    // 入力監視スレッド制御
    void StartInputMonitor();
    void StopInputMonitor();
    void InputMonitorThread(); // Member function
    
public:
    void RestartInputMonitor(); // 設定変更時に入力監視を再起動
    
public:
    // プロパティページ連携
    void SetFilter(IUnknown* pFilterUnk);       // プロパティページ表示用にフィルタインターフェースを保持
    void ShowPropertyPage();                    // プロパティページをモーダル表示
    void PropertyPageThread(); // Member function
    void FocusRestoreThread(); // Member function (assuming it needs access to this)

private:
    void FormatExtWindowInfo(wchar_t* buffer, size_t size);
    void FormatFPSLimit(wchar_t* buffer, size_t size);
    void FormatInputStatus(wchar_t* gamePadStatus, size_t gamePadSize, wchar_t* keyStatus, size_t keySize);
    void FormatLetterboxInfo(wchar_t* buffer, size_t size, const LetterboxDebugInfo& lbInfo);

public:
    LR2BGASettings* m_pSettings;

    // 外部ウィンドウ関連
    HWND m_hExtWnd;
    HWND m_hOverlayWnd;         // 明るさ調整用オーバーレイウィンドウ (黒色半透明)
    std::unique_ptr<LR2BGAExternalRenderer> m_pRenderer;  // 描画エンジン

    std::thread m_threadExt;        // ウインドウスレッド
    std::thread m_threadInput;      // ゲームパッド/キーボード入力監視スレッド
    std::condition_variable m_cvInput; // 入力監視スレッド停止用CV
    std::mutex m_mtxInput;          // 入力監視CV用Mutex
    bool m_bInputStop;              // 入力監視停止フラグ
    //--------------------------------------------------------------------------
    // ロック階層 (Lock Hierarchy) - デッドロック防止のためのルール
    //--------------------------------------------------------------------------
    // 複数のミューテックスを必要とする場合、必ず以下の順序で取得すること:
    //   1. m_mtxInput (入力監視スレッド制御)
    //   2. m_mtxDebug (デバッグテキストバッファ)
    //   3. Renderer内部のmutex (描画バッファ保護)
    //
    // 注意:
    //   - ロックを保持したまま GUI 操作 (SetWindowPos, SendMessage 等) を行わないこと。
    //   - ロックのスコープはできるだけ短くすること。
    //--------------------------------------------------------------------------

    // デバッグウィンドウ関連
    IUnknown* m_pFilterUnk;
    HWND m_hDebugWnd;
    HWND m_hBtnSettings;        // 「Open Settings」ボタンハンドル
    std::thread m_threadDebug;
    wchar_t m_debugText[2048];  // 表示用テキストバッファ
    std::mutex m_mtxDebug; // テキストバッファアクセス保護用

    std::atomic<bool> m_bPropPageActive; // プロパティページ表示中フラグ
    std::thread m_threadProp;       // プロパティページスレッド
    std::thread m_threadFocus;      // フォーカス復帰スレッド
    HWND m_hPropPageWnd;            // プロパティページウィンドウハンドル (強制クローズ用)
};

// Auto-link winmm.lib for joystick
#pragma comment(lib, "winmm.lib")


