#pragma once
#include <windows.h>
#include <mutex>
#include "LR2BGATypes.h"

// デフォルト値定数
#define DEFAULT_OUTPUT_WIDTH 256  // LR2への出力幅 (BGA領域の標準サイズ)
#define DEFAULT_OUTPUT_HEIGHT 256 // LR2への出力高さ
#define DEFAULT_EXT_WIDTH 512     // 外部ウィンドウのデフォルト幅
#define DEFAULT_EXT_HEIGHT 512    // 外部ウィンドウのデフォルト高さ

class LR2BGASettings {
public:
    LR2BGASettings();
    ~LR2BGASettings();

    void Load();
    void Save();

    // LR2出力設定 (LR2 Output Settings)
    int m_outputWidth;              // 出力幅 (LR2に渡す画像サイズ)
    int m_outputHeight;             // 出力高さ
    ResizeAlgorithm m_resizeAlgo;   // リサイズアルゴリズム (Nearest/Bilinear)
    bool m_keepAspectRatio;         // アスペクト比を維持するか (黒帯が入る可能性あり)
    bool m_passthroughMode;         // パススルーモード有効化 (処理スキップ)
    bool m_dummyMode;               // ダミー出力モード (1x1ピクセル等の軽量出力をLR2へ渡す)
    int m_maxFPS;                   // FPS制限値 (ターゲットFPS)
    bool m_limitFPSEnabled;         // FPS制限を有効にするかどうか (チェックボックスの状態)

    // デバッグ設定 (Debug Settings)
    bool m_debugMode;               // デバッグモード有効化 (ログ出力など)

    // 外部ウィンドウ設定 (External Window Settings)
    bool m_extWindowEnabled;        // 外部ウィンドウを表示するか
    int m_extWindowX;               // ウィンドウX座標
    int m_extWindowY;               // ウィンドウY座標
    int m_extWindowWidth;           // ウィンドウ幅
    int m_extWindowHeight;          // ウィンドウ高さ
    ResizeAlgorithm m_extWindowAlgo;// 外部ウィンドウ用リサイズアルゴリズム
    bool m_extWindowKeepAspect;     // アスペクト比維持 (外部ウィンドウ)
    bool m_extWindowPassthrough;    // ソース同期モード (リサイズせず入力解像度で表示)
    bool m_extWindowTopmost;        // 最前面表示 (Topmost)
    
    // デバッグウィンドウ位置設定
    int m_debugWindowX;
    int m_debugWindowY;
    int m_debugWindowWidth;
    int m_debugWindowHeight;


    // スレッドセーフアクセス用設定構造体
    // 設定ダイアログとフィルタ処理スレッド間で安全に設定を受け渡すために使用
    struct ExtWindowConfig {
        bool enabled;
        int x, y;
        int width, height;
        ResizeAlgorithm algo;
        bool keepAspect;
        bool passthrough;
        bool topmost;
        int brightness;
        bool autoRemoveLetterbox; // 追加: 自動黒帯除去設定
        
        // クローズトリガー設定
        bool closeOnRightClick;
        bool closeOnResult; // 追加: リザルト画面で閉じる
        bool gamepadClose;
        int gamepadID;
        int gamepadBtn;
        bool keyboardClose;
        int keyboardKey;
    };

    void GetExtWindowConfig(ExtWindowConfig& cfg) {
        Lock();
        cfg.enabled = m_extWindowEnabled;
        cfg.x = m_extWindowX;
        cfg.y = m_extWindowY;
        cfg.width = m_extWindowWidth;
        cfg.height = m_extWindowHeight;
        cfg.algo = m_extWindowAlgo;
        cfg.keepAspect = m_extWindowKeepAspect;
        cfg.passthrough = m_extWindowPassthrough;
        cfg.topmost = m_extWindowTopmost;
        cfg.brightness = m_brightnessExt;
        cfg.autoRemoveLetterbox = m_autoRemoveLetterbox;
        
        cfg.closeOnRightClick = m_closeOnRightClick;
        cfg.closeOnResult = m_closeOnResult;
        cfg.gamepadClose = m_gamepadCloseEnabled;
        cfg.gamepadID = m_gamepadID;
        cfg.gamepadBtn = m_gamepadButtonID;
        cfg.keyboardClose = m_keyboardCloseEnabled;
        cfg.keyboardKey = m_keyboardKeyCode;
        Unlock();
    }
    
    // 黒帯自動除去設定 (Auto Remove Letterbox)
    bool m_autoRemoveLetterbox; // 機能有効化
    int m_lbThreshold;          // 黒色判定の輝度閾値 (0-255)
    int m_lbStability;          // 判定安定化に必要なフレーム数

    // 明るさ制御 (Brightness Control, 0-100)
    int m_brightnessLR2;    // LR2出力の明るさ
    int m_brightnessExt;    // 外部ウィンドウの明るさ

    // その他設定 (Misc)
    bool m_autoOpenSettings; // フィルタロード時に設定画面を自動で開く

    // 手動クローズトリガー設定 (Manual Close Trigger Settings)
    bool m_closeOnRightClick;       // 右クリックで閉じる
    bool m_closeOnResult;           // 追加: リザルト画面で閉じる
    bool m_gamepadCloseEnabled;     // ゲームパッドボタンで閉じる
    int m_gamepadID;                // 監視するジョイスティックID
    int m_gamepadButtonID;          // ボタンID
    bool m_keyboardCloseEnabled;    // キーボードキーで閉じる
    int m_keyboardKeyCode;          // 仮想キーコード

    // 接続制限設定 (Connection Restrictions)
    bool m_onlyOutputToLR2;         // プロセス名に body を含む場合のみ接続許可 (デフォルトON)
    bool m_onlyOutputToRenderer;    // レンダラー（出力ピンなし）への接続のみ許可 (デフォルトON)

    void SetCloseOnResult(bool b) { Lock(); m_closeOnResult = b; Unlock(); }
    void GetCloseOnResult(bool* b) { Lock(); if(b) *b = m_closeOnResult; Unlock(); }

    // Lock
    void Lock() { m_mtx.lock(); }
    void Unlock() { m_mtx.unlock(); }

private:
    std::recursive_mutex m_mtx;
    static const wchar_t* REGISTRY_KEY;
};


