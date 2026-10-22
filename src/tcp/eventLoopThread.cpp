//
// Author: sora
// Mail: sora-wu@foxmail.com
//

#include <tcp/eventLoopThread.h>
#include <DreamNet/eventLoop.h>
#include <tcp/eventLoopImpl.h>
#include <tcp/channel.h>
#include <common/log.hpp>

#include <future>

using namespace Dream;

detail::EventLoopThread::EventLoopThread(const std::string& threadName) : threadName_(threadName) {}

detail::EventLoopThread::~EventLoopThread() {
    if (loop_) {
        loop_->quit();
        if (thread_.joinable()) {
            thread_.join();
        }
        delete loop_;
        loop_ = nullptr;
    }
}

void detail::EventLoopThread::start() {
    std::promise<void> ready;
    auto future = ready.get_future();
    thread_ = std::jthread([this, ready = std::move(ready)]() mutable {
        loop_ = new EventLoop;
        ready.set_value();
        loop_->loop();
        if (!threadName_.empty()) {
            LOG_INFO("thread {} exit", threadName_);
        }
    });
    future.get();
}

EventLoop* detail::EventLoopThread::getEventLoop() const {
    return loop_;
}
