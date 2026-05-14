#pragma once

#include <array>
#include <cstddef>

#include "esp32_wifi_manager/WifiManagerTypes.hpp"

namespace esp32_wifi_manager {

class WifiManagerEventQueue {
public:
    static constexpr size_t kCapacity = 8;

    bool Push(const WifiManagerEvent& event)
    {
        if (size_ >= kCapacity) {
            return false;
        }

        events_[tail_] = event;
        tail_ = (tail_ + 1) % kCapacity;
        ++size_;
        return true;
    }

    bool Pop(WifiManagerEvent& outEvent)
    {
        if (size_ == 0) {
            return false;
        }

        outEvent = events_[head_];
        head_ = (head_ + 1) % kCapacity;
        --size_;
        return true;
    }

    void Clear()
    {
        head_ = 0;
        tail_ = 0;
        size_ = 0;
    }

    size_t Size() const
    {
        return size_;
    }

private:
    std::array<WifiManagerEvent, kCapacity> events_{};
    size_t head_ = 0;
    size_t tail_ = 0;
    size_t size_ = 0;
};

}  // namespace esp32_wifi_manager