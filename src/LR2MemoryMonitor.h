#pragma once
#include <windows.h>
#include <thread>
#include <atomic>
#include <functional>
#include <memory>

// LR2のメモリを監視し、シーン遷移（特にリザルト画面への移行）を検知するクラス
class LR2MemoryMonitor {
public:
    LR2MemoryMonitor();
    ~LR2MemoryMonitor();

    // 監視スレッドを開始する
    void Start();

    // 監視スレッドを停止する
    void Stop();

    // リザルト画面への遷移を検知した際のコールバックを設定する
    void SetResultCallback(std::function<void()> callback);

private:
    // 監視ループを実行するメソッド
    void MonitorThread();

    // 現在のプロセスがターゲット（LR2body）かどうかを確認する
    bool IsTargetProcess();

private:
    std::thread m_thread;
    std::atomic<bool> m_bStop;
    std::function<void()> m_callback;

    // シーンIDが格納されているオフセット (Base + 0x24 もしくは Base + 0x23DB4)
    // 実際には動的に取得したベースアドレスに対して加算する
    static const int kSceneIdOffset = 0x23DB4; // LR2Helperの解析に基づく

    // リザルト画面のシーンID
    static const int kResultSceneId = 5;
};
