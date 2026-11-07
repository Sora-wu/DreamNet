//
// Author: sora
// Mail: sora-wu@foxmail.com
//

#include <tcp/eventLoopImpl.h>
#include <tcp/channel.h>
#include <tcp/poller.h>
#include <common/define.h>

#include <deque>

namespace {
    constexpr int TIMEOUT = 1e4;
}

using namespace Dream::detail;

EventLoopImpl::EventLoopImpl() :
    threadId_(std::this_thread::get_id()) {
    poller_.reset(Poller::getDefaultPoller());
    wakeupChannel_ = std::make_unique<Channel>(this, wakeup_.getFd());
    if (!wakeupChannel_) {
        LOG_FATAL("wakeup channel is null");
    }
    wakeupChannel_->enableReading(); // 注册唤醒事件
    wakeupChannel_->setOnReadEvent([this] { onWakeUp(); });
    wakeupChannel_->setOnErrorEvent([] { LOG_FATAL("wakeup channel error"); });
    wakeupChannel_->setOnCloseEvent([] { LOG_FATAL("wakeup channel closed"); });

    timerQueue_ = std::make_unique<TimerQueue>(this);
}

EventLoopImpl::~EventLoopImpl() {
    timerQueue_.reset();
    wakeupChannel_->disableAll();
}

void EventLoopImpl::loop() {
    processPendingFunctors();
    while (!isQuit_) {
        activeChannels_.clear();
        // 定时回调也可能投递任务，有待执行任务时不能进入阻塞轮询。
        const int timeout = pendingFunctors_.empty() ? timerQueue_->getTimeout(TIMEOUT) : 0;
        poller_->poll(activeChannels_, timeout);
        for (Channel* channel : activeChannels_) {
            channel->handleEvent();
        }
        processPendingFunctors();
        timerQueue_->processTimers();
    }

    LOG_INFO("Event Loop exit");
}

void EventLoopImpl::quit() {
    isQuit_ = true;
    wakeup(); // 唤醒 poll，让loop()立即退出，而不是等poll超时
}

void EventLoopImpl::wakeup() const {
    if (!wakeupPending_.exchange(true)) {
        wakeup_.notify();
    }
}

void EventLoopImpl::runInLoop(Functor func) {
    if (isInLoopThread()) {
        func();
        return;
    }

    queueInLoop(func);
}

uint64_t EventLoopImpl::runAfter(Functor func, std::chrono::milliseconds delay) const {
    return timerQueue_->addTimer(std::move(func), delay);
}

uint64_t EventLoopImpl::runAfter(Functor func, std::chrono::seconds delay) const {
    return timerQueue_->addTimer(std::move(func), delay);
}

void EventLoopImpl::cancelAfter(uint64_t tid) const {
    timerQueue_->removeTimer(tid);
}

void EventLoopImpl::queueInLoop(Functor func) {
    pendingFunctors_.push(std::move(func));

    if (!isInLoopThread() || callingPendingFunctors_) {
        // 如果非当前线程，或者正在执行pendingFunctors_
        wakeup();
    }
}

std::thread::id EventLoopImpl::getThreadId() const {
    return threadId_;
}

bool EventLoopImpl::isInLoopThread() const {
    return threadId_ == std::this_thread::get_id();
}

void EventLoopImpl::updateChannel(Channel* channel) const {
    poller_->updateChannel(channel);
}

void EventLoopImpl::removeChannel(Channel* channel) const {
    poller_->removeChannel(channel);
}

uint32_t EventLoopImpl::getLoad() const {
    return poller_->getLoad();
}

void EventLoopImpl::processPendingFunctors() {
    std::deque<Functor> tmp;

    callingPendingFunctors_ = true;
    // 先重新允许通知，再取任务快照；快照之后入队的任务会留下新的唤醒。
    wakeupPending_ = false;
    pendingFunctors_.pop_all(tmp);
    for (auto& func : tmp) {
        func();
    }
    callingPendingFunctors_ = false;
}

void EventLoopImpl::onWakeUp() const {
    wakeup_.consume();
}
