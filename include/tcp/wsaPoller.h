//
// Author: sora
// Mail: sora-wu@foxmail.com
//

#pragma once

#include <tcp/poller.h>
#include <DreamNet/socketTypes.h>

#include <atomic>
#include <unordered_map>

namespace Dream::detail {
    class WsaPoller final : public Poller {
    public:
        void poll(std::vector<Channel*>& activeChannels, int timeout) override;
        void updateChannel(Channel* channel) override;
        void removeChannel(Channel* channel) override;
        [[nodiscard]] uint32_t getLoad() override;

    private:
        std::vector<WSAPOLLFD> events_;
        std::vector<Channel*> channels_;
        std::unordered_map<SocketFd, size_t> indices_;
        std::atomic<uint32_t> load_ = 0;
    };
}
