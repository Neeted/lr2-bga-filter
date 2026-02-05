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

  // 設定値のローカルコピー (ダイアログ表示用)
  int m_width;
  int m_height;
  ResizeAlgorithm m_algo;
  BOOL m_keepAspect;
  BOOL m_debugMode;
  int m_maxFPS;
  BOOL m_dummyMode;
  BOOL m_passthroughMode;

  // 外部ウィンドウ設定 (External window settings)
  BOOL m_extEnabled;
  int m_extX;
  int m_extY;
  int m_extWidth;
  int m_extHeight;
  ResizeAlgorithm m_extAlgo;
  BOOL m_extKeepAspect;
  BOOL m_extPassthrough;
  BOOL m_extTopmost;
  BOOL m_autoRemoveLB;

  // 手動クローズ設定 (Manual Close Settings)
  BOOL m_closeOnRightClick;
  BOOL m_gamepadCloseEnabled;
  int m_gamepadID;
  int m_gamepadButtonID;
  BOOL m_keyboardCloseEnabled;
  int m_keyboardKeyCode;

  int m_lbThreshold;
  int m_lbStability;
};

