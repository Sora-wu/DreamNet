//
// Author: sora
// Mail: sora-wu@foxmail.com
//

#include <tcp/timerQueue.h>
#include <tcp/eventLoopImpl.h>

#include <algorithm>

using namespace Dream::detail;

namespace {
    template <typename Period>
    Timer createTimer(Dream::Functor callback, Period delay, Period interval, uint64_t id) {
        TimePoint now = std::chrono::steady_clock::now();
        Timer timer{};
        timer.id = id;
        timer.expire = now + delay;
        timer.interval = interval;
        timer.delay = delay;
        timer.callback = std::move(callback);

        return timer;
    }
}

TimerQueue::TimerQueue(EventLoopImpl* loop) : loop_(loop) {}

uint64_t TimerQueue::addTimer(Functor callback, std::chrono::milliseconds delay, std::chrono::milliseconds interval) {
    return addTimerPri(std::move(callback), delay, interval);
}

uint64_t TimerQueue::addTimer(Functor callback, std::chrono::seconds delay, std::chrono::seconds interval) {
    return addTimerPri(std::move(callback), delay, interval);
}

void TimerQueue::removeTimer(uint64_t id) {
    loop_->runInLoop([this, id] {
        for (auto it = timers_.begin(); it != timers_.end(); /**/) {
            if (it->id == id) {
                it = timers_.erase(it);
            } else {
                ++it;
            }
        }
    });
}

int TimerQueue::getTimeout(int maximum) const {
    if (!timers_.empty()) {
        const auto remaining = timers_.begin()->expire - std::chrono::steady_clock::now();
        const auto timeout = std::chrono::ceil<std::chrono::milliseconds>(remaining).count();
        return static_cast<int>(std::clamp<int64_t>(timeout, 0, maximum));
    }
    return maximum;
}

void TimerQueue::processTimers() {
    TimePoint now = std::chrono::steady_clock::now();
    while (!timers_.empty() && timers_.begin()->expire <= now) {
        Timer t = std::move(timers_.extract(timers_.begin()).value());
        t.callback();

        if (t.interval.count() > 0) {
            t.expire += t.interval;
            timers_.insert(std::move(t));
        }
    }
}

uint64_t TimerQueue::addTimerPri(Functor callback, std::chrono::milliseconds delay, std::chrono::milliseconds interval) {
    Timer timer = std::move(createTimer(std::move(callback), delay, interval, currentID_++));
    loop_->runInLoop([this, timer = std::move(timer)]() mutable {
        timers_.insert(timer);
    });

    return timer.id;
}

uint64_t TimerQueue::addTimerPri(Functor callback, std::chrono::seconds delay, std::chrono::seconds interval) {
    Timer timer = std::move(createTimer(std::move(callback), delay, interval, currentID_++));
    loop_->runInLoop([this, timer = std::move(timer)]() mutable {
        timers_.insert(timer);
    });

    return timer.id;
}
