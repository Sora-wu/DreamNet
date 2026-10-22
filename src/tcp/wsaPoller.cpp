//
// Author: sora
// Mail: sora-wu@foxmail.com
//

#include <tcp/wsaPoller.h>
#include <tcp/channel.h>
#include <common/socket.h>
#include <common/log.hpp>

#include <thread>

using namespace Dream::detail;

namespace {
    short convertCommonEvents(uint32_t events) {
        short result = 0;
        if (events & Channel::EVENT_READ) result |= POLLRDNORM;
        if (events & Channel::EVENT_PRI) result |= POLLRDBAND;
        if (events & Channel::EVENT_WRITE) result |= POLLWRNORM;
        // WSAPoll 使用电平触发；上层仍然读写到 would-block，兼容 EVENT_ET。
        return result;
    }

    uint32_t convertPollEvents(short events) {
        uint32_t result = Channel::EVENT_NONE;
        if (events & POLLRDNORM) result |= Channel::EVENT_READ;
        if (events & POLLRDBAND) result |= Channel::EVENT_PRI;
        if (events & POLLWRNORM) result |= Channel::EVENT_WRITE;
        if (events & (POLLERR | POLLNVAL)) result |= Channel::EVENT_ERROR;
        if (events & POLLHUP) result |= Channel::EVENT_HUP;
        return result;
    }
}

void WsaPoller::poll(std::vector<Channel*>& activeChannels, int timeout) {
    if (events_.empty()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(timeout));
        return;
    }

    const int count = WSAPoll(events_.data(), static_cast<ULONG>(events_.size()), timeout);
    if (count < 0) {
        const int error = Socket::getLastError();
        if (!Socket::isInterrupted(error)) {
            LOG_ERROR("WSAPoll error: {}", Socket::errorString(error));
        }
        return;
    }
    if (count == 0) {
        return;
    }

    activeChannels.reserve(count);
    for (size_t i = 0; i < events_.size(); ++i) {
        if (events_[i].revents != 0) {
            channels_[i]->setActualEvent(convertPollEvents(events_[i].revents));
            activeChannels.push_back(channels_[i]);
        }
    }
}

void WsaPoller::updateChannel(Channel* channel) {
    if (channel->isNoneEvent()) {
        removeChannel(channel);
        return;
    }

    const auto it = indices_.find(channel->getFd());
    const short events = convertCommonEvents(channel->getListenEvent());
    if (it == indices_.end()) {
        indices_.emplace(channel->getFd(), events_.size());
        events_.push_back({channel->getFd(), events, 0});
        channels_.push_back(channel);
        ++load_;
    }
    else {
        events_[it->second].events = events;
    }
    channel->setStatus(ChannelStatus::IN_LOOP);
}

void WsaPoller::removeChannel(Channel* channel) {
    const auto it = indices_.find(channel->getFd());
    if (it == indices_.end()) {
        return;
    }

    const size_t index = it->second;
    const size_t last = events_.size() - 1;
    if (index != last) {
        events_[index] = events_[last];
        channels_[index] = channels_[last];
        indices_[events_[index].fd] = index;
    }
    events_.pop_back();
    channels_.pop_back();
    indices_.erase(it);
    --load_;
    channel->setStatus(ChannelStatus::OUT_LOOP);
}

uint32_t WsaPoller::getLoad() {
    return load_.load();
}
