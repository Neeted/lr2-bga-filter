#include "LR2BGAWindow.h"
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>

//------------------------------------------------------------------------------
// LR2BGAWindow.cpp
//
// 実装詳細:
//   このファイルは、LR2BGADirectShowフィルタに関連する全てのウィンドウ管理ロジックを実装します。
//   DirectShowフィルタは通常バックグラウンドで動作しますが、このクラスを通じて
//   ユーザーインターフェース（デバッグ情報や外部プレビュー）を提供します。
//
// 主要コンポーネント:
//   - External Window: 別のディスプレイや位置にBGAを表示するためのウィンドウ。
//     スレッド `ExtWindowThread` で独立してメッセージポンプを回します。
//   - Debug Window: 開発者向けの詳細情報を表示するウィンドウ。
//     スレッド `DebugWindowThread` で独立して動作します。
//   - Input Monitor: ウィンドウを閉じるための入力を監視するスレッド/ロジック。
//
// 技術的特記事項:
//   - ウィンドウプロシージャ (`ExtWndProc`, `DebugWndProc`) は静的メソッドとして実装され、
//     `GWLP_USERDATA` を使用して `LR2BGAWindow` インスタンスへメッセージを転送します。
//   - 描画処理は GDI (Graphics Device Interface) を使用して行われます。
//------------------------------------------------------------------------------
#include "LR2BGAImageProc.h"
#include "resource.h"
#include <tlhelp32.h>
#include <tchar.h>
#include <stdio.h>

static const wchar_t* EXT_WND_CLASS = L"LR2BGAFilterExtWnd";
static const wchar_t* OVERLAY_WND_CLASS = L"LR2BGAFilterOverlayWnd";
static const wchar_t* DEBUG_WND_CLASS = L"LR2BGAFilterDebugWnd";

//------------------------------------------------------------------------------
// 定数定義 (Constants)
// マジックナンバーを避け、意図を明確にするための名前付き定数
//------------------------------------------------------------------------------
constexpr DWORD kInputMonitorIntervalMs = 50;   // 入力監視のポーリング間隔 (ms)
constexpr DWORD kFocusRestoreDelayMs = 100;     // フォーカス復帰待機時間 (ms)
constexpr int kDebugWindowButtonMargin = 50;    // デバッグウィンドウのボタン領域高さ (px)

// Defined in LR2BGAFilter.h/cpp, but we declare it here to avoid circular include issues
EXTERN_C const GUID CLSID_LR2BGAFilterPropertyPage;

LR2BGAWindow::LR2BGAWindow(LR2BGASettings* pSettings)
    : m_pSettings(pSettings)
    , m_hExtWnd(NULL)
    , m_hOverlayWnd(NULL)
    , m_hDebugWnd(NULL)
    , m_hBtnSettings(NULL)

    , m_pFilterUnk(NULL)
{
    // Mutexes don't need explicit initialization
    m_debugText[0] = L'\0';
    m_bInputStop = false;
    m_bPropPageActive = false;
}

LR2BGAWindow::~LR2BGAWindow()
{
    CloseExternalWindow();
    
    if (m_hDebugWnd && IsWindow(m_hDebugWnd)) {
        SendMessage(m_hDebugWnd, WM_CLOSE, 0, 0);
        if (m_threadDebug.joinable()) m_threadDebug.join();
    }

    // プロパティページスレッドのクリーンアップ
    if (m_threadProp.joinable()) {
        // プロパティページはOLEによって管理されるモーダルダイアログであり、
        // OleCreatePropertyFrame はダイアログが閉じられるまでブロックします。
        // デストラクタ内で join() を呼ぶと、ユーザーがダイアログを閉じるまで
        // アプリケーション終了がブロックされてしまうため、detach() して
        // スレッドを切り離します（OSプロセス終了時のクリーンアップに委ねる）。
        m_threadProp.detach();
    }

    StopInputMonitor(); // Stop Monitor
    
    if (m_threadInput.joinable()) m_threadInput.join();

    // Mutexes destroy automatically
}

//------------------------------------------------------------------------------
// 外部ウィンドウの実装 (External Window Implementation)
//------------------------------------------------------------------------------

void LR2BGAWindow::ShowExternalWindow()
{
    if (!m_pSettings->m_extWindowEnabled) return;
    
    // 既にウィンドウハンドルが存在し、有効な場合は表示状態にするのみ
    if (m_hExtWnd && IsWindow(m_hExtWnd)) {
        ShowWindow(m_hExtWnd, SW_SHOW);
        return;
    }
    
    // 外部ウィンドウ作成スレッドを起動
    // 以前のスレッドが残っている場合は終了を待機 (join) してから新規作成します。
    if (m_threadExt.joinable()) m_threadExt.join();
    m_threadExt = std::thread(&LR2BGAWindow::ExtWindowThread, this);
    
    // 入力監視（ゲームパッド/キーによるクローズ）を開始
    StartInputMonitor();

    // 注: ウィンドウ作成は ExtWindowThread 内で行われ、自動的に表示されます。
}

void LR2BGAWindow::CloseExternalWindow()
{
    // ウィンドウを安全に閉じる処理
    // ウィンドウを安全に閉じる処理
    if (m_hExtWnd && IsWindow(m_hExtWnd)) {
        // WM_CLOSE を送信してスレッドのメッセージループを終了させる
        PostMessage(m_hExtWnd, WM_CLOSE, 0, 0);
    }
    
    // スレッドの終了を待機 (join)
    // ウィンドウハンドルが既に無効（手動クローズ済み）であっても、
    // スレッドオブジェクトが joinable な状態であれば join する必要があります。
    // さもないと std::thread のデストラクタでクラッシュします。
    if (m_threadExt.joinable()) {
        m_threadExt.join();
    }
    
    m_hExtWnd = NULL;
    
    // 入力監視も停止
    StopInputMonitor();
    
    // オーバーレイウィンドウの破棄 (通常は親ウィンドウと共に破棄されるが念のため)
    if (m_hOverlayWnd && IsWindow(m_hOverlayWnd)) {
        DestroyWindow(m_hOverlayWnd);
        m_hOverlayWnd = NULL;
    }
}

