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
    , m_lbStability(5)
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

    // フィルタから現在の設定値を読み込む
    m_pSettings->GetOutputSize(&m_width, &m_height);
    m_pSettings->GetResizeAlgorithm(&m_algo);
    m_pSettings->GetKeepAspectRatio(&m_keepAspect);
    m_pSettings->GetDebugMode(&m_debugMode);
    m_pSettings->GetMaxFPS(&m_maxFPS);
    m_pSettings->GetDummyMode(&m_dummyMode);
    m_pSettings->GetPassthroughMode(&m_passthroughMode);
    
    m_pSettings->GetExternalWindowEnabled(&m_extEnabled);
    m_pSettings->GetExternalWindowPosition(&m_extX, &m_extY);
    m_pSettings->GetExternalWindowSize(&m_extWidth, &m_extHeight);
    m_pSettings->GetExternalWindowAlgorithm(&m_extAlgo);
    m_pSettings->GetExternalWindowKeepAspect(&m_extKeepAspect);
    m_pSettings->GetExternalWindowPassthrough(&m_extPassthrough);
    m_pSettings->GetExternalWindowTopmost(&m_extTopmost);

    // 手動クローズ設定 (Manual Close)
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

    return S_OK;
}

//------------------------------------------------------------------------------
// OnDisconnect
// フィルタとの接続が解除された際に呼び出される
//------------------------------------------------------------------------------
HRESULT CLR2BGAFilterPropertyPage::OnDisconnect()
{
    if (m_pSettings) {
        m_pSettings->Release();
        m_pSettings = NULL;
    }
    return S_OK;
}

