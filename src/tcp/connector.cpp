//
// Author: sora
// Mail: sora-wu@foxmail.com
//

#include <tcp/connector.h>
#include <tcp/channel.h>
#include <common/log.hpp>

#include <cstring>

#include "tcp/eventLoopImpl.h"

using namespace Dream;

namespace {
    constexpr uint64_t MAX_RETRY_INTERVAL = 30000;
}

detail::Connector::Connector(EventLoopImpl* loop, const Address& addr) :
    loop_(loop),
    addr_(addr) {}

void detail::Connector::start() {
    // 避免重复调用
    if (state_ == State::CONNECTING) {
        return;
    }

    reset();

    int res = socket_.connect(addr_);
    if (res == 0) {
        if (newConnectionCallback_) {
            state_ = State::CONNECTED;
            attemptToConnect_ = 0;
            const SocketFd fd = socket_.getFd();
            socket_.setFd(INVALID_SOCKET_FD); // 移交fd的管理权，避免一个fd调用两次close
            channel_.reset();
            newConnectionCallback_(fd, addr_);
            return;
        }

        LOG_WARN("the connect callback is not set yet, will close new client by default");
        return;
    }
    if (res != 0 && Socket::isConnectInProgress(Socket::getLastError())) {
        state_ = State::CONNECTING;
        channel_->enableWriting();
        return;
    }

    // 其他错误
    retry();
}

void detail::Connector::stop() {
    if (!channel_) {
        return;
    }

    state_ = State::DISCONNECTED;
    channel_->disableAll();
    socket_.close();
    loop_->cancelAfter(tid_);
}

void detail::Connector::setRetryInterval(uint32_t retryIntervalMillis) {
    std::weak_ptr weakSelf = shared_from_this();
    loop_->runInLoop([weakSelf, retryIntervalMillis] {
        if (auto self = weakSelf.lock()) {
            self->retryIntervalMillis_ = retryIntervalMillis;
        }
    });
}

void detail::Connector::setNewConnectionCallback(NewConnectionCallback newConnectionCallback) {
    newConnectionCallback_ = std::move(newConnectionCallback);
}

void detail::Connector::initSocket() const {
    socket_.setTcpNoDelay(true); // TODO: 测试有没有这个选项，延迟多大
    socket_.setKeepAlive(true);
}

void detail::Connector::handleWrite() {
    if (state_ != State::CONNECTING) {
        return;
    }

    const SocketFd sockfd = socket_.getFd();
    const int error = Socket::getError(sockfd);

    if (error) {
        LOG_ERROR("connect error: {}", Socket::errorString(error));
        retry();
        return;
    }

    channel_->disableAll();
    if (newConnectionCallback_) {
        state_ = State::CONNECTED;
        attemptToConnect_ = 0;
        socket_.setFd(INVALID_SOCKET_FD); // 移交fd的管理权，避免一个fd调用两次close
        // 当前仍在 Channel 的回调中，等本轮事件处理结束后再析构。
        loop_->queueInLoop([channel = std::shared_ptr<Channel>(std::move(channel_))] {});
        newConnectionCallback_(sockfd, addr_);
        return;
    }

    LOG_WARN("the connect callback is not set yet, will close new client by default");
}

void detail::Connector::handleClose() {
    LOG_ERROR("connect failed! will try again");
    retry();
}

void detail::Connector::handleError() {
    LOG_ERROR("connect failed! will try again");
    retry();
}

void detail::Connector::retry() {
    channel_->disableAll();
    socket_.close();
    state_ = State::DISCONNECTED;

    LOG_ERROR("attempt to connect failed, try #{}", attemptToConnect_);
    std::weak_ptr weakSelf = shared_from_this();
    tid_ = loop_->runAfter([weakSelf] {
        if (auto self = weakSelf.lock()) {
            self->start();
        }
    }, std::chrono::milliseconds(std::min(retryIntervalMillis_ * ++attemptToConnect_, MAX_RETRY_INTERVAL)));
}

void detail::Connector::reset() {
    socket_ = Socket(SOCK_STREAM);
    const SocketFd sockfd = socket_.getFd();
    channel_ = std::make_unique<Channel>(loop_, sockfd);

    std::weak_ptr weakSelf = shared_from_this();
    channel_->setOnWriteEvent([weakSelf] {
        if (auto self = weakSelf.lock()) {
            self->handleWrite();
        }
    });

    channel_->setOnCloseEvent([weakSelf] {
        if (auto self = weakSelf.lock()) {
            self->handleClose();
        }
    });

    channel_->setOnErrorEvent([weakSelf] {
        if (auto self = weakSelf.lock()) {
            self->handleError();
        }
    });

    initSocket();
}