// オーバーレイウィンドウ（輝度調整用）の更新
void LR2BGAWindow::UpdateOverlayWindow()
{
    if (!m_hOverlayWnd || !IsWindow(m_hOverlayWnd)) return;

    // 不透明度計算: 0 (透明) ～ 255 (不透明)
    // Brightness: 100 (最大輝度) -> Alpha 0 (透明)
    // Brightness: 0 (真っ黒) -> Alpha 255 (完全黒オーバーレイ)
    int alpha = 255 - (m_pSettings->m_brightnessExt * 255 / 100);
    if (alpha < 0) alpha = 0;
    if (alpha > 255) alpha = 255;

    SetLayeredWindowAttributes(m_hOverlayWnd, 0, (BYTE)alpha, LWA_ALPHA);
    
    // 位置同期
    if (m_hExtWnd && IsWindow(m_hExtWnd)) {
        RECT rect;
        GetWindowRect(m_hExtWnd, &rect);
        
        // オーバーレイのZオーダー制御
        // ExtWindowがTopmostならOverlayもTopmost扱いにする必要があるが、
        // OverlayはPopupウィンドウとしてExtWnd所有にしているため、
        // 基本的に所有者のZオーダーに追従する。
        // ここでは位置とサイズのみ同期し、NOACTIVATEとNOOWNERZORDERを指定する。
        
        HWND hWndInsertAfter = m_pSettings->m_extWindowTopmost ? HWND_TOPMOST : HWND_NOTOPMOST;
        SetWindowPos(m_hOverlayWnd, hWndInsertAfter, 
            rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, 
            SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_SHOWWINDOW); 
    }
}

