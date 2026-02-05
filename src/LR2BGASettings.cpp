#include "LR2BGASettings.h"
#include <stdio.h>

// レジストリ保存先キー
const wchar_t* LR2BGASettings::REGISTRY_KEY = L"Software\\LR2BGAFilter";

// コンストラクタ: デフォルト値で初期化
LR2BGASettings::LR2BGASettings()
    : m_outputWidth(DEFAULT_OUTPUT_WIDTH)
    , m_outputHeight(DEFAULT_OUTPUT_HEIGHT)
    , m_resizeAlgo(RESIZE_BILINEAR)
    , m_keepAspectRatio(true)
    , m_passthroughMode(false)
    , m_dummyMode(false)
    , m_maxFPS(0)
    , m_debugMode(false)
    , m_extWindowEnabled(false)
    , m_extWindowX(0)
    , m_extWindowY(0)
    , m_extWindowWidth(DEFAULT_EXT_WIDTH)
    , m_extWindowHeight(DEFAULT_EXT_HEIGHT)
    , m_extWindowAlgo(RESIZE_BILINEAR)
    , m_extWindowKeepAspect(true)
    , m_extWindowPassthrough(false)

    , m_extWindowTopmost(true) // デフォルトで最前面
    , m_brightnessLR2(100)
    , m_brightnessExt(100)
    , m_autoOpenSettings(false)
    // 黒帯自動除去 (デフォルト無効)
    , m_autoRemoveLetterbox(false)
    , m_lbThreshold(16)
    , m_lbStability(5)
    
    // 手動クローズトリガー設定
    , m_closeOnRightClick(true)
    , m_gamepadCloseEnabled(false)
    , m_gamepadID(0)
    , m_gamepadButtonID(0)
    , m_keyboardCloseEnabled(false)
    , m_keyboardKeyCode(VK_ESCAPE)
{
    // InitializeCriticalSection(&m_cs); // No longer needed
}

LR2BGASettings::~LR2BGASettings()
{
    // DeleteCriticalSection(&m_cs); // No longer needed
}

