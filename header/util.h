#ifndef UTIL_H
#define UTIL_H

#include <memory>
#include <mutex>
#include <condition_variable>

// Any类型，可以接受任意数据类型
class Any {
public:
    Any() = default;
    ~Any() = default;

    Any(const Any&) = delete;
    Any& operator=(const Any&) = delete;

    Any(Any&&) = default;
    Any& operator=(Any&&) = default;

    template<typename T>
    Any(T data) : base_(std::make_unique<Derive<T>>(data))
    {}

    template<typename T>
    T cast_() {
        Derive<T>* pd = dynamic_cast<Derive<T>*>(base_.get());
        if (pd == nullptr) {
            throw "type is unmatch!";
        }
        return pd->data_;
    }

private:
    // 基类
    class Base {
    public:
        virtual ~Base() = default;
    };

    // 派生类
    template<typename T>
    class Derive : public Base {
    public:
        Derive(T data) : data_(data)
        {}
         
        T data_;    // 保存了任意的其他类型
    };

private:
    std::unique_ptr<Base> base_;
};

// 信号量
class Semaphore {
public:
    Semaphore(int resLimit = 0)
        : resLimit_(resLimit),
          isExit_(false)
    {}
    ~Semaphore() {
        isExit_ = true;
    }

    // 获取一个信号量资源
    void wait() {
        if (isExit_) {
            return;
        }
        std::unique_lock<std::mutex> lock(mtx_);
        cond_.wait(lock, [&]()->bool{
            return resLimit_ > 0;
        });
        resLimit_--;
    }

    // 增加一个信号量资源
    void post() {
        if (isExit_) {
            return;
        }
        std::unique_lock<std::mutex> lock(mtx_);
        resLimit_++;
        cond_.notify_all();
    }
private:
    int resLimit_;                      // 资源计数
    std::mutex mtx_;
    std::condition_variable cond_;
    std::atomic_bool isExit_;
};

#endif // UTIL_H