void LR2BGAWindow::UpdateExternalWindow(const BYTE* pSrcData, int srcWidth, int srcHeight, int srcStride, int srcBitCount, const RECT* pSrcRect)
{
    if (!m_hExtWnd || !IsWindow(m_hExtWnd)) return;

    // Get thread-safe snapshot of settings
    LR2BGASettings::ExtWindowConfig cfg;
    m_pSettings->GetExtWindowConfig(cfg);
    
    int targetWidth = cfg.width;
    int targetHeight = cfg.height;
    
    if (cfg.passthrough) {
        // In passthrough, we use the cropped size if cropped
        if (pSrcRect) {
            targetWidth = pSrcRect->right - pSrcRect->left;
            targetHeight = pSrcRect->bottom - pSrcRect->top;
        } else {
            targetWidth = srcWidth;
            targetHeight = srcHeight;
        }
    }
    
    // Check buffer size
    int dstStride = ((targetWidth * 3 + 3) & ~3);
    int dstSize = dstStride * targetHeight;
    
    // バッファサイズ変更 (排他制御が必要)
    {
        std::lock_guard<std::mutex> lock(m_mtxExtWindow);
        if (m_extWindowBuffer.size() != dstSize || 
            m_extWindowBufWidth != targetWidth || 
            m_extWindowBufHeight != targetHeight) {
            m_extWindowBuffer.resize(dstSize);
            m_extWindowBufWidth = targetWidth;
            m_extWindowBufHeight = targetHeight;
        }
    }
    
    // パススルー時のウィンドウ強制サイズ更新
    // デッドロック回避のため、ロックを外してから実行する
    // (SetWindowPos -> WM_WINDOWPOSCHANGED -> WM_PAINT -> lock(m_mtxExtWindow) の順で呼ばれる可能性があるため)
    if (cfg.passthrough && m_hExtWnd) {
        RECT rc;
        GetWindowRect(m_hExtWnd, &rc);
        int currentW = rc.right - rc.left;
        int currentH = rc.bottom - rc.top;
        if (currentW != targetWidth || currentH != targetHeight) {
             SetWindowPos(m_hExtWnd, NULL, 0, 0, targetWidth, targetHeight, 
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }
    
    // リサイズ/コピー処理を行うため再度ロック
    {
        std::lock_guard<std::mutex> lock(m_mtxExtWindow);
        
        // クロップ範囲（Source Rect）の考慮
        int effectiveSrcW = srcWidth;
        int effectiveSrcH = srcHeight;
        if (pSrcRect) {
            effectiveSrcW = pSrcRect->right - pSrcRect->left;
            effectiveSrcH = pSrcRect->bottom - pSrcRect->top;
        }
        
        // 描画サイズとオフセット計算
        int outWidth, outHeight, offsetX, offsetY;
        
        if (cfg.passthrough) {
            // パススルーモード：ソースサイズをそのまま使用
            outWidth = effectiveSrcW;
            outHeight = effectiveSrcH;
            offsetX = 0;
            offsetY = 0;
        } else {
            // アスペクト比維持の計算
            LR2BGAImageProc::CalculateResizeDimensions(
                effectiveSrcW, effectiveSrcH, targetWidth, targetHeight,
                cfg.keepAspect,
                outWidth, outHeight, offsetX, offsetY);
        }
        
        // 背景のクリア (レターボックス用)
        // 描画領域がウィンドウ全体より小さい場合、余白を黒で塗りつぶす
        if (outWidth < targetWidth || outHeight < targetHeight) {
            memset(m_extWindowBuffer.data(), 0, m_extWindowBuffer.size());
        }
            
        // リサイズ実行
        if (cfg.algo == RESIZE_NEAREST) {
            LR2BGAImageProc::ResizeNearestNeighbor(
                pSrcData, srcWidth, srcHeight, srcStride, srcBitCount,
                m_extWindowBuffer.data(), targetWidth, targetHeight, dstStride, 24,
                outWidth, outHeight, offsetX, offsetY, pSrcRect, m_extLutXIndices);
        } else {
            LR2BGAImageProc::ResizeBilinear(
                pSrcData, srcWidth, srcHeight, srcStride, srcBitCount,
                m_extWindowBuffer.data(), targetWidth, targetHeight, dstStride, 24,
                outWidth, outHeight, offsetX, offsetY, pSrcRect, m_extLutXIndices, m_extLutXWeights);
        }
    } // lock scope end  
    // ウィンドウ再描画要求
    InvalidateRect(m_hExtWnd, NULL, FALSE);
}

// 外部ウィンドウの位置・サイズ・最前面設定を更新
void LR2BGAWindow::UpdateExternalWindowPos()
{
    if (!m_hExtWnd || !IsWindow(m_hExtWnd)) return;

    // 設定のスナップショットをスレッドセーフに取得
    LR2BGASettings::ExtWindowConfig cfg;
    m_pSettings->GetExtWindowConfig(cfg);

    // 位置、サイズ、最前面設定の更新
    int x = cfg.x;
    int y = cfg.y;
    int w = cfg.width;
    int h = cfg.height;
    HWND hWndInsertAfter = cfg.topmost ? HWND_TOPMOST : HWND_NOTOPMOST;

    UINT uFlags = SWP_NOACTIVATE | SWP_NOOWNERZORDER;
    
    // パススルーモード有効時は設定画面のサイズ（Resize用）を無視する
    // サイズは UpdateExternalWindow で実際の映像サイズに合わせて制御されるため
    if (cfg.passthrough) {
        uFlags |= SWP_NOSIZE;
    }

    SetWindowPos(m_hExtWnd, hWndInsertAfter, x, y, w, h, uFlags);
}

// Helper for FocusLR2Window
struct FindLR2WindowData {
    DWORD pid;
    HWND hExternalWnd;
    HWND hDebugWnd;
    HWND hFoundWnd;
};

static BOOL CALLBACK EnumWindowsCallback(HWND hwnd, LPARAM lParam) {
    FindLR2WindowData* pData = (FindLR2WindowData*)lParam;
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != pData->pid) return TRUE;
    if (hwnd == pData->hExternalWnd) return TRUE;
    if (hwnd == pData->hDebugWnd) return TRUE;
    if (!IsWindowVisible(hwnd)) return TRUE;
    if (GetWindow(hwnd, GW_OWNER) != NULL) return TRUE;
    pData->hFoundWnd = hwnd;
    return FALSE;
}

// LR2本体ウィンドウを探してフォーカスを戻す処理
// タイトル名 "LR2 beta3 version 100201" を検索
void LR2BGAWindow::FocusLR2Window()
{
    HWND hLR2 = FindWindowW(NULL, L"LR2 beta3 version 100201");
    if (hLR2 && IsWindowVisible(hLR2)) {
        SetForegroundWindow(hLR2);
        SetFocus(hLR2);
        return;
    }

    // タイトルで見つからない場合（改造クライアントなど）、プロセスIDからメインウィンドウを探索
    DWORD pid = GetCurrentProcessId();
    FindLR2WindowData data;
    data.pid = pid;
    data.hExternalWnd = m_hExtWnd;
    data.hDebugWnd = m_hDebugWnd;
    data.hFoundWnd = NULL;
    EnumWindows(EnumWindowsCallback, (LPARAM)&data);
    if (data.hFoundWnd) {
        SetForegroundWindow(data.hFoundWnd);
        SetFocus(data.hFoundWnd);
    }
}

//------------------------------------------------------------------------------
// ExtWndProc - 外部ウィンドウプロシージャ
// 
// 役割:
//   外部ウィンドウへのメッセージを処理します。
//   静的メソッドであるため、WM_CREATE 時に設定されたユーザデータ (thisポインタ) を取得して
//   インスタンスメソッドのように振る舞います。
//
// 主な処理:
//   WM_CREATE: インスタンスポインタを GWLP_USERDATA に保存。
//   WM_PAINT: 保持している最新の画像データ (m_extWindowBuf) を GDI で描画。
//             StretchDIBits を使用してリサイズ描画を行う。アスペクト比維持の計算もここで実施。
//   WM_RBUTTONUP: 右クリックでウィンドウを閉じる (設定による)。
//------------------------------------------------------------------------------
LRESULT CALLBACK LR2BGAWindow::ExtWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    // GWLP_USERDATA からインスタンスを取得
    LR2BGAWindow* pWindow = (LR2BGAWindow*)GetWindowLongPtr(hwnd, GWLP_USERDATA);

    if (uMsg == WM_CREATE) {
        CREATESTRUCT* pCreate = (CREATESTRUCT*)lParam;
        pWindow = (LR2BGAWindow*)pCreate->lpCreateParams;
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)pWindow);
        return 0;
    }

    if (pWindow) {
        switch (uMsg) {
        case WM_LBUTTONDOWN:
            SendMessage(hwnd, HTCAPTION, 0, 0);
            return 0;
            
        case WM_NCHITTEST:
            {
                LRESULT hit = DefWindowProcW(hwnd, uMsg, wParam, lParam);
                if (hit == HTCLIENT) return HTCAPTION;
                return hit;
            }
        
        case WM_RBUTTONUP:
        case WM_NCRBUTTONUP:
            // Right Click to Close
            if (pWindow->m_pSettings->m_closeOnRightClick) {
                 pWindow->OnManualClose();
                 return 0;
            }
            break;

        case WM_PAINT:
            {
                PAINTSTRUCT ps;
                HDC hdc = BeginPaint(hwnd, &ps);
                
                // Mutexで画像バッファアクセスを保護
                {
                    std::lock_guard<std::mutex> lock(pWindow->m_mtxExtWindow);
                    if (pWindow->m_extWindowBuffer.size() > 0) {
                         BITMAPINFO bmi = {0};
                         bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                         bmi.bmiHeader.biWidth = pWindow->m_extWindowBufWidth;
                         bmi.bmiHeader.biHeight = pWindow->m_extWindowBufHeight;
                         bmi.bmiHeader.biPlanes = 1;
                         bmi.bmiHeader.biBitCount = 24;
                         bmi.bmiHeader.biCompression = BI_RGB;
                         
                         RECT rect;
                         GetClientRect(hwnd, &rect);
                         SetStretchBltMode(hdc, COLORONCOLOR);
                         StretchDIBits(hdc, 0, 0, rect.right, rect.bottom,
                             0, 0, pWindow->m_extWindowBufWidth, pWindow->m_extWindowBufHeight,
                             pWindow->m_extWindowBuffer.data(), &bmi, DIB_RGB_COLORS, SRCCOPY);
                    } else {
                        // Draw black background if no buffer yet
                        RECT rect;
                        GetClientRect(hwnd, &rect);
                        FillRect(hdc, &rect, (HBRUSH)GetStockObject(BLACK_BRUSH));
                    }
                } // Unlock
                EndPaint(hwnd, &ps);
            }
            return 0;
            
        case WM_MOVE:
        case WM_SIZE:
            if (pWindow->m_hOverlayWnd) {
                // Sync Overlay
                RECT rect;
                GetWindowRect(hwnd, &rect);
                SetWindowPos(pWindow->m_hOverlayWnd, NULL, 
                    rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, 
                    SWP_NOZORDER | SWP_NOACTIVATE);
            }
            break; // Added break here to allow WM_WINDOWPOSCHANGED to be processed if it follows.

        case WM_WINDOWPOSCHANGED:
            {
                RECT rect;
                GetWindowRect(hwnd, &rect);
                pWindow->m_pSettings->m_extWindowX = rect.left;
                pWindow->m_pSettings->m_extWindowY = rect.top;
                pWindow->m_pSettings->Save(); // Sync save
                
                // Sync Overlay
                if (pWindow->m_hOverlayWnd) {
                     HWND hWndInsertAfter = pWindow->m_pSettings->m_extWindowTopmost ? HWND_TOPMOST : HWND_NOTOPMOST;
                     SetWindowPos(pWindow->m_hOverlayWnd, hWndInsertAfter, 
                        rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, 
                        SWP_NOACTIVATE | SWP_NOOWNERZORDER);
                }
            }
            return 0;

        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
            
        case WM_DESTROY:
            if (pWindow->m_hOverlayWnd && IsWindow(pWindow->m_hOverlayWnd)) {
                DestroyWindow(pWindow->m_hOverlayWnd);
                pWindow->m_hOverlayWnd = NULL;
            }
            PostQuitMessage(0);
            return 0;
        }
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

//------------------------------------------------------------------------------
// ExtWindowThread - 外部ウィンドウ用スレッド
// 
// 役割:
//   外部ウィンドウのための独立したメッセージループを提供します。
//   メインスレッド (DirectShowフィルタ等のスレッド) がブロックしてもウィンドウが応答するように、
//   UI操作はここで行います。
// 
// フロー:
//   1. ウィンドウクラスの登録 (LR2BGAFilterExtWnd, LR2BGAFilterOverlayWnd)
//------------------------------------------------------------------------------
        


//------------------------------------------------------------------------------
// デバッグウィンドウの実装 (Debug Window Implementation)
//------------------------------------------------------------------------------

void LR2BGAWindow::ShowDebugWindow()
{
    if (!m_pSettings->m_debugMode) return;
    
    if (m_hDebugWnd && IsWindow(m_hDebugWnd)) {
        ShowWindow(m_hDebugWnd, SW_SHOW);
        return;
    }

    if (m_threadDebug.joinable()) m_threadDebug.join();
    m_threadDebug = std::thread(&LR2BGAWindow::DebugWindowThread, this);
}

//------------------------------------------------------------------------------
// UpdateDebugInfo
// 
// 役割:
//   デバッグウィンドウに表示するテキスト情報を更新します。
//   フィルタ本体から呼び出され、各パラメータ（フィルタグラフ情報、解像度、FPS等）を受け取ります。
//   
// 注意:
//   大量の文字列フォーマット処理を含むため、頻繁（毎フレームなど）に呼び出すと負荷になる可能性があります。
//   現状はLR2BGAFilter::UpdateDebugInfoで制御されています。
//------------------------------------------------------------------------------
void LR2BGAWindow::UpdateDebugInfo(
    const std::wstring& inputFilter, const std::wstring& outputFilter,
    const std::wstring& filterGraphInfo,
    int inputWidth, int inputHeight, int inputBitCount,
    int outputWidth, int outputHeight,
    double frameRate, double outputFrameRate,
    long long frameCount, long long droppedFrames,
    double avgTime,
    long exceptionCount,
    const LetterboxDebugInfo& lbInfo)
{
    if (!m_hDebugWnd || !IsWindow(m_hDebugWnd)) return;
    

    {
       std::lock_guard<std::mutex> lock(m_mtxDebug);
    
    wchar_t extInfo[512];
    if (m_pSettings->m_extWindowEnabled) {
        swprintf_s(extInfo, sizeof(extInfo)/sizeof(wchar_t),
            L"Enabled\r\n"
            L"  Position: %d, %d\r\n"
            L"  Size: %dx%d\r\n"
            L"  Wait: %s\r\n"
            L"  Algorithm: %s\r\n"
            L"  Keep Aspect: %s\r\n"
            L"  Passthrough: %s\r\n"
            L"  Layer: %s",
            m_pSettings->m_extWindowX, m_pSettings->m_extWindowY,
            m_pSettings->m_extWindowWidth, m_pSettings->m_extWindowHeight,
            m_pSettings->m_extWindowPassthrough ? L"Yes (Source Sync)" : L"No (Fixed Size)",
            m_pSettings->m_extWindowAlgo == RESIZE_NEAREST ? L"Nearest" : L"Bilinear",
            m_pSettings->m_extWindowKeepAspect ? L"Yes" : L"No",
            m_pSettings->m_extWindowPassthrough ? L"Yes" : L"No",
            m_pSettings->m_extWindowTopmost ? L"Topmost" : L"Bottommost");
    } else {
        wcscpy_s(extInfo, sizeof(extInfo)/sizeof(wchar_t), L"Disabled");
    }
    
    wchar_t fpsLimitStr[32];
    if (m_pSettings->m_maxFPS > 0) {
        swprintf_s(fpsLimitStr, sizeof(fpsLimitStr)/sizeof(wchar_t), L"%d fps", m_pSettings->m_maxFPS);
    } else {
        wcscpy_s(fpsLimitStr, sizeof(fpsLimitStr)/sizeof(wchar_t), L"Uncapped");
    }

    // ゲームパッド入力チェック（デバッグ用）
    wchar_t gamePadStatus[128];
    JOYINFOEX joyInfo;
    joyInfo.dwSize = sizeof(JOYINFOEX);
    joyInfo.dwFlags = JOY_RETURNBUTTONS;
    MMRESULT res = joyGetPosEx(m_pSettings->m_gamepadID, &joyInfo);
    if (res == MMSYSERR_NOERROR) {
        if (joyInfo.dwButtons == 0) {
            wcscpy_s(gamePadStatus, sizeof(gamePadStatus)/sizeof(wchar_t), L"Connected (None)");
        } else {
            wchar_t btns[64] = L"";
            bool first = true;
            for (int b = 0; b < 32; b++) {
                if (joyInfo.dwButtons & (1 << b)) {
                    wchar_t buf[8];
                    swprintf_s(buf, sizeof(buf)/sizeof(wchar_t), L"%s%d", first ? L"" : L",", b);
                    wcscat_s(btns, sizeof(btns)/sizeof(wchar_t), buf);
                    first = false;
                }
            }
            swprintf_s(gamePadStatus, sizeof(gamePadStatus)/sizeof(wchar_t), L"Connected (Btn:%s)", btns);
        }
    } else {
        swprintf_s(gamePadStatus, sizeof(gamePadStatus)/sizeof(wchar_t), L"Not Connected (Err:%d)", res);
    }

    // キーボード入力チェック（ライブ監視）
    wchar_t keyStatus[64] = L"None";
    for (int k = 0x01; k < 0xFF; k++) {
        if (GetAsyncKeyState(k) & 0x8000) {
            swprintf_s(keyStatus, sizeof(keyStatus)/sizeof(wchar_t), L"0x%X (%d)", k, k);
            break; // Show first key only
        }
    }

    // LB Mode String
    const wchar_t* lbModeStr = L"Off";
    if (!m_pSettings->m_autoRemoveLetterbox) {
        lbModeStr = L"Disabled";
    } else {
        switch (lbInfo.detectedMode) {
            case 0: lbModeStr = L"Scanning (Original)"; break; // LB_MODE_ORIGINAL
            case 1: lbModeStr = L"16:9 Detected"; break;      // LB_MODE_16_9
            case 2: lbModeStr = L"4:3 Detected"; break;       // LB_MODE_4_3
            default: lbModeStr = L"Unknown"; break;
        }
    }

    // 黒帯検出の詳細情報 (Detailed LB Info)
    wchar_t lbDetailStr[1024] = L"";
    if (m_pSettings->m_autoRemoveLetterbox) {
        swprintf_s(lbDetailStr, sizeof(lbDetailStr)/sizeof(wchar_t),
            L"[Auto Letterbox Removal]\r\n"
            L"  LB Exceptions: %ld\r\n"
            L"  LB Mode: %s\r\n"
            L"  Stability: %d / %d\r\n"
            L"  Center Black: %s (%.1f%%)\r\n"
            L"  16:9 Check: %s (Top: %.1f%%, Btm: %.1f%%)%s\r\n"
            L"  4:3 Check: %s (Top: %.1f%%, Btm: %.1f%%)%s\r\n\r\n",
            exceptionCount,
            lbModeStr,
            lbInfo.stabilityCounter, lbInfo.stabilityThreshold,
            lbInfo.isCenterBlack ? L"Yes" : L"No", lbInfo.centerBlackRatio * 100.0f,
            (lbInfo.ratio169Top < 0) ? L"Skipped" : (lbInfo.is169TopBlack && lbInfo.is169BottomBlack ? L"Yes" : L"No"),
            (lbInfo.ratio169Top < 0) ? 0.0f : lbInfo.ratio169Top * 100.0f,
            (lbInfo.ratio169Bottom < 0) ? 0.0f : lbInfo.ratio169Bottom * 100.0f,
            lbInfo.bRejected169 ? L" [Rejected]" : L"",
            (lbInfo.ratio43Top < 0) ? L"Skipped" : (lbInfo.is43TopBlack && lbInfo.is43BottomBlack ? L"Yes" : L"No"),
            (lbInfo.ratio43Top < 0) ? 0.0f : lbInfo.ratio43Top * 100.0f,
            (lbInfo.ratio43Bottom < 0) ? 0.0f : lbInfo.ratio43Bottom * 100.0f,
            lbInfo.bRejected43 ? L" [Rejected]" : L""
        ); 
    } else {
        swprintf_s(lbDetailStr, sizeof(lbDetailStr)/sizeof(wchar_t),
            L"[Auto Letterbox Removal]\r\n"
            L"  Disabled\r\n\r\n");
    }

    // デバッグテキストの構築
    swprintf_s(m_debugText, sizeof(m_debugText)/sizeof(wchar_t),
        L"[LR2 Output]\r\n"
        L"  Input Size: %dx%d (%d bpp)\r\n"
        L"  Output Size: %dx%d\r\n"
        L"  FPS Limit: %s\r\n"
        L"  Keep Aspect: %s\r\n"
        L"  Raw Input Frame Rate: %.2f fps\r\n\r\n"
        L"[External Window]\r\n"
        L"  %s\r\n\r\n"
        L"[Close Trigger]\r\n"
        L"  Right Click: %s\r\n"
        L"  Gamepad: %s\r\n"
        L"  Keyboard: %s\r\n\r\n"
        L"%s" // Note: Replaced LB Details
        L"[Filter Graph]\r\n%s\r\n"
        L"[Statistics]\r\n"
        L"  Avg Processing Time: %.3f ms\r\n"
        L"  Frame Count: %lld\r\n"
        L"  Dropped Frames: %lld\r\n"
        L"  Input Filter: %s\r\n"
        L"  Output Filter: %s\r\n",
        inputWidth, inputHeight, inputBitCount,
        outputWidth, outputHeight,
        fpsLimitStr,
        m_pSettings->m_keepAspectRatio ? L"Yes" : L"No",
        frameRate,
        // extInfo
        extInfo, 
        m_pSettings->m_closeOnRightClick ? L"Enabled" : L"Disabled",
        gamePadStatus,
        keyStatus,
        lbDetailStr,
        filterGraphInfo.c_str(), // Filter Graph Section
        // Stats
        avgTime,
        frameCount,
        droppedFrames,
        inputFilter.c_str(),
        outputFilter.c_str());
    
    } // Unlock m_mtxDebug
    InvalidateRect(m_hDebugWnd, NULL, FALSE);
}

LRESULT CALLBACK LR2BGAWindow::DebugWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    LR2BGAWindow* pThis = NULL;

    if (msg == WM_NCCREATE) {
        CREATESTRUCT* pCreate = (CREATESTRUCT*)lParam;
        pThis = (LR2BGAWindow*)pCreate->lpCreateParams;
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)pThis);
    } else {
        pThis = (LR2BGAWindow*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    }

    switch (msg) {
    case WM_CREATE:
        {
            CreateWindowW(L"BUTTON", L"Copy Info",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                10, 10, 100, 30, hwnd, (HMENU)101, g_hInst, NULL);
                
            CreateWindowW(L"BUTTON", L"Open Settings",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                120, 10, 100, 30, hwnd, (HMENU)IDC_BUTTON_OPEN_SETTINGS, g_hInst, NULL); // IDC from resource.h
        }
        return 0;

    case WM_COMMAND:
        if (LOWORD(wParam) == 101 && pThis) {
            // "Copy Info" ボタンハンドラ
            if (OpenClipboard(hwnd)) {
                EmptyClipboard();
                {
                    std::lock_guard<std::mutex> lock(pThis->m_mtxDebug);
                    size_t len = wcslen(pThis->m_debugText);
                    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, (len + 1) * sizeof(wchar_t));
                    if (hMem) {
                        wchar_t* pMem = (wchar_t*)GlobalLock(hMem);
                        wcscpy_s(pMem, len + 1, pThis->m_debugText);
                        GlobalUnlock(hMem);
                        SetClipboardData(CF_UNICODETEXT, hMem);
                    }
                } // Unlock
                CloseClipboard();
            }
        } else if (LOWORD(wParam) == IDC_BUTTON_OPEN_SETTINGS && pThis) {
            // Open Settings Handler
            pThis->ShowPropertyPage();
        }
        return 0;

    case WM_ERASEBKGND:
        return 1; // ちらつき防止のため背景消去をスキップ (ダブルバッファリング使用)

    case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            if (pThis) {
                RECT rect;
                GetClientRect(hwnd, &rect);

                // ダブルバッファリング (ちらつき防止)
                HDC hMemDC = CreateCompatibleDC(hdc);
                HBITMAP hMemBitmap = CreateCompatibleBitmap(hdc, rect.right, rect.bottom);
                HGDIOBJ hOldBitmap = SelectObject(hMemDC, hMemBitmap);

                // 背景塗りつぶし
                HBRUSH hBgBrush = CreateSolidBrush(RGB(25, 20, 15)); // レトロな暗い背景色
                FillRect(hMemDC, &rect, hBgBrush);
                DeleteObject(hBgBrush);

                // テキスト描画
                SetTextColor(hMemDC, RGB(255, 180, 0)); // アンバー（琥珀色）
                SetBkMode(hMemDC, TRANSPARENT);

                HFONT hFont = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, 
                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, 
                    DEFAULT_QUALITY, FIXED_PITCH, L"Consolas");
                HGDIOBJ hOldFont = SelectObject(hMemDC, hFont);
                
                rect.top += kDebugWindowButtonMargin; // ボタン領域を確保
                rect.left += 10; // Margin
                
                {
                    std::lock_guard<std::mutex> lock(pThis->m_mtxDebug);
                    DrawTextW(hMemDC, pThis->m_debugText, -1, &rect, DT_LEFT | DT_TOP | DT_WORDBREAK);
                } // Unlock
                
                SelectObject(hMemDC, hOldFont);
                DeleteObject(hFont);

                // 画面へ転送 (Blit)
                BitBlt(hdc, 0, 0, rect.right, rect.bottom, hMemDC, 0, 0, SRCCOPY);

                // Cleanup
                SelectObject(hMemDC, hOldBitmap);
                DeleteObject(hMemBitmap);
                DeleteDC(hMemDC);
            }
            EndPaint(hwnd, &ps);
        }
        return 0;
    case WM_CLOSE:
        // ShowWindow(hwnd, SW_HIDE);
        // DestroyWindow to ensure thread exit
        DestroyWindow(hwnd); 
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void LR2BGAWindow::DebugWindowThread()
{
    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = DebugWndProc;
    wc.hInstance = g_hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = DEBUG_WND_CLASS;
    RegisterClassExW(&wc);

    // this を lpParam として渡す
    HWND hwnd = CreateWindowExW(0, DEBUG_WND_CLASS, L"LR2 BGA Filter Info",
        WS_OVERLAPPEDWINDOW, // WS_VISIBLE を削除
        CW_USEDEFAULT, CW_USEDEFAULT, 450, 1000,
        NULL, NULL, g_hInst, this);

    if (hwnd) {
        m_hDebugWnd = hwnd;
        ShowWindow(hwnd, SW_SHOWNOACTIVATE); // アクティブ化せずに表示！
        UpdateWindow(hwnd);
        MSG msg;
        while (GetMessage(&msg, NULL, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
    m_hDebugWnd = NULL;
}



void LR2BGAWindow::SetFilter(IUnknown* pFilterUnk)
{
    m_pFilterUnk = pFilterUnk;
}

//------------------------------------------------------------------------------
// プロパティページの実装 (Property Page Implementation)
//------------------------------------------------------------------------------

void LR2BGAWindow::ShowPropertyPage()
{
    if (!m_pFilterUnk) return;

    // スレッドが既に実行中か確認
    if (m_bPropPageActive) return;

    // プロパティページをモーダル表示として別スレッドで開く
    // (メインスレッドをブロックしないため)
    if (m_threadProp.joinable()) m_threadProp.join();
    m_bPropPageActive = true;
    m_threadProp = std::thread(&LR2BGAWindow::PropertyPageThread, this);
    
    // 自動オープンの場合、遅延後にLR2へフォーカスを戻すスレッドを起動
    if (m_pSettings->m_autoOpenSettings) {
       // 別スレッドでフォーカス復帰処理を行う（ブロッキング回避）
    if (m_threadFocus.joinable()) m_threadFocus.join();
    m_threadFocus = std::thread(&LR2BGAWindow::FocusRestoreThread, this);
    }
}

void LR2BGAWindow::PropertyPageThread()
{
    if (!m_pFilterUnk) return;
    
    // このスレッド用にCOMを初期化
    CoInitialize(NULL);
    
    IUnknown* pFilterUnk = m_pFilterUnk;
    GUID clsid = CLSID_LR2BGAFilterPropertyPage;
    
    // プロパティフレームを表示
    HRESULT hr = OleCreatePropertyFrame(
        NULL,                   // 親ウィンドウ (NULL = デスクトップ, または DebugWnd を使用可能)
        0, 0,                   // x, y
        L"LR2 BGA Filter",      // キャプション
        1,                      // オブジェクト数
        &pFilterUnk,            // IUnknown ポインタ配列
        1,                      // プロパティページ数
        &clsid,                 // プロパティページ CLSID 配列
        0,                      // ロケール ID
        0, NULL                 // 予約済み
    );
    
    CoUninitialize();
    m_bPropPageActive = false;
}

void LR2BGAWindow::FocusRestoreThread()
{
    // プロパティページが表示されるのを待機 (モーダル的だがここでは別スレッドで動作)
    // PropertyPageThread は OleCreatePropertyFrame を使用しており、この関数はそのスレッドでブロックします。
    // ダイアログが表示されてフォーカスを奪うまで少し待ち、その後強制的にLR2へフォーカスを戻します。
    // 200-500ms 程度で十分です。
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    
    // LR2を見つけてフォーカスを試みる
    FocusLR2Window();
}

//------------------------------------------------------------------------------
// 入力監視スレッド (Input Monitor Thread)
//------------------------------------------------------------------------------
void LR2BGAWindow::StartInputMonitor()
{
    StopInputMonitor(); // 念のため停止
    m_bInputStop = false;
    m_threadInput = std::thread(&LR2BGAWindow::InputMonitorThread, this);
}

void LR2BGAWindow::StopInputMonitor()
{
    if (m_threadInput.joinable()) {
        {
            std::lock_guard<std::mutex> lock(m_mtxInput);
            m_bInputStop = true;
            m_cvInput.notify_one();
        }
        m_threadInput.join();
    }
}

void LR2BGAWindow::RestartInputMonitor()
{
    StopInputMonitor(); // 既存の監視を停止
    // 外部ウィンドウが実際にアクティブな場合のみ再起動
    // ウィンドウが閉じている場合、ShowExternalWindow 経由で StartInputMonitor が呼ばれます。
    // ウィンドウが閉じている場合、ShowExternalWindow 経由で StartInputMonitor が呼ばれます。
    if (m_pSettings->m_extWindowEnabled && (m_hExtWnd || m_threadExt.joinable())) {
        StartInputMonitor();
    }
}

void LR2BGAWindow::ExtWindowThread()
{
    // 外部ウィンドウ用スレッドのエントリポイント
    // GUIスレッドとして独立して動作し、メインのフィルタ処理（DirectShow）がブロックしても応答性を維持します。

    // 1. 外部ウィンドウクラスの登録
    //    フィルタDLLのインスタンスハンドルを使用してWNDCLASSEXを登録します。
    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = ExtWndProc;
    wc.hInstance = g_hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = EXT_WND_CLASS;
    RegisterClassExW(&wc);

    // 2. オーバーレイウィンドウクラスの登録
    //    明るさ調整用（黒色半透明）のウィンドウクラスです。
    WNDCLASSEXW wcOverlay = wc; // 基本設定をコピー
    wcOverlay.lpszClassName = OVERLAY_WND_CLASS;
    wcOverlay.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    RegisterClassExW(&wcOverlay);
    
    // 3. 外部ウィンドウの作成
    //    初期サイズはデフォルト値ですが、後続の UpdateExternalWindowPos で設定値が適用されます。
    //    WS_CLIPCHILDREN はオーバーレイ等の小ウィンドウ描画時のチラつき防止に重要です。
    HWND hwnd = CreateWindowExW(
        0, 
        EXT_WND_CLASS, 
        L"LR2 BGA External Window",
        WS_POPUP | WS_CLIPCHILDREN, 
        CW_USEDEFAULT, CW_USEDEFAULT, 800, 600,
        NULL, NULL, g_hInst, this);
    
    // 4. オーバーレイウィンドウの作成
    //    Layered Window として作成し、アルファブレンドによる明度調整を実現します。
    //    WS_EX_TRANSPARENT により、マウスイベントを透過させて背面の外部ウィンドウに伝播させます。
    HWND hOverlay = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        OVERLAY_WND_CLASS,
        L"LR2 BGA Overlay",
        WS_POPUP,
        0, 0, 100, 100, // ダミーサイズ（親ウィンドウ追従）
        hwnd, // 親ウィンドウを外部ウィンドウに設定
        NULL, g_hInst, this);

    if (hwnd) {
        m_hExtWnd = hwnd;
        m_hOverlayWnd = hOverlay;
        
        // 設定の適用 (位置、サイズ、Topmost、明るさ)
        UpdateExternalWindowPos(); 
        UpdateOverlayWindow();     

        // 設定で有効な場合のみ表示
        // SW_SHOWNOACTIVATE を使用して、LR2本体からフォーカスを奪わないようにします。
        if (m_pSettings->m_extWindowEnabled) {
            ShowWindow(hwnd, SW_SHOWNOACTIVATE);
        }

        // メッセージループ
        // このスレッド内でのウィンドウメッセージを処理します。
        MSG msg;
        while (GetMessage(&msg, NULL, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
    
    // クリーンアップ
    // スレッド終了時にハンドルを無効化します。
    m_hExtWnd = NULL;
    m_hOverlayWnd = NULL;
}

void LR2BGAWindow::InputMonitorThread()
{
    // 入力監視ループ
    // ゲームパッドやキーボードの状態をポーリングし、特定のボタン押下で外部ウィンドウを閉じます。
    while (true) {
        // 停止シグナルの待機
        // std::condition_variable を使用して、停止要求があるまで最大50ms待機します。
        // これにより、CPU負荷を抑えつつ、停止要求には即座に応答できます。
        {
            std::unique_lock<std::mutex> lock(m_mtxInput);
            if (m_cvInput.wait_for(lock, std::chrono::milliseconds(kInputMonitorIntervalMs), [this] { return m_bInputStop; })) {
                if (m_bInputStop) break; // 停止要求があればループを抜ける
            }
        }

        bool closeTriggered = false;
        
        // ゲームパッドを確認 (DirectInput/XInputではなくwinmmのjoyGetPosExを使用)
        if (m_pSettings->m_gamepadCloseEnabled) {
            JOYINFOEX joyInfo;
            joyInfo.dwSize = sizeof(JOYINFOEX);
            joyInfo.dwFlags = JOY_RETURNBUTTONS;
            
            if (joyGetPosEx(m_pSettings->m_gamepadID, &joyInfo) == MMSYSERR_NOERROR) {
                if (joyInfo.dwButtons & (1 << m_pSettings->m_gamepadButtonID)) {
                    closeTriggered = true;
                }
            }
        }
        
        // キーボードを確認 (非同期キーステート取得)
        if (!closeTriggered && m_pSettings->m_keyboardCloseEnabled) {
            if (GetAsyncKeyState(m_pSettings->m_keyboardKeyCode) & 0x8000) {
                closeTriggered = true;
            }
        }
        
        if (closeTriggered) {
            // 閉じるアクション
            // 設定値の変更ではなく、一時的なウィンドウクローズとして扱います。
            OnManualClose();
            break; // トリガーされたら監視スレッドは終了
        }
    }
}

void LR2BGAWindow::OnManualClose()
{
    // 永続的な設定を変更せずに非同期でウィンドウを閉じる
    if (m_hExtWnd && IsWindow(m_hExtWnd)) {
        PostMessage(m_hExtWnd, WM_CLOSE, 0, 0);
    }
}
extern HINSTANCE g_hInst;


