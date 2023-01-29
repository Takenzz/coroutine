#include <iostream>
#include <chrono>
#include "coroutine2.h"

Task<int> func() {
    using namespace std::chrono_literals;
    co_await 1s;
    co_return 1;
}
Task<int> func2() {
    using namespace std::chrono_literals;
    co_await 2s;
    co_return 2;
}

Task<int> func3() {
    using namespace std::chrono_literals;
    auto f = co_await func();
    auto f2 = co_await func2();
    co_return 3 + f + f2;
}


int main() {
    auto f = func3();
    std::cout << "nonblocking" << std::endl;
    std::cout << "nonblocking" << std::endl;
    std::cout << f.get_result() << std::endl;
}
