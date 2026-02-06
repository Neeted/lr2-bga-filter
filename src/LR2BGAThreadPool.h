//------------------------------------------------------------------------------
// LR2BGAThreadPool.h
// 汎用スレッドプール実装
//
// 概要:
//   シングルトンパターンで実装されたスレッドプールクラスです。
//   主に画像処理の並列化（ParallelFor）に使用します。
//
// 使用法:
//   LR2BGAThreadPool::Instance().ParallelFor(0, height, [&](int startY, int endY) {
//       // 行 [startY, endY) を処理
//   });
//
// 注意:
//   - C++17 では std::result_of は非推奨です (std::invoke_result 推奨)
//   - 本実装では互換性のため std::result_of を使用していますが、
//     将来的に更新を検討してください。
//------------------------------------------------------------------------------
#pragma once

#include <vector>
#include <thread>
#include <queue>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <future>

class LR2BGAThreadPool {
public:
    static LR2BGAThreadPool& Instance() {
        static LR2BGAThreadPool instance;
        return instance;
    }

    // 並列Forループ
    // 範囲 [start, end) をチャンクに分割し、各チャンクを並列実行します
    template<typename Function>
    void ParallelFor(int start, int end, Function func) {
        int range = end - start;
        if (range <= 0) return;

        int threadCount = (int)workers.size();
        if (threadCount == 0 || range < threadCount) {
             func(start, end);
             return;
        }

        // 作業を分割
        int chunk = range / threadCount;
        int remainder = range % threadCount;

        std::vector<std::future<void>> futures;
        int currentStart = start;

        for (int i = 0; i < threadCount; ++i) {
            int currentChunk = chunk + (i < remainder ? 1 : 0);
            int currentEnd = currentStart + currentChunk;
            
            // タスクをスレッドプールにエンキューし、futureで完了を待機
            // カスタムスレッドプールにより、スレッド生成コストを抑制
            if (currentChunk > 0) {
                futures.emplace_back(Enqueue([=] {
                    func(currentStart, currentEnd);
                }));
            }
            currentStart = currentEnd;
        }

        // すべてのチャンクの完了を待機
        for (auto& f : futures) {
            f.wait();
        }
    }

    // 汎用タスクのエンキュー
    template<class F, class... Args>
    auto Enqueue(F&& f, Args&&... args) 
        -> std::future<typename std::result_of<F(Args...)>::type>
    {
        using return_type = typename std::result_of<F(Args...)>::type;

        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );
            
        std::future<return_type> res = task->get_future();
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            if(stop)
                throw std::runtime_error("enqueue on stopped ThreadPool");

            tasks.emplace([task](){ (*task)(); });
        }
        condition.notify_one();
        return res;
    }

    ~LR2BGAThreadPool() {
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            stop = true;
        }
        condition.notify_all();
        for(std::thread &worker: workers)
            worker.join();
    }

private:
    LR2BGAThreadPool() : stop(false) {
        // ハードウェアスレッド数を取得 (0の場合はフォールバック)
        unsigned int threads = std::thread::hardware_concurrency();
        if (threads == 0) threads = 4;
        
        for(size_t i = 0; i<threads; ++i)
            workers.emplace_back(
                [this]
                {
                    for(;;)
                    {
                        std::function<void()> task;

                        {
                            std::unique_lock<std::mutex> lock(this->queue_mutex);
                            this->condition.wait(lock,
                                [this]{ return this->stop || !this->tasks.empty(); });
                            if(this->stop && this->tasks.empty())
                                return;
                            task = std::move(this->tasks.front());
                            this->tasks.pop();
                        }

                        task();
                    }
                }
            );
    }

    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;
    std::mutex queue_mutex;
    std::condition_variable condition;
    bool stop;
    
    // コピー禁止
    LR2BGAThreadPool(const LR2BGAThreadPool&) = delete;
    LR2BGAThreadPool& operator=(const LR2BGAThreadPool&) = delete;
};

