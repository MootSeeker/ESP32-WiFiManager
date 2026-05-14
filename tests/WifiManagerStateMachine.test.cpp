#include <cstdlib>
#include <iostream>

#include "esp32_wifi_manager/WifiManagerEventQueue.hpp"
#include "esp32_wifi_manager/WifiManagerStateMachine.hpp"

using esp32_wifi_manager::WifiManagerEvent;
using esp32_wifi_manager::WifiManagerEventQueue;
using esp32_wifi_manager::WifiManagerEventType;
using esp32_wifi_manager::WifiManagerStateMachine;
using esp32_wifi_manager::WifiState;

namespace {

bool ExpectEqual(WifiState actual, WifiState expected, const char* message)
{
    if (actual == expected) {
        return true;
    }

    std::cerr << message << std::endl;
    return false;
}

bool ExpectEqual(uint8_t actual, uint8_t expected, const char* message)
{
    if (actual == expected) {
        return true;
    }

    std::cerr << message << std::endl;
    return false;
}

bool ExpectEqual(uint32_t actual, uint32_t expected, const char* message)
{
    if (actual == expected) {
        return true;
    }

    std::cerr << message << std::endl;
    return false;
}

bool ShouldRetryBeforePortalFallback()
{
    WifiManagerStateMachine stateMachine;

    if (!ExpectEqual(stateMachine.OnStart(false, true), WifiState::kConnecting,
                     "expected stored credentials to start in connecting state")) {
        return false;
    }

    if (!ExpectEqual(stateMachine.OnConnectionFailed(3, 1000, 8000), WifiState::kConnecting,
                     "expected first connection failure to stay in connecting state")) {
        return false;
    }

    if (!ExpectEqual(stateMachine.GetConnectAttempts(), 1,
                     "expected first connection failure to increment retry count")) {
        return false;
    }

    if (!ExpectEqual(stateMachine.GetReconnectDelayMs(), 1000,
                     "expected first connection failure to set initial reconnect delay")) {
        return false;
    }

    if (!ExpectEqual(stateMachine.OnConnectionFailed(3, 1000, 8000), WifiState::kConnecting,
                     "expected second connection failure to still retry")) {
        return false;
    }

    if (!ExpectEqual(stateMachine.GetReconnectDelayMs(), 2000,
                     "expected second connection failure to double reconnect delay")) {
        return false;
    }

    if (!ExpectEqual(stateMachine.OnConnectionFailed(3, 1000, 8000), WifiState::kPortal,
                     "expected final allowed connection failure to fall back to portal")) {
        return false;
    }

    if (!ExpectEqual(stateMachine.GetConnectAttempts(), 3,
                     "expected retry count to reflect the failure threshold")) {
        return false;
    }

    return ExpectEqual(stateMachine.GetReconnectDelayMs(), 0,
                       "expected portal fallback to clear reconnect delay");
}

bool ShouldResetRetriesAfterSuccessAndProvisioning()
{
    WifiManagerStateMachine stateMachine;

    stateMachine.OnStart(false, true);
    stateMachine.OnConnectionFailed(4, 1000, 8000);
    stateMachine.OnConnectionFailed(4, 1000, 8000);

    if (!ExpectEqual(stateMachine.OnConnectionSucceeded(), WifiState::kConnected,
                     "expected success event to transition to connected")) {
        return false;
    }

    if (!ExpectEqual(stateMachine.GetConnectAttempts(), 0,
                     "expected success to clear retry count")) {
        return false;
    }

    if (!ExpectEqual(stateMachine.GetReconnectDelayMs(), 0,
                     "expected success to clear reconnect delay")) {
        return false;
    }

    if (!ExpectEqual(stateMachine.OnProvisioningRequested(), WifiState::kPortal,
                     "expected provisioning request to enter portal mode")) {
        return false;
    }

    if (!ExpectEqual(stateMachine.GetConnectAttempts(), 0,
                     "expected provisioning request to keep retry count cleared")) {
        return false;
    }

    return ExpectEqual(stateMachine.GetReconnectDelayMs(), 0,
                       "expected provisioning request to keep reconnect delay cleared");
}

bool ShouldCapReconnectDelay()
{
    WifiManagerStateMachine stateMachine;
    stateMachine.OnStart(false, true);

    if (!ExpectEqual(stateMachine.OnConnectionFailed(5, 1000, 2500), WifiState::kConnecting,
                     "expected first failure to retry before cap test")) {
        return false;
    }

    if (!ExpectEqual(stateMachine.OnConnectionFailed(5, 1000, 2500), WifiState::kConnecting,
                     "expected second failure to retry before cap test")) {
        return false;
    }

    return ExpectEqual(stateMachine.GetReconnectDelayMs(), 2000,
                       "expected reconnect delay to remain below the cap before saturation");
}

bool ShouldSaturateReconnectDelayAtConfiguredMaximum()
{
    WifiManagerStateMachine stateMachine;
    stateMachine.OnStart(false, true);
    stateMachine.OnConnectionFailed(5, 1000, 2500);
    stateMachine.OnConnectionFailed(5, 1000, 2500);

    if (!ExpectEqual(stateMachine.OnConnectionFailed(5, 1000, 2500), WifiState::kConnecting,
                     "expected third failure to continue retrying before portal fallback")) {
        return false;
    }

    return ExpectEqual(stateMachine.GetReconnectDelayMs(), 2500,
                       "expected reconnect delay to saturate at configured maximum");
}

bool ShouldPreserveEventOrderInQueue()
{
    WifiManagerEventQueue queue;

    WifiManagerEvent firstEvent;
    firstEvent.type = WifiManagerEventType::kProvisioningRequested;

    WifiManagerEvent secondEvent;
    secondEvent.type = WifiManagerEventType::kConnectionFailed;

    if (!queue.Push(firstEvent) || !queue.Push(secondEvent)) {
        std::cerr << "expected queue push operations to succeed" << std::endl;
        return false;
    }

    WifiManagerEvent actualEvent;
    if (!queue.Pop(actualEvent) || actualEvent.type != WifiManagerEventType::kProvisioningRequested) {
        std::cerr << "expected queue to return first event first" << std::endl;
        return false;
    }

    if (!queue.Pop(actualEvent) || actualEvent.type != WifiManagerEventType::kConnectionFailed) {
        std::cerr << "expected queue to return second event second" << std::endl;
        return false;
    }

    return ExpectEqual(static_cast<uint8_t>(queue.Size()), 0,
                       "expected queue to be empty after popping all events");
}

bool ShouldRejectEventsWhenQueueIsFull()
{
    WifiManagerEventQueue queue;
    WifiManagerEvent event;
    event.type = WifiManagerEventType::kConnectionFailed;

    for (size_t index = 0; index < WifiManagerEventQueue::kCapacity; ++index) {
        if (!queue.Push(event)) {
            std::cerr << "expected push to succeed before queue reaches capacity" << std::endl;
            return false;
        }
    }

    if (queue.Push(event)) {
        std::cerr << "expected push to fail when queue is full" << std::endl;
        return false;
    }

    return ExpectEqual(static_cast<uint8_t>(queue.Size()),
                       static_cast<uint8_t>(WifiManagerEventQueue::kCapacity),
                       "expected queue size to stay at capacity after failed push");
}

}  // namespace

int main()
{
    if (!ShouldRetryBeforePortalFallback()) {
        return EXIT_FAILURE;
    }

    if (!ShouldResetRetriesAfterSuccessAndProvisioning()) {
        return EXIT_FAILURE;
    }

    if (!ShouldPreserveEventOrderInQueue()) {
        return EXIT_FAILURE;
    }

    if (!ShouldRejectEventsWhenQueueIsFull()) {
        return EXIT_FAILURE;
    }

    if (!ShouldCapReconnectDelay()) {
        return EXIT_FAILURE;
    }

    if (!ShouldSaturateReconnectDelayAtConfiguredMaximum()) {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}