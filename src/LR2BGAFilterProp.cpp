//------------------------------------------------------------------------------
// LR2BGAFilterProp.cpp
//
// 概要:
//   DirectShowフィルタのプロパティページ (設定ダイアログ) の動作を実装します。
//   ユーザーが設定を変更した際、ILR2BGAFilterSettings インターフェースを通じて
//   設定値を即座に反映(Apply)または保存します。
//
// アーキテクチャ:
//   - CBasePropertyPage (DirectShow基底クラス) を継承。
//   - m_pSettings (ILR2BGAFilterSettings) へのポインタを保持し、データの読み書きを行います。
//   - ウィンドウメッセージ (WM_COMMAND, WM_HSCROLL) を捕捉してUIの変更を検知し、
//     m_bDirty フラグを立てて「適用」ボタンを有効化します。
//------------------------------------------------------------------------------

#define NO_DSHOW_STRSAFE

#include <streams.h>
#include <commctrl.h>
#include "LR2BGAFilterProp.h"

//------------------------------------------------------------------------------
// CreateInstance
// プロパティページのインスタンスを作成するファクトリメソッド
//------------------------------------------------------------------------------
CUnknown* WINAPI CLR2BGAFilterPropertyPage::CreateInstance(LPUNKNOWN pUnk, HRESULT* phr)
{
    CLR2BGAFilterPropertyPage* pPage = new CLR2BGAFilterPropertyPage(pUnk);
    if (pPage == NULL && phr) {
        *phr = E_OUTOFMEMORY;
    }
    return pPage;
}

//------------------------------------------------------------------------------
// コンストラクタ
//------------------------------------------------------------------------------
CLR2BGAFilterPropertyPage::CLR2BGAFilterPropertyPage(LPUNKNOWN pUnk)
    : CBasePropertyPage(NAME("LR2 BGA Filter Settings"), pUnk, IDD_PROPPAGE, IDS_TITLE)
    , m_pSettings(NULL)
    , m_width(256)
    , m_height(256)
    , m_algo(RESIZE_BILINEAR)
    , m_keepAspect(TRUE)
    , m_debugMode(FALSE)
    , m_limitFPS(FALSE)
    , m_maxFPS(0)
    , m_dummyMode(FALSE)
    , m_passthroughMode(FALSE)
    , m_extEnabled(FALSE)
    , m_extX(0)
    , m_extY(0)
    , m_extWidth(512)
    , m_extHeight(512)
    , m_extAlgo(RESIZE_BILINEAR)
    , m_extKeepAspect(TRUE)
    , m_extPassthrough(FALSE)
    , m_extTopmost(TRUE)
    , m_brightnessLR2(100)
    , m_brightnessExt(100)
    , m_autoOpen(FALSE)
    , m_autoRemoveLB(FALSE)
    , m_lbThreshold(0)
    , m_lbStability(5)
    , m_closeOnRightClick(FALSE)
    , m_gamepadCloseEnabled(FALSE)
    , m_gamepadID(0)
    , m_gamepadButtonID(0)
    , m_keyboardCloseEnabled(FALSE)
    , m_keyboardKeyCode(0)
{
}