// 設定をレジストリから読み込む
void LR2BGASettings::Load()
{
    Lock();
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, REGISTRY_KEY, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD data, size = sizeof(DWORD);
        // 基本設定
        if (RegQueryValueExW(hKey, L"OutputWidth", NULL, NULL, (LPBYTE)&data, &size) == ERROR_SUCCESS) m_outputWidth = data;
        if (RegQueryValueExW(hKey, L"OutputHeight", NULL, NULL, (LPBYTE)&data, &size) == ERROR_SUCCESS) m_outputHeight = data;
        if (RegQueryValueExW(hKey, L"ResizeAlgo", NULL, NULL, (LPBYTE)&data, &size) == ERROR_SUCCESS) m_resizeAlgo = (ResizeAlgorithm)data;
        if (RegQueryValueExW(hKey, L"KeepAspectRatio", NULL, NULL, (LPBYTE)&data, &size) == ERROR_SUCCESS) m_keepAspectRatio = (data != 0);
        if (RegQueryValueExW(hKey, L"DebugMode", NULL, NULL, (LPBYTE)&data, &size) == ERROR_SUCCESS) m_debugMode = (data != 0);
        if (RegQueryValueExW(hKey, L"MaxFPS", NULL, NULL, (LPBYTE)&data, &size) == ERROR_SUCCESS) m_maxFPS = data;
        if (RegQueryValueExW(hKey, L"DummyMode", NULL, NULL, (LPBYTE)&data, &size) == ERROR_SUCCESS) m_dummyMode = (data != 0);
        if (RegQueryValueExW(hKey, L"PassthroughMode", NULL, NULL, (LPBYTE)&data, &size) == ERROR_SUCCESS) m_passthroughMode = (data != 0);
        
        // 外部ウィンドウ設定
        if (RegQueryValueExW(hKey, L"ExtWindowEnabled", NULL, NULL, (LPBYTE)&data, &size) == ERROR_SUCCESS) m_extWindowEnabled = (data != 0);
        if (RegQueryValueExW(hKey, L"ExtWindowX", NULL, NULL, (LPBYTE)&data, &size) == ERROR_SUCCESS) m_extWindowX = (int)data;
        if (RegQueryValueExW(hKey, L"ExtWindowY", NULL, NULL, (LPBYTE)&data, &size) == ERROR_SUCCESS) m_extWindowY = (int)data;
        if (RegQueryValueExW(hKey, L"ExtWindowWidth", NULL, NULL, (LPBYTE)&data, &size) == ERROR_SUCCESS) m_extWindowWidth = data;
        if (RegQueryValueExW(hKey, L"ExtWindowHeight", NULL, NULL, (LPBYTE)&data, &size) == ERROR_SUCCESS) m_extWindowHeight = data;
        if (RegQueryValueExW(hKey, L"ExtWindowAlgo", NULL, NULL, (LPBYTE)&data, &size) == ERROR_SUCCESS) m_extWindowAlgo = (ResizeAlgorithm)data;
        if (RegQueryValueExW(hKey, L"ExtWindowKeepAspect", NULL, NULL, (LPBYTE)&data, &size) == ERROR_SUCCESS) m_extWindowKeepAspect = (data != 0);
        if (RegQueryValueExW(hKey, L"ExtWindowPassthrough", NULL, NULL, (LPBYTE)&data, &size) == ERROR_SUCCESS) m_extWindowPassthrough = (data != 0);
        if (RegQueryValueExW(hKey, L"ExtWindowTopmost", NULL, NULL, (LPBYTE)&data, &size) == ERROR_SUCCESS) m_extWindowTopmost = (data != 0);
        
        // 明るさ設定
        if (RegQueryValueExW(hKey, L"BrightnessLR2", NULL, NULL, (LPBYTE)&data, &size) == ERROR_SUCCESS) m_brightnessLR2 = data;
        if (RegQueryValueExW(hKey, L"BrightnessExt", NULL, NULL, (LPBYTE)&data, &size) == ERROR_SUCCESS) m_brightnessExt = data;

        // 設定画面自動オープン
        if (RegQueryValueExW(hKey, L"AutoOpenSettings", NULL, NULL, (LPBYTE)&data, &size) == ERROR_SUCCESS) m_autoOpenSettings = (data != 0);
        
        // 黒帯自動除去
        if (RegQueryValueExW(hKey, L"AutoRemoveLetterbox", NULL, NULL, (LPBYTE)&data, &size) == ERROR_SUCCESS) m_autoRemoveLetterbox = (data != 0);
        if (RegQueryValueExW(hKey, L"LetterboxThreshold", NULL, NULL, (LPBYTE)&data, &size) == ERROR_SUCCESS) m_lbThreshold = (int)data;
        if (RegQueryValueExW(hKey, L"LetterboxStability", NULL, NULL, (LPBYTE)&data, &size) == ERROR_SUCCESS) m_lbStability = (int)data;

        // 手動クローズトリガー
        if (RegQueryValueExW(hKey, L"CloseOnRightClick", NULL, NULL, (LPBYTE)&data, &size) == ERROR_SUCCESS) m_closeOnRightClick = (data != 0);
        if (RegQueryValueExW(hKey, L"GamepadCloseEnabled", NULL, NULL, (LPBYTE)&data, &size) == ERROR_SUCCESS) m_gamepadCloseEnabled = (data != 0);
        if (RegQueryValueExW(hKey, L"GamepadID", NULL, NULL, (LPBYTE)&data, &size) == ERROR_SUCCESS) m_gamepadID = (int)data;
        if (RegQueryValueExW(hKey, L"GamepadButtonID", NULL, NULL, (LPBYTE)&data, &size) == ERROR_SUCCESS) m_gamepadButtonID = (int)data;
        if (RegQueryValueExW(hKey, L"KeyboardCloseEnabled", NULL, NULL, (LPBYTE)&data, &size) == ERROR_SUCCESS) m_keyboardCloseEnabled = (data != 0);
        if (RegQueryValueExW(hKey, L"KeyboardKeyCode", NULL, NULL, (LPBYTE)&data, &size) == ERROR_SUCCESS) m_keyboardKeyCode = (int)data;

        RegCloseKey(hKey);
    }
    Unlock();
}

