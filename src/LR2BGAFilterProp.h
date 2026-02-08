//------------------------------------------------------------------------------
// LR2BGAFilterProp.h
// LR2BGAフィルタの設定プロパティページ定義
//------------------------------------------------------------------------------
#pragma once

#define NO_DSHOW_STRSAFE

#include "LR2BGAFilter.h"
#include "LR2BGATypes.h"
#include "resource.h"
#include <streams.h>
#include <vector>
#include <functional>


//------------------------------------------------------------------------------
// CLR2BGAFilterPropertyPage
// フィルタ設定ダイヤログのロジックを管理するクラス
//------------------------------------------------------------------------------
class CLR2BGAFilterPropertyPage : public CBasePropertyPage {
public:
  // ファクトリメソッド (COMインスタンス生成用)
  static CUnknown *WINAPI CreateInstance(LPUNKNOWN pUnk, HRESULT *phr);

  // コンストラクタ
  CLR2BGAFilterPropertyPage(LPUNKNOWN pUnk);

  //--------------------------------------------------------------------------
  // CBasePropertyPage オーバーライドメソッド
  //--------------------------------------------------------------------------
  HRESULT OnConnect(IUnknown *pUnk) override;
  HRESULT OnDisconnect() override;
  HRESULT OnActivate() override;
  HRESULT OnApplyChanges() override;
  INT_PTR OnReceiveMessage(HWND hwnd, UINT uMsg, WPARAM wParam,
                           LPARAM lParam) override;

private:
  // 現在の設定値に基づいてダイアログのコントロールを更新
  void UpdateControls();

  // ダイアログのコントロールから設定値を読み取る
  void ReadControls();

  // フィルタ設定インターフェースへのポインタ
  ILR2BGAFilterSettings *m_pSettings;

  //--------------------------------------------------------------------------
  // Property Binder Architecture
  //--------------------------------------------------------------------------
  enum class BindType { Bool, Int, Combo, Slider };

  struct Binding {
      UINT id;            // Control ID
      BindType type;      // Data type
      void* pTarget;      // Pointer to member variable
      int minVal = 0;
      int maxVal = 0;
      bool immediateApply = false; // Apply to m_pSettings immediately on change
      
      // Callback for immediate application
      std::function<void(ILR2BGAFilterSettings*, const Binding&)> applyCallback;
      
      // Callback for UI dependencies (enable/disable other controls)
      std::function<void()> onUpdateUI;
  };

  std::vector<Binding> m_bindings;

  void InitBindings();
  void ApplyToUI();   // m_bindings -> UI
  void LoadFromUI();  // UI -> m_bindings
  bool HandleAutoBinding(UINT uMsg, WPARAM wParam, LPARAM lParam);

  //--------------------------------------------------------------------------
  // Member Variables (Local Cache)
  //--------------------------------------------------------------------------
  int m_width;
  int m_height;
  ResizeAlgorithm m_algo;
  BOOL m_keepAspect;
  BOOL m_debugMode;
  
  BOOL m_limitFPS; // Added for UI state
  int m_maxFPS;
  
  BOOL m_dummyMode;
  BOOL m_passthroughMode;

  // External window settings
  BOOL m_extEnabled;
  int m_extX;
  int m_extY;
  int m_extWidth;
  int m_extHeight;
  ResizeAlgorithm m_extAlgo;
  BOOL m_extKeepAspect;
  BOOL m_extPassthrough;
  BOOL m_extTopmost;
  
  // Brightness (Added)
  int m_brightnessLR2;
  int m_brightnessExt;

  // Auto Open (Added)
  BOOL m_autoOpen;

  // Letterbox
  BOOL m_autoRemoveLB;
  int m_lbThreshold;
  int m_lbStability;

  // Manual Close
  BOOL m_closeOnRightClick;
  BOOL m_closeOnResult; // New
  BOOL m_gamepadCloseEnabled;
  int m_gamepadID;
  int m_gamepadButtonID;
  BOOL m_keyboardCloseEnabled;
  int m_keyboardKeyCode;
};

