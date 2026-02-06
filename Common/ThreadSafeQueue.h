#pragma once
#include <mutex>
#include <condition_variable>
#include <queue>

template<class T>
class ThreadSafeQueue {
public:
    void push(T v) {
        {
            std::lock_guard<std::mutex> lk(mu_);
            q_.push(std::move(v));
        }
        cv_.notify_one();
    }

    bool wait_pop(T& out) {
        std::unique_lock<std::mutex> lk(mu_);
        cv_.wait(lk, [&] { return stop_ || !q_.empty(); });
        if (stop_ && q_.empty()) return false;
        out = std::move(q_.front());
        q_.pop();
        return true;
    }

    void stop() {
        {
            std::lock_guard<std::mutex> lk(mu_);
            stop_ = true;
        }
        cv_.notify_all();
    }

private:
    std::mutex mu_;
    std::condition_variable cv_;
    std::queue<T> q_;
    bool stop_ = false;
};