// 設定をレジストリへ保存
void LR2BGASettings::Save()
{
    Lock();
    HKEY hKey;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, REGISTRY_KEY, 0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
        DWORD data;
        // 基本設定
        data = m_outputWidth; RegSetValueExW(hKey, L"OutputWidth", 0, REG_DWORD, (LPBYTE)&data, sizeof(DWORD));
        data = m_outputHeight; RegSetValueExW(hKey, L"OutputHeight", 0, REG_DWORD, (LPBYTE)&data, sizeof(DWORD));
        data = (DWORD)m_resizeAlgo; RegSetValueExW(hKey, L"ResizeAlgo", 0, REG_DWORD, (LPBYTE)&data, sizeof(DWORD));
        data = m_keepAspectRatio ? 1 : 0; RegSetValueExW(hKey, L"KeepAspectRatio", 0, REG_DWORD, (LPBYTE)&data, sizeof(DWORD));
        data = m_debugMode ? 1 : 0; RegSetValueExW(hKey, L"DebugMode", 0, REG_DWORD, (LPBYTE)&data, sizeof(DWORD));
        data = m_maxFPS; RegSetValueExW(hKey, L"MaxFPS", 0, REG_DWORD, (LPBYTE)&data, sizeof(DWORD));
        data = m_dummyMode ? 1 : 0; RegSetValueExW(hKey, L"DummyMode", 0, REG_DWORD, (LPBYTE)&data, sizeof(DWORD));
        data = m_passthroughMode ? 1 : 0; RegSetValueExW(hKey, L"PassthroughMode", 0, REG_DWORD, (LPBYTE)&data, sizeof(DWORD));

        // 外部ウィンドウ設定
        data = m_extWindowEnabled ? 1 : 0; RegSetValueExW(hKey, L"ExtWindowEnabled", 0, REG_DWORD, (LPBYTE)&data, sizeof(DWORD));
        data = (DWORD)m_extWindowX; RegSetValueExW(hKey, L"ExtWindowX", 0, REG_DWORD, (LPBYTE)&data, sizeof(DWORD));
        data = (DWORD)m_extWindowY; RegSetValueExW(hKey, L"ExtWindowY", 0, REG_DWORD, (LPBYTE)&data, sizeof(DWORD));
        data = m_extWindowWidth; RegSetValueExW(hKey, L"ExtWindowWidth", 0, REG_DWORD, (LPBYTE)&data, sizeof(DWORD));
        data = m_extWindowHeight; RegSetValueExW(hKey, L"ExtWindowHeight", 0, REG_DWORD, (LPBYTE)&data, sizeof(DWORD));
        data = (DWORD)m_extWindowAlgo; RegSetValueExW(hKey, L"ExtWindowAlgo", 0, REG_DWORD, (LPBYTE)&data, sizeof(DWORD));
        data = m_extWindowKeepAspect ? 1 : 0; RegSetValueExW(hKey, L"ExtWindowKeepAspect", 0, REG_DWORD, (LPBYTE)&data, sizeof(DWORD));
        data = m_extWindowPassthrough ? 1 : 0; RegSetValueExW(hKey, L"ExtWindowPassthrough", 0, REG_DWORD, (LPBYTE)&data, sizeof(DWORD));
        data = m_extWindowTopmost ? 1 : 0; RegSetValueExW(hKey, L"ExtWindowTopmost", 0, REG_DWORD, (LPBYTE)&data, sizeof(DWORD));
        
        // 明るさ設定
        data = m_brightnessLR2; RegSetValueExW(hKey, L"BrightnessLR2", 0, REG_DWORD, (LPBYTE)&data, sizeof(DWORD));
        data = m_brightnessExt; RegSetValueExW(hKey, L"BrightnessExt", 0, REG_DWORD, (LPBYTE)&data, sizeof(DWORD));

        // 設定画面自動オープン
        data = m_autoOpenSettings ? 1 : 0; RegSetValueExW(hKey, L"AutoOpenSettings", 0, REG_DWORD, (LPBYTE)&data, sizeof(DWORD));
        
        // 黒帯自動除去
        data = m_autoRemoveLetterbox ? 1 : 0; RegSetValueExW(hKey, L"AutoRemoveLetterbox", 0, REG_DWORD, (LPBYTE)&data, sizeof(DWORD));
        data = (DWORD)m_lbThreshold; RegSetValueExW(hKey, L"LetterboxThreshold", 0, REG_DWORD, (LPBYTE)&data, sizeof(DWORD));
        data = (DWORD)m_lbStability; RegSetValueExW(hKey, L"LetterboxStability", 0, REG_DWORD, (LPBYTE)&data, sizeof(DWORD));

        // 手動クローズトリガー
        data = m_closeOnRightClick ? 1 : 0; RegSetValueExW(hKey, L"CloseOnRightClick", 0, REG_DWORD, (LPBYTE)&data, sizeof(DWORD));
        data = m_gamepadCloseEnabled ? 1 : 0; RegSetValueExW(hKey, L"GamepadCloseEnabled", 0, REG_DWORD, (LPBYTE)&data, sizeof(DWORD));
        data = (DWORD)m_gamepadID; RegSetValueExW(hKey, L"GamepadID", 0, REG_DWORD, (LPBYTE)&data, sizeof(DWORD));
        data = (DWORD)m_gamepadButtonID; RegSetValueExW(hKey, L"GamepadButtonID", 0, REG_DWORD, (LPBYTE)&data, sizeof(DWORD));
        data = m_keyboardCloseEnabled ? 1 : 0; RegSetValueExW(hKey, L"KeyboardCloseEnabled", 0, REG_DWORD, (LPBYTE)&data, sizeof(DWORD));
        data = (DWORD)m_keyboardKeyCode; RegSetValueExW(hKey, L"KeyboardKeyCode", 0, REG_DWORD, (LPBYTE)&data, sizeof(DWORD));

        RegCloseKey(hKey);
    }
    Unlock();
}


