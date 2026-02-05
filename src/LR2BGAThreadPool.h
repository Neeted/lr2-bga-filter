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

    // Parallel For Loop
    // Splits range [start, end) into chunks and executes func in parallel
    template<typename Function>
    void ParallelFor(int start, int end, Function func) {
        int range = end - start;
        if (range <= 0) return;

        int threadCount = (int)workers.size();
        if (threadCount == 0 || range < threadCount) {
             func(start, end);
             return;
        }

        // Divide work
        int chunk = range / threadCount;
        int remainder = range % threadCount;

        std::vector<std::future<void>> futures;
        int currentStart = start;

        for (int i = 0; i < threadCount; ++i) {
            int currentChunk = chunk + (i < remainder ? 1 : 0);
            int currentEnd = currentStart + currentChunk;
            
            // Should properly implement a task queue, but for simplicity/RAII with futures:
            // Since we want this to block until done, we can use std::async if we didn't have a custom pool.
            // But custom pool is better for persistent threads.
            
            // Simplified "Task" submission not implemented fully here for brevity in this snippet usually,
            // but let's do it properly via Enqueue.
            
            // Wait, standard futures approach with a custom pool:
            // passing a promise.
            
            if (currentChunk > 0) {
                futures.emplace_back(Enqueue([=] {
                    func(currentStart, currentEnd);
                }));
            }
            currentStart = currentEnd;
        }

        for (auto& f : futures) {
            f.wait();
        }
    }

    // Enqueue a generic task
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
        // Use hardware concurrency, but limit minimal/maximal reasonable count
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
    
    // Non-copyable
    LR2BGAThreadPool(const LR2BGAThreadPool&) = delete;
    LR2BGAThreadPool& operator=(const LR2BGAThreadPool&) = delete;
};

