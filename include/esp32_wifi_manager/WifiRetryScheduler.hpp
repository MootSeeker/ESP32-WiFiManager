#pragma once

#include <cstdint>

namespace esp32_wifi_manager {

class WifiRetryScheduler {
public:
    void Arm(uint32_t delayMs)
    {
        remainingDelayMs_ = delayMs;
        armed_ = delayMs > 0;
    }

    void Cancel()
    {
        remainingDelayMs_ = 0;
        armed_ = false;
    }

    bool Advance(uint32_t elapsedMs)
    {
        if (!armed_) {
            return false;
        }

        if (elapsedMs >= remainingDelayMs_) {
            Cancel();
            return true;
        }

        remainingDelayMs_ -= elapsedMs;
        return false;
    }

    bool IsArmed() const
    {
        return armed_;
    }

    uint32_t RemainingDelayMs() const
    {
        return remainingDelayMs_;
    }

private:
    uint32_t remainingDelayMs_ = 0;
    bool armed_ = false;
};

}  // namespace esp32_wifi_manager