//------------------------------------------------------------------------------
// OnConnect
// フィルタがプロパティページに接続された際に呼び出される
//------------------------------------------------------------------------------
HRESULT CLR2BGAFilterPropertyPage::OnConnect(IUnknown* pUnk)
{
    CheckPointer(pUnk, E_POINTER);

    HRESULT hr = pUnk->QueryInterface(IID_ILR2BGAFilterSettings, (void**)&m_pSettings);
    if (FAILED(hr)) {
        return hr;
    }

    // Weak Reference Pattern:
    // プロパティページが開いている間もフィルタの参照カウントを増やさないようにする。
    // これにより、ホストアプリケーションがフィルタを破棄した際に、
    // プロパティページが開いていてもフィルタのデストラクタが呼び出されるようになる。
    // フィルタのデストラクタ内でプロパティページは強制的に閉じられるため、ダングリングポインタの問題は回避される。
    m_pSettings->Release();

    // Main Settings
    m_pSettings->GetOutputSize(&m_width, &m_height);
    m_pSettings->GetResizeAlgorithm(&m_algo);
    m_pSettings->GetKeepAspectRatio(&m_keepAspect);
    m_pSettings->GetDebugMode(&m_debugMode);
    
    // FPS Limit
    // FPS Limit
    BOOL bLimit = FALSE;
    m_pSettings->GetLimitFPS(&bLimit);
    m_limitFPS = (bLimit != FALSE);
    m_pSettings->GetMaxFPS(&m_maxFPS);
    
    m_pSettings->GetDummyMode(&m_dummyMode);
    m_pSettings->GetPassthroughMode(&m_passthroughMode);
    
    // External Window
    m_pSettings->GetExternalWindowEnabled(&m_extEnabled);
    m_pSettings->GetExternalWindowPosition(&m_extX, &m_extY);
    m_pSettings->GetExternalWindowSize(&m_extWidth, &m_extHeight);
    m_pSettings->GetExternalWindowAlgorithm(&m_extAlgo);
    m_pSettings->GetExternalWindowKeepAspect(&m_extKeepAspect);
    m_pSettings->GetExternalWindowPassthrough(&m_extPassthrough);
    m_pSettings->GetExternalWindowTopmost(&m_extTopmost);

    // Manual Close
    m_pSettings->GetCloseOnRightClick(&m_closeOnRightClick);
    m_pSettings->GetGamepadCloseEnabled(&m_gamepadCloseEnabled);
    m_pSettings->GetGamepadID(&m_gamepadID);
    m_pSettings->GetGamepadButtonID(&m_gamepadButtonID);
    m_pSettings->GetKeyboardCloseEnabled(&m_keyboardCloseEnabled);
    m_pSettings->GetKeyboardKeyCode(&m_keyboardKeyCode);
    
    // Auto Remove Letterbox
    m_pSettings->GetAutoRemoveLetterbox(&m_autoRemoveLB);
    m_pSettings->GetLetterboxThreshold(&m_lbThreshold);
    m_pSettings->GetLetterboxStability(&m_lbStability);

    // Brightness
    m_pSettings->GetBrightnessLR2(&m_brightnessLR2);
    m_pSettings->GetBrightnessExt(&m_brightnessExt);

    // Auto Open
    m_pSettings->GetAutoOpenSettings(&m_autoOpen);

    return S_OK;
}

//------------------------------------------------------------------------------
// OnDisconnect
// フィルタとの接続が解除された際に呼び出される
//
// 重要: Release() を呼び出さない理由
//   プロパティページが閉じられると、この関数が PropertyPageThread 内から呼ばれます。
//   Release() を呼ぶと、これがフィルタへの最後の参照だった場合、フィルタのデストラクタが
//   PropertyPageThread 内から実行されます。デストラクタは自身のスレッド (m_threadProp) を
//   クリーンアップしようとしますが、実行中のスレッドに対して join/detach を呼ぶことは
//   std::terminate を引き起こします。
//
//   この問題を回避するため、参照のリリースはスキップします。
//   プロパティページが閉じられた後、ホストアプリケーションがフィルタグラフを破棄する際に
//   フィルタは正しくクリーンアップされます。
//------------------------------------------------------------------------------
HRESULT CLR2BGAFilterPropertyPage::OnDisconnect()
{
    // OnConnect で Release() して Weak Reference としているため、
    // ここでの Release() は不要（二重解放になるため禁止）。
    m_pSettings = NULL;
    return S_OK;
}

