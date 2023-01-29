#ifndef COROUTINE_COROUTINE2_H
#define COROUTINE_COROUTINE2_H
#include <exception>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <queue>
#include <vector>
#include <coroutine>

template<typename T>
struct Task;

template<typename T>
struct TaskAwait;

class SchedulerTask {
public:
    SchedulerTask(std::function<void()> &&f, long long t): func(f) {
        auto now = std::chrono::system_clock::now();
        auto cur = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
        time  = cur + t;
    }

    auto TimeToWait() const {
        auto now = std::chrono::system_clock::now();
        auto cur = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
        return time - cur;
    }

    auto ScheduleTime() {
        return time;
    }

    void run() {
        func();
    }
private:
    long long time;
    std::function<void()> func;
};

struct Scompare {
    bool operator()(SchedulerTask &a, SchedulerTask &b) {
        return a.ScheduleTime() > b.ScheduleTime();
    }
};

class Scheduler {
public:
    Scheduler() {
        running = true;
        work = std::thread(&Scheduler::run,this);
    }

    ~Scheduler() {
        shutdown();
        join();
    }

    void execute(std::function<void()> &&func, long long time) {
        std::unique_lock l(lock);
        if(running) {
            bool notify = work_queue.empty() || (work_queue.top().TimeToWait() > time);
            work_queue.push(SchedulerTask(std::move(func),time));
            lock.unlock();
            if(notify) {
                cond.notify_one();
            }
        }
    }

    void shutdown() {
        running = false;
        cond.notify_all();
    }

    void join() {
        if(work.joinable()) {
            work.join();
        }
    }

private:
    std::condition_variable cond;
    std::mutex lock;
    bool running;
    std::thread work;
    std::priority_queue<SchedulerTask,std::vector<SchedulerTask>,Scompare> work_queue;

    void run() {
        while(running || !work_queue.empty()) {
            std::unique_lock l(lock);
            if (work_queue.empty()) {
                cond.wait(l);
                if(work_queue.empty()) {
                    continue;
                }
            }
            auto work_task = work_queue.top();
            long long DelayTime = work_task.TimeToWait();
            if(DelayTime > 0) {
                auto status = cond.wait_for(l,std::chrono::milliseconds(DelayTime));
                if(status != std::cv_status::timeout) {
                    continue;
                }
            }
            work_queue.pop();
            l.unlock();
            work_task.run();
        }
    }
};

struct SleepAwait {
    explicit SleepAwait(long long duration): _duration(duration) {}

    bool await_ready() { return false;}
    void await_resume() {}
    void await_suspend(std::coroutine_handle<> handle) const {
        static Scheduler scheduler;
        scheduler.execute([handle](){
            handle.resume();
            },_duration);
    }

private:
    long long _duration;
};

template<typename T>
struct TaskResult {
    explicit TaskResult() = default;
    explicit TaskResult(T &&value) : _value(value) {}
    explicit TaskResult(std::exception_ptr &&exception_ptr) : exp(exception_ptr) {}

    T get_value() {
        if(exp) {}
        else {
            //std::cout << _value;
            return _value;
        }
    }
private:
    T _value{};
    std::exception_ptr exp;
};

template<typename T>
struct TaskPromise {
    Task<T> get_return_object() {
        return Task{ std::coroutine_handle<TaskPromise>::from_promise(*this) };
    }
    std::suspend_never initial_suspend() { return std::suspend_never{}; }
    std::suspend_always final_suspend() noexcept { return std::suspend_always{}; }
    void unhandled_exception() {
        std::lock_guard l(lock);
        result = TaskResult<T>(std::current_exception());
        cond.notify_all();
        to_callback();
    }
    void return_value(T value) {
        std::lock_guard l(lock);
        result = TaskResult<T>(std::move(value));
        cond.notify_all();
        to_callback();
    }

    template<typename type>
    auto await_transform(Task<type> &&value) {
        return TaskAwait<type>(std::move(value));
    }

    template<typename Rep, typename Period>
    auto await_transform(std::chrono::duration<Rep, Period> &&duration) {
        return SleepAwait(std::chrono::duration_cast<std::chrono::milliseconds>(duration).count());
    }

    T get_value() {
        std::unique_lock l(lock);
        if(!result.has_value()) {
            cond.wait(l);
        }
        return result->get_value();
    }

    void is_completed(std::function<void()> &&func) {
        std::unique_lock l(lock);
        if(result.has_value()) {
            lock.unlock();
            func();
        } else {
            callbacks.push_back(func);
        }
    }


private:
    std::vector<std::function<void()>> callbacks;

    std::optional<TaskResult<T>> result;
    std::condition_variable cond;
    std::mutex lock;

    void to_callback() {
        for(auto &func : callbacks) {
            func();
        }
        callbacks.clear();
    }
};

template<typename T>
struct Task {
    using promise_type = TaskPromise<T>;

    explicit Task(std::coroutine_handle<promise_type> handle) noexcept: handle(handle) {}

    Task(Task &&task) noexcept: handle(std::exchange(task.handle, {})) {}

    Task(Task &) = delete;

    Task &operator=(Task &) = delete;

    ~Task() {
        if (handle) handle.destroy();
    }

    T get_result() {
        return handle.promise().get_value();
    }

    Task &finally(std::function<void()> &&func) {
        handle.promise().is_completed(std::move(func));
        return *this;
    }
private:
    std::coroutine_handle<promise_type> handle;
};

template<typename T>
struct TaskAwait {
    explicit TaskAwait(Task<T> &&task) noexcept : t(std::move(task)) {}

    TaskAwait(TaskAwait &) = delete;

    TaskAwait &operator= (TaskAwait &) = delete;

    bool await_ready() { return false;}

    T await_resume() {
        return t.get_result();
    }

    void await_suspend(std::coroutine_handle<> handle) noexcept {
        t.finally([handle](){
            std::thread([handle](){ handle.resume(); }).detach();  //如果不用新线程恢复协程 会造成死锁
        });
    }

private:
    Task<T> t;
};





#endif //COROUTINE_COROUTINE2_H
