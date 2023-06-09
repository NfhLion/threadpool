#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <vector>
#include <queue>
#include <memory>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <thread>
#include <unordered_map>

#include "util.h"

// 实现接收提交到线程池的task任务执行完成后的返回值类型Result
class Task;
class Result {
public:
    Result(std::shared_ptr<Task> task, bool isValid = true);
    ~Result() = default;

    void setVal(Any any);

    Any get();
    
private:
    Any any_;                       // 存储任务的返回值
    Semaphore sem_;           // 
    std::shared_ptr<Task> task_;
    std::atomic_bool isValid_;
};

// 任务抽象基类
class Task {
public:
    Task() 
        : result_(nullptr)
    {}
    ~Task() = default;
    void exec() {
        if (result_ != nullptr) {
            result_->setVal(run());
        }
    }

    void setResult(Result* res) {
        result_ = res;
    }

    virtual Any run() = 0;
private:
    Result* result_;
};

// 线程池支持的模式
enum class PoolMode
{
    MODE_FIXED,     // 固定数量模式
    MODE_CACHED     // 动态增长模式
};

class Thread {
public:
    // 线程函数对象类型
    using ThreadFunc = std::function<void(int)>;
    
    Thread(ThreadFunc func);
    ~Thread();
    
    // 启动线程
    void start();

    int getId() const;
private:
    ThreadFunc func_;
    int threadId_;      // 保存线程id

    static int generateId_;
};

class ThreadPool {
public:
    ThreadPool();
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    // 设置线程池的工作模式
    void setMode(PoolMode mode);

    // 设置cached模式下线程数量的上限阈值
    void setThreadSizeThreshHold(int threshHold);

    // 设置task队列的上限阈值
    void setTaskQueMaxThreshHold(int threshHold);

    // 给线程池提交任务
    Result subMitTask(std::shared_ptr<Task> sp);

    // 开启线程池
    void start(unsigned int initThreadSize = std::thread::hardware_concurrency());

    bool checkRunningState() const;

private:
    // 线程执行函数
    void threadHandler(int threadId);

private:
    // std::vector<std::unique_ptr<Thread>> threads_;                  // 线程队列
    std::unordered_map<int, std::unique_ptr<Thread>> threads_;      // 线程队列
    size_t initThreadSize_;                         // 初始线程数量
    int threadSizeThreshHold_;                      // 线程上限数量阈值
    std::atomic_int curThreadSize_;                // 当前线程池中线程的数量
    std::atomic_int idleThreadSize_;               // 空闲线程的大小数量
    

    std::queue<std::shared_ptr<Task>> taskQue_;     // 任务队列 
    std::atomic_int taskSize_;                     // 任务数量
    int taskQueMaxThreshHold_;                      // 任务队列上限的阈值

    std::mutex taskQueMtx_;                         // 保证任务队列的线程安全
    std::condition_variable notFull_;               // 表示任务队列不满
    std::condition_variable notEmpty_;              // 表示任务队列不空
    std::condition_variable exitCond_;              // 线程池销毁

    PoolMode poolMode_;                             // 当前线程池的工作模式

    std::atomic_bool isPoolRunning_;                // 线程池的启动状态
};

#endif