//------------------------------------------------------------------------------
// OnActivate
// プロパティページが表示（アクティブ化）された際に呼び出される
//------------------------------------------------------------------------------
HRESULT CLR2BGAFilterPropertyPage::OnActivate()
{
    // スピンコントロールの設定 (共通)
    SendMessage(GetDlgItem(m_Dlg, IDC_SPIN_WIDTH), UDM_SETRANGE32, 1, 4096);
    SendMessage(GetDlgItem(m_Dlg, IDC_SPIN_HEIGHT), UDM_SETRANGE32, 1, 4096);
    SendMessage(GetDlgItem(m_Dlg, IDC_SPIN_MAXFPS), UDM_SETRANGE32, 1, 60);

    // 外部ウィンドウ用スピン設定
    SendMessage(GetDlgItem(m_Dlg, IDC_SPIN_EXT_X), UDM_SETRANGE32, -4096, 4096);
    SendMessage(GetDlgItem(m_Dlg, IDC_SPIN_EXT_Y), UDM_SETRANGE32, -4096, 4096);
    SendMessage(GetDlgItem(m_Dlg, IDC_SPIN_EXT_WIDTH), UDM_SETRANGE32, 1, 4096);
    SendMessage(GetDlgItem(m_Dlg, IDC_SPIN_EXT_HEIGHT), UDM_SETRANGE32, 1, 4096);
    
    // 黒帯除去用スピン設定
    SendMessage(GetDlgItem(m_Dlg, IDC_SPIN_LB_THRESHOLD), UDM_SETRANGE32, 0, 255);
    SendMessage(GetDlgItem(m_Dlg, IDC_SPIN_LB_STABILITY), UDM_SETRANGE32, 1, 900);

    // ゲームパッド用スピン設定
    SendMessage(GetDlgItem(m_Dlg, IDC_SPIN_GAMEPAD_ID), UDM_SETRANGE32, 0, 15);
    SendMessage(GetDlgItem(m_Dlg, IDC_SPIN_GAMEPAD_BTN), UDM_SETRANGE32, 0, 31);

    // アルゴリズムコンボボックスの初期化 (メイン)
    HWND hCombo = GetDlgItem(m_Dlg, IDC_COMBO_ALGORITHM);
    SendMessage(hCombo, CB_ADDSTRING, 0, (LPARAM)L"Nearest Neighbor (Fast)");
    SendMessage(hCombo, CB_ADDSTRING, 0, (LPARAM)L"Bilinear (Balanced)");

    // アルゴリズムコンボボックスの初期化 (外部ウィンドウ)
    HWND hComboExt = GetDlgItem(m_Dlg, IDC_COMBO_EXT_ALGO);
    SendMessage(hComboExt, CB_ADDSTRING, 0, (LPARAM)L"Nearest Neighbor (Fast)");
    SendMessage(hComboExt, CB_ADDSTRING, 0, (LPARAM)L"Bilinear (Balanced)");
    
    // スライダー範囲設定
    SendMessage(GetDlgItem(m_Dlg, IDC_SLIDER_BRIGHTNESS_LR2), TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
    SendMessage(GetDlgItem(m_Dlg, IDC_SLIDER_BRIGHTNESS_EXT), TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
    
    // Initialize Bindings
    InitBindings();

    // Update controls with current values
    ApplyToUI();

    return S_OK;
}

//------------------------------------------------------------------------------
// OnApplyChanges
// ユーザーが「適用」または「OK」をクリックした際に呼び出される
//------------------------------------------------------------------------------
HRESULT CLR2BGAFilterPropertyPage::OnApplyChanges()
{
    if (m_pSettings == NULL) {
        return E_UNEXPECTED;
    }

    // コントロールから値を読み取る
    ReadControls();

    // フィルタに設定値を適用
    m_pSettings->SetOutputSize(m_width, m_height);
    m_pSettings->SetResizeAlgorithm(m_algo);
    m_pSettings->SetKeepAspectRatio(m_keepAspect);
    m_pSettings->SetDebugMode(m_debugMode);
    m_pSettings->SetLimitFPS(m_limitFPS);
    m_pSettings->SetMaxFPS(m_maxFPS);
    
    m_pSettings->SetDummyMode(m_dummyMode);
    m_pSettings->SetPassthroughMode(m_passthroughMode);
    
    m_pSettings->SetExternalWindowEnabled(m_extEnabled);
    m_pSettings->SetExternalWindowPosition(m_extX, m_extY);
    m_pSettings->SetExternalWindowSize(m_extWidth, m_extHeight);
    m_pSettings->SetExternalWindowAlgorithm(m_extAlgo);
    m_pSettings->SetExternalWindowKeepAspect(m_extKeepAspect);
    m_pSettings->SetExternalWindowPassthrough(m_extPassthrough);
    m_pSettings->SetExternalWindowTopmost(m_extTopmost);

    // Manual Close
    m_pSettings->SetCloseOnRightClick(m_closeOnRightClick);
    m_pSettings->SetGamepadCloseEnabled(m_gamepadCloseEnabled);
    m_pSettings->SetGamepadID(m_gamepadID);
    m_pSettings->SetGamepadButtonID(m_gamepadButtonID);
    m_pSettings->SetKeyboardCloseEnabled(m_keyboardCloseEnabled);
    m_pSettings->SetKeyboardKeyCode(m_keyboardKeyCode);

    // Brightness
    int bLR2 = (int)SendMessage(GetDlgItem(m_Dlg, IDC_SLIDER_BRIGHTNESS_LR2), TBM_GETPOS, 0, 0);
    m_pSettings->SetBrightnessLR2(bLR2);
    
    int bExt = (int)SendMessage(GetDlgItem(m_Dlg, IDC_SLIDER_BRIGHTNESS_EXT), TBM_GETPOS, 0, 0);
    m_pSettings->SetBrightnessExt(bExt);

    // Auto Open
    BOOL bAutoOpen = (IsDlgButtonChecked(m_Dlg, IDC_CHECK_AUTO_OPEN) == BST_CHECKED);
    m_pSettings->SetAutoOpenSettings(bAutoOpen);
    
    // Auto Remove LB
    // Auto Remove LB
    m_pSettings->SetAutoRemoveLetterbox(m_autoRemoveLB);
    
    // Read Threshold
    BOOL bTrans;
    int thresh = GetDlgItemInt(m_Dlg, IDC_EDIT_LB_THRESHOLD, &bTrans, FALSE);
    if (bTrans) m_lbThreshold = thresh;
    if (m_lbThreshold > 255) m_lbThreshold = 255;
    
    // Read Stability
    int stability = GetDlgItemInt(m_Dlg, IDC_EDIT_LB_STABILITY, &bTrans, FALSE);
    if (bTrans) m_lbStability = stability;
    if (m_lbStability < 1) m_lbStability = 1;
    
    m_pSettings->SetLetterboxThreshold(m_lbThreshold);
    m_pSettings->SetLetterboxStability(m_lbStability);

    // 統計情報のリセット (設定変更の影響を確認しやすくするため)
  m_pSettings->ResetPerformanceStatistics();

  return S_OK;
}

//------------------------------------------------------------------------------
// OnReceiveMessage
//
// 概要:
//   プロパティページのダイアログメッセージプロシージャ。
//   UIコントロールの操作イベント（ボタン押下、チェックボックス切り替えなど）を処理し、
//   変更があった場合は m_bDirty フラグを立ててプロパティサイトに変更を通知します。
//
// 戻り値:
//   INT_PTR (BOOL相当): メッセージを処理した場合は TRUE (1)
//------------------------------------------------------------------------------
INT_PTR CLR2BGAFilterPropertyPage::OnReceiveMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    // 汎用バインディング処理 (Binder)
    // 値の変更があれば自動的に m_bDirty をセットし、依存関係の更新や即時反映を行う
    if (HandleAutoBinding(uMsg, wParam, lParam)) {
        return TRUE;
    }

    // 必要であればここで個別メッセージ処理を追加
    // 現在は全てのコントロールが Binder で処理されるため、追加処理は不要

    return CBasePropertyPage::OnReceiveMessage(hwnd, uMsg, wParam, lParam);
}

//------------------------------------------------------------------------------
// PropertyBinder Implementation
//------------------------------------------------------------------------------

void CLR2BGAFilterPropertyPage::InitBindings()
{
    m_bindings.clear();

    // 1. LR2出力設定 (LR2 Output Settings)
    m_bindings.push_back({ IDC_EDIT_WIDTH, BindType::Int, &m_width, 1, 4096 });
    m_bindings.push_back({ IDC_EDIT_HEIGHT, BindType::Int, &m_height, 1, 4096 });
    m_bindings.push_back({ IDC_COMBO_ALGORITHM, BindType::Combo, &m_algo });
    m_bindings.push_back({ IDC_CHECK_KEEPASPECT, BindType::Bool, &m_keepAspect });
    
    // FPS制限 (FPS Limit)
    // チェックボックスのON/OFFでFPS入力欄の有効/無効を切り替える (即時反映)
    m_bindings.push_back({ IDC_CHECK_LIMITFPS, BindType::Bool, &m_limitFPS, 0, 0, false, nullptr,
        [this]() { EnableWindow(GetDlgItem(m_Dlg, IDC_EDIT_MAXFPS), m_limitFPS); } 
    });
    m_bindings.push_back({ IDC_EDIT_MAXFPS, BindType::Int, &m_maxFPS, 1, 60 });

    // 動作モード (Operation Mode)
    m_bindings.push_back({ IDC_CHECK_DUMMY, BindType::Bool, &m_dummyMode });
    m_bindings.push_back({ IDC_CHECK_PASSTHROUGH, BindType::Bool, &m_passthroughMode, 0, 0, false, nullptr,
        [this]() {
            BOOL enable = !m_passthroughMode;
            EnableWindow(GetDlgItem(m_Dlg, IDC_EDIT_WIDTH), enable);
            EnableWindow(GetDlgItem(m_Dlg, IDC_EDIT_HEIGHT), enable);
            EnableWindow(GetDlgItem(m_Dlg, IDC_COMBO_ALGORITHM), enable);
            EnableWindow(GetDlgItem(m_Dlg, IDC_CHECK_KEEPASPECT), enable);
        }
    });

    // デバッグ設定 (Debug)
    m_bindings.push_back({ IDC_CHECK_DEBUGMODE, BindType::Bool, &m_debugMode });
    m_bindings.push_back({ IDC_CHECK_AUTO_OPEN, BindType::Bool, &m_autoOpen });

    // 2. 外部ウィンドウ設定 (External Window Settings)
    m_bindings.push_back({ IDC_CHECK_EXT_ENABLE, BindType::Bool, &m_extEnabled, 0, 0, false, nullptr,
        [this]() {
            // 複雑な依存関係の更新
            BOOL enabled = m_extEnabled;
            BOOL passthrough = m_extPassthrough;
            EnableWindow(GetDlgItem(m_Dlg, IDC_CHECK_EXT_PASSTHROUGH), enabled);
            EnableWindow(GetDlgItem(m_Dlg, IDC_EDIT_EXT_X), enabled);
            EnableWindow(GetDlgItem(m_Dlg, IDC_EDIT_EXT_Y), enabled);
            EnableWindow(GetDlgItem(m_Dlg, IDC_RADIO_EXT_TOPMOST), enabled);
            EnableWindow(GetDlgItem(m_Dlg, IDC_RADIO_EXT_BOTTOMMOST), enabled);
            
            BOOL enableSize = enabled && !passthrough;
            EnableWindow(GetDlgItem(m_Dlg, IDC_EDIT_EXT_WIDTH), enableSize);
            EnableWindow(GetDlgItem(m_Dlg, IDC_EDIT_EXT_HEIGHT), enableSize);
            EnableWindow(GetDlgItem(m_Dlg, IDC_COMBO_EXT_ALGO), enableSize);
            EnableWindow(GetDlgItem(m_Dlg, IDC_CHECK_EXT_KEEPASPECT), enableSize);
        }
    });

    m_bindings.push_back({ IDC_CHECK_EXT_PASSTHROUGH, BindType::Bool, &m_extPassthrough, 0, 0, false, nullptr,
        [this]() {
            // 上記ロジックを再利用、または個別実装
            BOOL enableSize = m_extEnabled && !m_extPassthrough;
            EnableWindow(GetDlgItem(m_Dlg, IDC_EDIT_EXT_WIDTH), enableSize);
            EnableWindow(GetDlgItem(m_Dlg, IDC_EDIT_EXT_HEIGHT), enableSize);
            EnableWindow(GetDlgItem(m_Dlg, IDC_COMBO_EXT_ALGO), enableSize);
            EnableWindow(GetDlgItem(m_Dlg, IDC_CHECK_EXT_KEEPASPECT), enableSize);
        }
    });

    m_bindings.push_back({ IDC_EDIT_EXT_X, BindType::Int, &m_extX });
    m_bindings.push_back({ IDC_EDIT_EXT_Y, BindType::Int, &m_extY });
    m_bindings.push_back({ IDC_EDIT_EXT_WIDTH, BindType::Int, &m_extWidth, 1, 4096 });
    m_bindings.push_back({ IDC_EDIT_EXT_HEIGHT, BindType::Int, &m_extHeight, 1, 4096 });
    m_bindings.push_back({ IDC_COMBO_EXT_ALGO, BindType::Combo, &m_extAlgo });
    m_bindings.push_back({ IDC_CHECK_EXT_KEEPASPECT, BindType::Bool, &m_extKeepAspect });
    
    // 最前面表示ラジオボタン (バインディングは片方のみで管理し、ApplyToUIで連携)
    // BindType::Bool で特定IDの状態を監視すれば連動する
    m_bindings.push_back({ IDC_RADIO_EXT_TOPMOST, BindType::Bool, &m_extTopmost });

    // 3. 明るさ設定 (即時反映)
    m_bindings.push_back({ IDC_SLIDER_BRIGHTNESS_LR2, BindType::Slider, &m_brightnessLR2, 0, 100, true,
        [](ILR2BGAFilterSettings* s, const Binding& b) { s->SetBrightnessLR2(*static_cast<int*>(b.pTarget)); },
        [this]() {
            wchar_t buf[16]; swprintf_s(buf, L"%d%%", m_brightnessLR2);
            SetDlgItemTextW(m_Dlg, IDC_LABEL_VAL_BRIGHTNESS_LR2, buf);
        }
    });
    
    m_bindings.push_back({ IDC_SLIDER_BRIGHTNESS_EXT, BindType::Slider, &m_brightnessExt, 0, 100, true,
        [](ILR2BGAFilterSettings* s, const Binding& b) { s->SetBrightnessExt(*static_cast<int*>(b.pTarget)); },
        [this]() {
            wchar_t buf[16]; swprintf_s(buf, L"%d%%", m_brightnessExt);
            SetDlgItemTextW(m_Dlg, IDC_LABEL_VAL_BRIGHTNESS_EXT, buf);
        }
    });

    // 4. 手動クローズ設定 (Manual Close)
    m_bindings.push_back({ IDC_CHECK_CLOSE_RCLICK, BindType::Bool, &m_closeOnRightClick });
    m_bindings.push_back({ IDC_CHECK_CLOSE_GAMEPAD, BindType::Bool, &m_gamepadCloseEnabled });
    m_bindings.push_back({ IDC_EDIT_GAMEPAD_ID, BindType::Int, &m_gamepadID });
    m_bindings.push_back({ IDC_EDIT_GAMEPAD_BTN, BindType::Int, &m_gamepadButtonID });
    m_bindings.push_back({ IDC_CHECK_CLOSE_KEYBOARD, BindType::Bool, &m_keyboardCloseEnabled });
    // IDC_EDIT_KEYBOARD_KEY はHexパースが必要なため手動処理

    // 5. 黒帯除去 (Letterbox)
    m_bindings.push_back({ IDC_CHECK_AUTO_REMOVE_LB, BindType::Bool, &m_autoRemoveLB });
    m_bindings.push_back({ IDC_EDIT_LB_THRESHOLD, BindType::Int, &m_lbThreshold, 0, 255 });
    m_bindings.push_back({ IDC_EDIT_LB_STABILITY, BindType::Int, &m_lbStability, 1, 900, false, nullptr,
        [this]() {
            wchar_t buf[32]; swprintf_s(buf, L"(approx. %d ms)", m_lbStability * 200);
            SetDlgItemTextW(m_Dlg, IDC_LABEL_LB_MS, buf);
        }
    });
}

void CLR2BGAFilterPropertyPage::ApplyToUI()
{
    // バインディング定義に従ってコントロールに値を反映
    for (const auto& b : m_bindings) {
        HWND hWnd = GetDlgItem(m_Dlg, b.id);
        if (!hWnd) continue;

        switch (b.type) {
        case BindType::Bool:
            CheckDlgButton(m_Dlg, b.id, (*(BOOL*)b.pTarget) ? BST_CHECKED : BST_UNCHECKED);
            break;
        case BindType::Int:
            // 符号付き整数として扱う
            SetDlgItemInt(m_Dlg, b.id, *(int*)b.pTarget, TRUE); 
            break;
        case BindType::Combo:
            SendMessage(hWnd, CB_SETCURSEL, (WPARAM)(*(int*)b.pTarget), 0);
            break;
        case BindType::Slider:
            SendMessage(hWnd, TBM_SETPOS, TRUE, *(int*)b.pTarget);
            break;
        }

        // 依存関係（ラベル更新、有効/無効切り替え）の実行
        if (b.onUpdateUI) b.onUpdateUI();
    }
    
    // ラジオボタンの手動処理 (Bottommost)
    if (!m_extTopmost) CheckRadioButton(m_Dlg, IDC_RADIO_EXT_TOPMOST, IDC_RADIO_EXT_BOTTOMMOST, IDC_RADIO_EXT_BOTTOMMOST);

    // 特殊処理: Hexキーコードの表示
    wchar_t buf[16];
    swprintf_s(buf, L"0x%X", m_keyboardKeyCode);
    SetDlgItemTextW(m_Dlg, IDC_EDIT_KEYBOARD_KEY, buf);
}

void CLR2BGAFilterPropertyPage::LoadFromUI()
{
    // バインディング定義に従ってコントロールから値を読み込む
    for (const auto& b : m_bindings) {
        HWND hWnd = GetDlgItem(m_Dlg, b.id);
        if (!hWnd) continue;

        switch (b.type) {
        case BindType::Bool:
            *(BOOL*)b.pTarget = (IsDlgButtonChecked(m_Dlg, b.id) == BST_CHECKED);
            break;
        case BindType::Int:
            {
                BOOL trans = FALSE;
                int val = GetDlgItemInt(m_Dlg, b.id, &trans, TRUE); // 常に符号付きをサポート
                if (trans) {
                    if (b.maxVal > b.minVal) { // 範囲チェックが設定されている場合
                        if (val < b.minVal) val = b.minVal;
                        if (val > b.maxVal) val = b.maxVal;
                    }
                    *(int*)b.pTarget = val;
                }
            }
            break;
        case BindType::Combo:
            *(int*)b.pTarget = (int)SendMessage(hWnd, CB_GETCURSEL, 0, 0);
            break;
        case BindType::Slider:
            *(int*)b.pTarget = (int)SendMessage(hWnd, TBM_GETPOS, 0, 0);
            break;
        }
    }
    
    // 特殊処理: Hexキーコードの読み取り
    wchar_t buf[32];
    GetDlgItemTextW(m_Dlg, IDC_EDIT_KEYBOARD_KEY, buf, 32);
    int keyCode = 0;
    if (swscanf_s(buf, L"0x%x", &keyCode) == 1 || swscanf_s(buf, L"%d", &keyCode) == 1) {
        m_keyboardKeyCode = keyCode;
    }
}

bool CLR2BGAFilterPropertyPage::HandleAutoBinding(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    UINT id = 0;
    UINT event = 0;
    bool isChanged = false;

    if (uMsg == WM_COMMAND) {
        id = LOWORD(wParam);
        event = HIWORD(wParam);
        
        switch (event) {
        case BN_CLICKED:
        case CBN_SELCHANGE:
            isChanged = true;
            break;
        case EN_CHANGE:
            isChanged = true;
            break;
        }
    } else if (uMsg == WM_HSCROLL) {
        isChanged = true;
        id = GetDlgCtrlID((HWND)lParam);
    }
    
    if (!isChanged || id == 0) return false;

    // バインディングを検索
    auto it = std::find_if(m_bindings.begin(), m_bindings.end(), [id](const Binding& b){ return b.id == id; });
    if (it != m_bindings.end()) {
        const Binding& b = *it;
        
        // 変更されたコントロールの値のみを取得
        // LoadFromUI のロジックと一部重複するが、単一アイテムの効率的な更新のためここで処理する
        
        HWND hWnd = GetDlgItem(m_Dlg, id);
        if (b.type == BindType::Bool) {
            *(BOOL*)b.pTarget = (IsDlgButtonChecked(m_Dlg, id) == BST_CHECKED);
        } else if (b.type == BindType::Slider) {
            *(int*)b.pTarget = (int)SendMessage(hWnd, TBM_GETPOS, 0, 0);
        } else if (b.type == BindType::Int && event == EN_CHANGE) {
             // エディットボックスの場合、リアルタイム反映は数字入力のパースが必要
             // ここでは簡易的に実装 (不正な値の場合は反映しない)
             BOOL trans;
             int val = GetDlgItemInt(m_Dlg, id, &trans, TRUE);
             if (trans) *(int*)b.pTarget = val;
        } else if (b.type == BindType::Combo) {
             *(int*)b.pTarget = (int)SendMessage(hWnd, CB_GETCURSEL, 0, 0);
        }

        // UI更新コールバックを実行 (関連コントロールの有効/無効化など)
        if (b.onUpdateUI) b.onUpdateUI();

        // 状態変更通知 (即時反映設定があればそれに従う)
        if (b.immediateApply && m_pSettings && b.applyCallback) {
            b.applyCallback(m_pSettings, b);
        } else {
            m_bDirty = TRUE;
            if (m_pPageSite) m_pPageSite->OnStatusChange(PROPPAGESTATUS_DIRTY);
        }
        
        return true;
    }
    
    return false;
}

//------------------------------------------------------------------------------
// Legacy Methods Shim
//------------------------------------------------------------------------------

void CLR2BGAFilterPropertyPage::UpdateControls()
{
    ApplyToUI();
}

void CLR2BGAFilterPropertyPage::ReadControls()
{
    LoadFromUI();
}