//------------------------------------------------------------------------------
// OnActivate
// プロパティページが表示（アクティブ化）された際に呼び出される
//------------------------------------------------------------------------------
HRESULT CLR2BGAFilterPropertyPage::OnActivate()
{
    // Set up spin controls
    HWND hSpinWidth = GetDlgItem(m_Dlg, IDC_SPIN_WIDTH);
    HWND hSpinHeight = GetDlgItem(m_Dlg, IDC_SPIN_HEIGHT);
    HWND hSpinMaxFPS = GetDlgItem(m_Dlg, IDC_SPIN_MAXFPS);
    
    SendMessage(hSpinWidth, UDM_SETRANGE32, 1, 4096);
    SendMessage(hSpinHeight, UDM_SETRANGE32, 1, 4096);
    SendMessage(hSpinMaxFPS, UDM_SETRANGE32, 1, 60);

    // External Window Spins
    SendMessage(GetDlgItem(m_Dlg, IDC_SPIN_EXT_X), UDM_SETRANGE32, -4096, 4096);
    SendMessage(GetDlgItem(m_Dlg, IDC_SPIN_EXT_Y), UDM_SETRANGE32, -4096, 4096);
    SendMessage(GetDlgItem(m_Dlg, IDC_SPIN_EXT_WIDTH), UDM_SETRANGE32, 1, 4096);
    SendMessage(GetDlgItem(m_Dlg, IDC_SPIN_EXT_HEIGHT), UDM_SETRANGE32, 1, 4096);

    // Gamepad Spins
    SendMessage(GetDlgItem(m_Dlg, IDC_SPIN_GAMEPAD_ID), UDM_SETRANGE32, 0, 15);
    SendMessage(GetDlgItem(m_Dlg, IDC_SPIN_GAMEPAD_BTN), UDM_SETRANGE32, 0, 31);

    ILR2BGAFilterSettings* pSettings = m_pSettings;

    // Populate algorithm combo box (Main)
    HWND hCombo = GetDlgItem(m_Dlg, IDC_COMBO_ALGORITHM);
    SendMessage(hCombo, CB_ADDSTRING, 0, (LPARAM)L"Nearest Neighbor (Fast)");
    SendMessage(hCombo, CB_ADDSTRING, 0, (LPARAM)L"Bilinear (Balanced)");

    // Populate algorithm combo box (External)
    HWND hComboExt = GetDlgItem(m_Dlg, IDC_COMBO_EXT_ALGO);
    SendMessage(hComboExt, CB_ADDSTRING, 0, (LPARAM)L"Nearest Neighbor (Fast)");
    SendMessage(hComboExt, CB_ADDSTRING, 0, (LPARAM)L"Bilinear (Balanced)");

    BOOL bAutoOpen = FALSE;
    if (pSettings) pSettings->GetAutoOpenSettings(&bAutoOpen);
    CheckDlgButton(m_Dlg, IDC_CHECK_AUTO_OPEN, bAutoOpen ? BST_CHECKED : BST_UNCHECKED);
    
    // Auto Remove LB
    CheckDlgButton(m_Dlg, IDC_CHECK_AUTO_REMOVE_LB, m_autoRemoveLB ? BST_CHECKED : BST_UNCHECKED);

    // Update controls with current values
    UpdateControls();

    // Brightness Slider Init
    HWND hSliderLR2 = GetDlgItem(m_Dlg, IDC_SLIDER_BRIGHTNESS_LR2);
    SendMessage(hSliderLR2, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
    int bLR2 = 100;
    if (m_pSettings) m_pSettings->GetBrightnessLR2(&bLR2);
    SendMessage(hSliderLR2, TBM_SETPOS, TRUE, bLR2);
    
    wchar_t buf[16];
    swprintf_s(buf, L"%d%%", bLR2);
    SetDlgItemTextW(m_Dlg, IDC_LABEL_VAL_BRIGHTNESS_LR2, buf);

    HWND hSliderExt = GetDlgItem(m_Dlg, IDC_SLIDER_BRIGHTNESS_EXT);
    SendMessage(hSliderExt, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
    int bExt = 100;
    if (m_pSettings) m_pSettings->GetBrightnessExt(&bExt);
    SendMessage(hSliderExt, TBM_SETPOS, TRUE, bExt);
    
    swprintf_s(buf, L"%d%%", bExt);
    SetDlgItemTextW(m_Dlg, IDC_LABEL_VAL_BRIGHTNESS_EXT, buf);

    // Auto Remove LB - Spin Ranges
    SendMessage(GetDlgItem(m_Dlg, IDC_SPIN_LB_THRESHOLD), UDM_SETRANGE32, 0, 255);
    SendMessage(GetDlgItem(m_Dlg, IDC_SPIN_LB_STABILITY), UDM_SETRANGE32, 1, 900);

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
    switch (uMsg) {
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_EDIT_WIDTH:
        case IDC_EDIT_HEIGHT:
            if (HIWORD(wParam) == EN_CHANGE) {
                m_bDirty = TRUE;
                if (m_pPageSite) m_pPageSite->OnStatusChange(PROPPAGESTATUS_DIRTY);
            }
            break;
        case IDC_COMBO_ALGORITHM:
        case IDC_COMBO_EXT_ALGO:
            if (HIWORD(wParam) == CBN_SELCHANGE) {
                m_bDirty = TRUE;
                if (m_pPageSite) m_pPageSite->OnStatusChange(PROPPAGESTATUS_DIRTY);
            }
            break;
        case IDC_CHECK_KEEPASPECT:
        case IDC_CHECK_DEBUGMODE:
        case IDC_CHECK_EXT_KEEPASPECT:
        case IDC_RADIO_EXT_TOPMOST:
        case IDC_RADIO_EXT_BOTTOMMOST:
        case IDC_CHECK_DUMMY:
        case IDC_CHECK_AUTO_OPEN:
        case IDC_CHECK_CLOSE_RCLICK:
        case IDC_CHECK_CLOSE_GAMEPAD:
        case IDC_CHECK_CLOSE_KEYBOARD:
        case IDC_CHECK_AUTO_REMOVE_LB:
            // For simple checkboxes/radios, always mark dirty on click
            m_bDirty = TRUE;
            if (m_pPageSite) m_pPageSite->OnStatusChange(PROPPAGESTATUS_DIRTY);
            break;
        
        case IDC_CHECK_PASSTHROUGH:
            {
                m_bDirty = TRUE;
                if (m_pPageSite) m_pPageSite->OnStatusChange(PROPPAGESTATUS_DIRTY);
                // Enable/disable LR2 output controls
                BOOL passthrough = IsDlgButtonChecked(m_Dlg, IDC_CHECK_PASSTHROUGH) == BST_CHECKED;
                BOOL enable = !passthrough;
                EnableWindow(GetDlgItem(m_Dlg, IDC_EDIT_WIDTH), enable);
                EnableWindow(GetDlgItem(m_Dlg, IDC_EDIT_HEIGHT), enable);
                EnableWindow(GetDlgItem(m_Dlg, IDC_COMBO_ALGORITHM), enable);
                EnableWindow(GetDlgItem(m_Dlg, IDC_CHECK_KEEPASPECT), enable);
            }
            break;

        case IDC_CHECK_EXT_ENABLE:
            {
                m_bDirty = TRUE;
                if (m_pPageSite) m_pPageSite->OnStatusChange(PROPPAGESTATUS_DIRTY);
                
                BOOL enabled = IsDlgButtonChecked(m_Dlg, IDC_CHECK_EXT_ENABLE) == BST_CHECKED;
                BOOL passthrough = IsDlgButtonChecked(m_Dlg, IDC_CHECK_EXT_PASSTHROUGH) == BST_CHECKED;
                
                // Enable/Disable main external controls
                EnableWindow(GetDlgItem(m_Dlg, IDC_CHECK_EXT_PASSTHROUGH), enabled);
                EnableWindow(GetDlgItem(m_Dlg, IDC_EDIT_EXT_X), enabled);
                EnableWindow(GetDlgItem(m_Dlg, IDC_EDIT_EXT_Y), enabled);
                EnableWindow(GetDlgItem(m_Dlg, IDC_RADIO_EXT_TOPMOST), enabled);
                EnableWindow(GetDlgItem(m_Dlg, IDC_RADIO_EXT_BOTTOMMOST), enabled);
                
                // Enable/Disable size/algo controls (dependent on passthrough)
                BOOL enableSize = enabled && !passthrough;
                EnableWindow(GetDlgItem(m_Dlg, IDC_EDIT_EXT_WIDTH), enableSize);
                EnableWindow(GetDlgItem(m_Dlg, IDC_EDIT_EXT_HEIGHT), enableSize);
                EnableWindow(GetDlgItem(m_Dlg, IDC_COMBO_EXT_ALGO), enableSize);
                EnableWindow(GetDlgItem(m_Dlg, IDC_CHECK_EXT_KEEPASPECT), enableSize);
            }
            break;

        case IDC_CHECK_EXT_PASSTHROUGH:
            {
                m_bDirty = TRUE;
                if (m_pPageSite) m_pPageSite->OnStatusChange(PROPPAGESTATUS_DIRTY);
                
                BOOL enabled = IsDlgButtonChecked(m_Dlg, IDC_CHECK_EXT_ENABLE) == BST_CHECKED;
                BOOL passthrough = IsDlgButtonChecked(m_Dlg, IDC_CHECK_EXT_PASSTHROUGH) == BST_CHECKED;
                BOOL enableSize = enabled && !passthrough;
                
                EnableWindow(GetDlgItem(m_Dlg, IDC_EDIT_EXT_WIDTH), enableSize);
                EnableWindow(GetDlgItem(m_Dlg, IDC_EDIT_EXT_HEIGHT), enableSize);
                EnableWindow(GetDlgItem(m_Dlg, IDC_COMBO_EXT_ALGO), enableSize);
                EnableWindow(GetDlgItem(m_Dlg, IDC_CHECK_EXT_KEEPASPECT), enableSize);
            }
            break;

        case IDC_CHECK_LIMITFPS:
            {
                m_bDirty = TRUE;
                if (m_pPageSite) m_pPageSite->OnStatusChange(PROPPAGESTATUS_DIRTY);
                // Enable/disable Max FPS edit based on checkbox state
                BOOL checked = IsDlgButtonChecked(m_Dlg, IDC_CHECK_LIMITFPS) == BST_CHECKED;
                EnableWindow(GetDlgItem(m_Dlg, IDC_EDIT_MAXFPS), checked);
            }
            break;
        case IDC_EDIT_MAXFPS:
        case IDC_EDIT_EXT_X:
        case IDC_EDIT_EXT_Y:
        case IDC_EDIT_EXT_WIDTH:
        case IDC_EDIT_EXT_HEIGHT:
        case IDC_EDIT_GAMEPAD_ID:
        case IDC_EDIT_GAMEPAD_BTN:
        case IDC_EDIT_KEYBOARD_KEY:
            if (HIWORD(wParam) == EN_CHANGE) {
                m_bDirty = TRUE;
                if (m_pPageSite) m_pPageSite->OnStatusChange(PROPPAGESTATUS_DIRTY);
            }
            break;

        case IDC_EDIT_LB_THRESHOLD:
             if (HIWORD(wParam) == EN_CHANGE) {
                m_bDirty = TRUE;
                if (m_pPageSite) m_pPageSite->OnStatusChange(PROPPAGESTATUS_DIRTY);
            }
            break;

        case IDC_EDIT_LB_STABILITY:
             if (HIWORD(wParam) == EN_CHANGE) {
                m_bDirty = TRUE;
                if (m_pPageSite) m_pPageSite->OnStatusChange(PROPPAGESTATUS_DIRTY);
                
                // Live update MS
                int val = GetDlgItemInt(m_Dlg, IDC_EDIT_LB_STABILITY, NULL, FALSE);
                wchar_t buf[32];
                swprintf_s(buf, L"(approx. %d ms)", val * 200);
                SetDlgItemTextW(m_Dlg, IDC_LABEL_LB_MS, buf);
            }
            break;


        }
        break;
        
    case WM_HSCROLL:
        {
            // Update labels and SetDirty
            HWND hSlider = (HWND)lParam;
            int pos = (int)SendMessage(hSlider, TBM_GETPOS, 0, 0);
            wchar_t buf[16];
            swprintf_s(buf, L"%d%%", pos);
            
            int id = GetDlgCtrlID(hSlider);
            if (id == IDC_SLIDER_BRIGHTNESS_LR2) {
                SetDlgItemTextW(m_Dlg, IDC_LABEL_VAL_BRIGHTNESS_LR2, buf);
                m_bDirty = TRUE;
                if (m_pPageSite) m_pPageSite->OnStatusChange(PROPPAGESTATUS_DIRTY);
                // Realtime update
                if (m_pSettings) {
                     m_pSettings->SetBrightnessLR2(pos);
                }
            } else if (id == IDC_SLIDER_BRIGHTNESS_EXT) {
                SetDlgItemTextW(m_Dlg, IDC_LABEL_VAL_BRIGHTNESS_EXT, buf);
                m_bDirty = TRUE;
                if (m_pPageSite) m_pPageSite->OnStatusChange(PROPPAGESTATUS_DIRTY);
                if (m_pSettings) {
                     m_pSettings->SetBrightnessExt(pos);
                }
                if (m_pSettings) {
                     m_pSettings->SetBrightnessExt(pos);
                }
            }
        }
        break;
    }

    return CBasePropertyPage::OnReceiveMessage(hwnd, uMsg, wParam, lParam);
}

//------------------------------------------------------------------------------
// UpdateControls
// メンバ変数の値をダイアログのコントロールに反映する
//------------------------------------------------------------------------------
void CLR2BGAFilterPropertyPage::UpdateControls()
{
    // 出力幅 (Width)
    SetDlgItemInt(m_Dlg, IDC_EDIT_WIDTH, m_width, FALSE);
    
    // 出力高さ (Height)
    SetDlgItemInt(m_Dlg, IDC_EDIT_HEIGHT, m_height, FALSE);
    
    // リサイズアルゴリズム (Algorithm)
    HWND hCombo = GetDlgItem(m_Dlg, IDC_COMBO_ALGORITHM);
    SendMessage(hCombo, CB_SETCURSEL, (WPARAM)m_algo, 0);
    
    // アスペクト比維持 (Keep aspect ratio)
    CheckDlgButton(m_Dlg, IDC_CHECK_KEEPASPECT, m_keepAspect ? BST_CHECKED : BST_UNCHECKED);
    
    // デバッグモード (Debug mode)
    CheckDlgButton(m_Dlg, IDC_CHECK_DEBUGMODE, m_debugMode ? BST_CHECKED : BST_UNCHECKED);
    
    // Max FPS
    if (m_maxFPS > 0) {
        CheckDlgButton(m_Dlg, IDC_CHECK_LIMITFPS, BST_CHECKED);
        SetDlgItemInt(m_Dlg, IDC_EDIT_MAXFPS, m_maxFPS, FALSE);
        EnableWindow(GetDlgItem(m_Dlg, IDC_EDIT_MAXFPS), TRUE);
    } else {
        CheckDlgButton(m_Dlg, IDC_CHECK_LIMITFPS, BST_UNCHECKED);
        SetDlgItemInt(m_Dlg, IDC_EDIT_MAXFPS, 30, FALSE);
        EnableWindow(GetDlgItem(m_Dlg, IDC_EDIT_MAXFPS), FALSE);
    }

    // ダミーモード＆パススルーモード (Dummy & Passthrough)
    CheckDlgButton(m_Dlg, IDC_CHECK_DUMMY, m_dummyMode ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(m_Dlg, IDC_CHECK_PASSTHROUGH, m_passthroughMode ? BST_CHECKED : BST_UNCHECKED);
    
    // パススルー設定に基づいてLR2出力設定コントロールの有効/無効を切り替え
    BOOL enableLR2 = !m_passthroughMode;
    EnableWindow(GetDlgItem(m_Dlg, IDC_EDIT_WIDTH), enableLR2);
    EnableWindow(GetDlgItem(m_Dlg, IDC_EDIT_HEIGHT), enableLR2);
    EnableWindow(GetDlgItem(m_Dlg, IDC_COMBO_ALGORITHM), enableLR2);
    EnableWindow(GetDlgItem(m_Dlg, IDC_CHECK_KEEPASPECT), enableLR2);

    // 外部ウィンドウ設定 (External Window)
    CheckDlgButton(m_Dlg, IDC_CHECK_EXT_ENABLE, m_extEnabled ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(m_Dlg, IDC_CHECK_EXT_PASSTHROUGH, m_extPassthrough ? BST_CHECKED : BST_UNCHECKED);
    
    SetDlgItemInt(m_Dlg, IDC_EDIT_EXT_X, m_extX, TRUE);
    SetDlgItemInt(m_Dlg, IDC_EDIT_EXT_Y, m_extY, TRUE);
    SetDlgItemInt(m_Dlg, IDC_EDIT_EXT_WIDTH, m_extWidth, FALSE);
    SetDlgItemInt(m_Dlg, IDC_EDIT_EXT_HEIGHT, m_extHeight, FALSE);
    
    HWND hComboExt = GetDlgItem(m_Dlg, IDC_COMBO_EXT_ALGO);
    SendMessage(hComboExt, CB_SETCURSEL, (WPARAM)m_extAlgo, 0);
    
    CheckDlgButton(m_Dlg, IDC_CHECK_EXT_KEEPASPECT, m_extKeepAspect ? BST_CHECKED : BST_UNCHECKED);
    
    if (m_extTopmost) CheckRadioButton(m_Dlg, IDC_RADIO_EXT_TOPMOST, IDC_RADIO_EXT_BOTTOMMOST, IDC_RADIO_EXT_TOPMOST);
    else CheckRadioButton(m_Dlg, IDC_RADIO_EXT_TOPMOST, IDC_RADIO_EXT_BOTTOMMOST, IDC_RADIO_EXT_BOTTOMMOST);

    // 外部ウィンドウのコントロール初期状態設定
    // パススルー有効時はサイズやアルゴリズム設定を無効化する
    BOOL enableExtSize = m_extEnabled && !m_extPassthrough;
    EnableWindow(GetDlgItem(m_Dlg, IDC_EDIT_EXT_WIDTH), enableExtSize);
    EnableWindow(GetDlgItem(m_Dlg, IDC_EDIT_EXT_HEIGHT), enableExtSize);
    EnableWindow(GetDlgItem(m_Dlg, IDC_COMBO_EXT_ALGO), enableExtSize);
    EnableWindow(GetDlgItem(m_Dlg, IDC_CHECK_EXT_KEEPASPECT), enableExtSize);
    

    
    // Enable/Disable External Window Controls
    EnableWindow(GetDlgItem(m_Dlg, IDC_CHECK_EXT_PASSTHROUGH), m_extEnabled);
    EnableWindow(GetDlgItem(m_Dlg, IDC_EDIT_EXT_X), m_extEnabled);
    EnableWindow(GetDlgItem(m_Dlg, IDC_EDIT_EXT_Y), m_extEnabled);
    EnableWindow(GetDlgItem(m_Dlg, IDC_RADIO_EXT_TOPMOST), m_extEnabled);
    EnableWindow(GetDlgItem(m_Dlg, IDC_RADIO_EXT_BOTTOMMOST), m_extEnabled);
    


    // 手動クローズ (Manual Close)
    CheckDlgButton(m_Dlg, IDC_CHECK_CLOSE_RCLICK, m_closeOnRightClick ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(m_Dlg, IDC_CHECK_CLOSE_GAMEPAD, m_gamepadCloseEnabled ? BST_CHECKED : BST_UNCHECKED);
    SetDlgItemInt(m_Dlg, IDC_EDIT_GAMEPAD_ID, m_gamepadID, FALSE);
    SetDlgItemInt(m_Dlg, IDC_EDIT_GAMEPAD_BTN, m_gamepadButtonID, FALSE);
    
    CheckDlgButton(m_Dlg, IDC_CHECK_CLOSE_KEYBOARD, m_keyboardCloseEnabled ? BST_CHECKED : BST_UNCHECKED);
    
    // Hex display for KeyCode
    wchar_t buf[16];
    swprintf_s(buf, L"0x%X", m_keyboardKeyCode);
    SetDlgItemTextW(m_Dlg, IDC_EDIT_KEYBOARD_KEY, buf);

    // レターボックス設定 (Letterbox settings)
    CheckDlgButton(m_Dlg, IDC_CHECK_AUTO_REMOVE_LB, m_autoRemoveLB ? BST_CHECKED : BST_UNCHECKED);
    
    SetDlgItemInt(m_Dlg, IDC_EDIT_LB_THRESHOLD, m_lbThreshold, FALSE);
    SetDlgItemInt(m_Dlg, IDC_EDIT_LB_STABILITY, m_lbStability, FALSE);

    // MS Calc
    wchar_t buf2[32];
    swprintf_s(buf2, L"(approx. %d ms)", m_lbStability * 200);
    SetDlgItemTextW(m_Dlg, IDC_LABEL_LB_MS, buf2);
    SetDlgItemInt(m_Dlg, IDC_EDIT_LB_STABILITY, m_lbStability, FALSE);
}

//------------------------------------------------------------------------------
// ReadControls
// ダイアログのコントロールから値を読み取り、メンバ変数に格納する
//------------------------------------------------------------------------------
void CLR2BGAFilterPropertyPage::ReadControls()
{
    // 出力幅 (Width)
    BOOL bTranslated;
    m_width = GetDlgItemInt(m_Dlg, IDC_EDIT_WIDTH, &bTranslated, FALSE);
    if (!bTranslated || m_width < 1) m_width = 256;
    if (m_width > 4096) m_width = 4096;

    // 出力高さ (Height)
    m_height = GetDlgItemInt(m_Dlg, IDC_EDIT_HEIGHT, &bTranslated, FALSE);
    if (!bTranslated || m_height < 1) m_height = 256;
    if (m_height > 4096) m_height = 4096;

    // Algorithm
    HWND hCombo = GetDlgItem(m_Dlg, IDC_COMBO_ALGORITHM);
    int sel = (int)SendMessage(hCombo, CB_GETCURSEL, 0, 0);
    if (sel == 0) m_algo = RESIZE_NEAREST;
    else m_algo = RESIZE_BILINEAR;

    // Aspect ratio
    m_keepAspect = IsDlgButtonChecked(m_Dlg, IDC_CHECK_KEEPASPECT) == BST_CHECKED;
    
    // Debug mode
    m_debugMode = IsDlgButtonChecked(m_Dlg, IDC_CHECK_DEBUGMODE) == BST_CHECKED;
    
    // FPS制限 (Max FPS)
    if (IsDlgButtonChecked(m_Dlg, IDC_CHECK_LIMITFPS) == BST_CHECKED) {
        int fps = GetDlgItemInt(m_Dlg, IDC_EDIT_MAXFPS, &bTranslated, FALSE);
        if (!bTranslated || fps < 1) fps = 30;
        if (fps > 60) fps = 60;
        m_maxFPS = fps;
    } else {
        m_maxFPS = 0; // No limit
    }
    
    // Dummy & Passthrough
    m_dummyMode = IsDlgButtonChecked(m_Dlg, IDC_CHECK_DUMMY) == BST_CHECKED;
    m_passthroughMode = IsDlgButtonChecked(m_Dlg, IDC_CHECK_PASSTHROUGH) == BST_CHECKED;

    // 外部ウィンドウ設定 (External Window)
    m_extEnabled = IsDlgButtonChecked(m_Dlg, IDC_CHECK_EXT_ENABLE) == BST_CHECKED;
    m_extPassthrough = IsDlgButtonChecked(m_Dlg, IDC_CHECK_EXT_PASSTHROUGH) == BST_CHECKED;
    
    m_extX = GetDlgItemInt(m_Dlg, IDC_EDIT_EXT_X, &bTranslated, TRUE); // Signed
    m_extY = GetDlgItemInt(m_Dlg, IDC_EDIT_EXT_Y, &bTranslated, TRUE); // Signed
    
    m_extWidth = GetDlgItemInt(m_Dlg, IDC_EDIT_EXT_WIDTH, &bTranslated, FALSE);
    if (!bTranslated || m_extWidth < 1) m_extWidth = 512;
    if (m_extWidth > 4096) m_extWidth = 4096;
    
    m_extHeight = GetDlgItemInt(m_Dlg, IDC_EDIT_EXT_HEIGHT, &bTranslated, FALSE);
    if (!bTranslated || m_extHeight < 1) m_extHeight = 512;
    if (m_extHeight > 4096) m_extHeight = 4096;
    
    HWND hComboExt = GetDlgItem(m_Dlg, IDC_COMBO_EXT_ALGO);
    sel = (int)SendMessage(hComboExt, CB_GETCURSEL, 0, 0);
    if (sel == 0) m_extAlgo = RESIZE_NEAREST;
    else m_extAlgo = RESIZE_BILINEAR;
    
    m_extKeepAspect = IsDlgButtonChecked(m_Dlg, IDC_CHECK_EXT_KEEPASPECT) == BST_CHECKED;

    m_extTopmost = IsDlgButtonChecked(m_Dlg, IDC_RADIO_EXT_TOPMOST) == BST_CHECKED;

    // 手動クローズ (Manual Close)
    m_closeOnRightClick = IsDlgButtonChecked(m_Dlg, IDC_CHECK_CLOSE_RCLICK) == BST_CHECKED;
    m_gamepadCloseEnabled = IsDlgButtonChecked(m_Dlg, IDC_CHECK_CLOSE_GAMEPAD) == BST_CHECKED;
    m_gamepadID = GetDlgItemInt(m_Dlg, IDC_EDIT_GAMEPAD_ID, &bTranslated, FALSE);
    m_gamepadButtonID = GetDlgItemInt(m_Dlg, IDC_EDIT_GAMEPAD_BTN, &bTranslated, FALSE);
    m_keyboardCloseEnabled = IsDlgButtonChecked(m_Dlg, IDC_CHECK_CLOSE_KEYBOARD) == BST_CHECKED;
    
    // 自動レターボックス除去 (Auto Remove LB)
    m_autoRemoveLB = IsDlgButtonChecked(m_Dlg, IDC_CHECK_AUTO_REMOVE_LB) == BST_CHECKED;
    
    m_lbThreshold = GetDlgItemInt(m_Dlg, IDC_EDIT_LB_THRESHOLD, &bTranslated, FALSE);
    m_lbStability = GetDlgItemInt(m_Dlg, IDC_EDIT_LB_STABILITY, &bTranslated, FALSE);
    
    // キーコードのパース (10進数 または 0x付き16進数)
    wchar_t buf[32];
    GetDlgItemTextW(m_Dlg, IDC_EDIT_KEYBOARD_KEY, buf, 32);
    int keyCode = 0;
    if (swscanf_s(buf, L"0x%x", &keyCode) == 1 || swscanf_s(buf, L"%d", &keyCode) == 1) {
        m_keyboardKeyCode = keyCode;
    }
}


