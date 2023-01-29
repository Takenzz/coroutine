#ifndef COROUTINE_COROUTINE_H
#define COROUTINE_COROUTINE_H
#include <coroutine>
#include <thread>
#include <chrono>
#include <functional>

void test(std::function<void()> f) {
    std::thread t([f]() {
        std::this_thread::sleep_for(std::chrono::seconds(3));
        f();
    });
    t.detach();
}

struct Awaiter {

    bool await_ready() { return false; }
    void await_resume() { }
    void await_suspend(std::coroutine_handle<> handle) {
        auto f = [handle,this]() {
            handle.resume();
        };
        f();
    }
};

struct result {
    struct promise_type {
        int value;
        auto get_return_object() {
            return result{  std::coroutine_handle<promise_type>::from_promise(*this)  };
        }   //创建协程的返回值
        auto initial_suspend() { return std::suspend_always{}; }    //协程首先调用的
        auto final_suspend() noexcept { return std::suspend_always{}; }   //协程执行完成调用
        void unhandled_exception() { }  //协程抛出异常时调用
        void return_void() { }  //协程返回值
        auto await_transform(int value) {
            this->value = value;
            return std::suspend_always {};
        }
        auto yield_value(int value) {
            this->value = value;
            return std::suspend_always {};
        }
    };
    std::coroutine_handle<promise_type> handle;

    explicit result(std::coroutine_handle<promise_type> handle1) noexcept : handle(handle1) {}

    result(result &) = delete;
    result &operator=(result &) = delete;
    ~result() {
        handle.destroy();
    }

    int next() {
        handle.resume();
        return handle.promise().value;
    }
};




#endif //COROUTINE_COROUTINE_H
