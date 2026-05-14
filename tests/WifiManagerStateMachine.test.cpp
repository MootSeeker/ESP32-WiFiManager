#include <cstdlib>
#include <iostream>

#include "esp32_wifi_manager/WifiManagerEventQueue.hpp"
#include "esp32_wifi_manager/WifiRetryScheduler.hpp"
#include "esp32_wifi_manager/WifiManagerStateMachine.hpp"

using esp32_wifi_manager::WifiManagerEvent;
using esp32_wifi_manager::WifiManagerEventQueue;
using esp32_wifi_manager::WifiManagerEventType;
using esp32_wifi_manager::WifiRetryScheduler;
using esp32_wifi_manager::WifiRuntimeStatus;
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

    if (!ExpectEqual(stateMachine.OnConnectionFailed(3, 1000, 8000), WifiState::kWaitingToRetry,
                     "expected first connection failure to enter waiting-to-retry state")) {
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

    if (!ExpectEqual(stateMachine.OnRetryTimerElapsed(), WifiState::kConnecting,
                     "expected retry timer to return the state machine to connecting")) {
        return false;
    }

    if (!ExpectEqual(stateMachine.OnConnectionFailed(3, 1000, 8000), WifiState::kWaitingToRetry,
                     "expected second connection failure to still wait before retrying")) {
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

    if (!ExpectEqual(stateMachine.OnConnectionFailed(5, 1000, 2500), WifiState::kWaitingToRetry,
                     "expected first failure to retry before cap test")) {
        return false;
    }

    if (!ExpectEqual(stateMachine.OnConnectionFailed(5, 1000, 2500), WifiState::kWaitingToRetry,
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

    if (!ExpectEqual(stateMachine.OnConnectionFailed(5, 1000, 2500), WifiState::kWaitingToRetry,
                     "expected third failure to continue retrying before portal fallback")) {
        return false;
    }

    return ExpectEqual(stateMachine.GetReconnectDelayMs(), 2500,
                       "expected reconnect delay to saturate at configured maximum");
}

bool ShouldExpireRetrySchedulerWhenElapsedTimeReachesDelay()
{
    WifiRetryScheduler scheduler;
    scheduler.Arm(1500);

    if (scheduler.Advance(500)) {
        std::cerr << "expected scheduler to remain armed before delay is exhausted" << std::endl;
        return false;
    }

    if (!ExpectEqual(scheduler.RemainingDelayMs(), 1000,
                     "expected scheduler to reduce remaining delay after partial advance")) {
        return false;
    }

    if (!scheduler.Advance(1000)) {
        std::cerr << "expected scheduler to expire when accumulated time reaches delay" << std::endl;
        return false;
    }

    return ExpectEqual(static_cast<uint8_t>(scheduler.IsArmed()), 0,
                       "expected scheduler to disarm after expiry");
}

bool ShouldCancelRetryScheduler()
{
    WifiRetryScheduler scheduler;
    scheduler.Arm(2000);
    scheduler.Cancel();

    if (scheduler.Advance(2000)) {
        std::cerr << "expected cancelled scheduler to ignore elapsed time" << std::endl;
        return false;
    }

    if (!ExpectEqual(static_cast<uint8_t>(scheduler.IsArmed()), 0,
                     "expected cancelled scheduler to stay disarmed")) {
        return false;
    }

    return ExpectEqual(scheduler.RemainingDelayMs(), 0,
                       "expected cancelled scheduler to clear remaining delay");
}

bool ShouldPreserveEventOrderInQueue()
{
    WifiManagerEventQueue queue;

    WifiManagerEvent firstEvent;
    firstEvent.type = WifiManagerEventType::kProvisioningRequested;
    firstEvent.runtimeStatus.disconnectReason = 7;

    WifiManagerEvent secondEvent;
    secondEvent.type = WifiManagerEventType::kConnectionFailed;
    secondEvent.runtimeStatus.disconnectReason = 42;

    if (!queue.Push(firstEvent) || !queue.Push(secondEvent)) {
        std::cerr << "expected queue push operations to succeed" << std::endl;
        return false;
    }

    WifiManagerEvent actualEvent;
    if (!queue.Pop(actualEvent) ||
        actualEvent.type != WifiManagerEventType::kProvisioningRequested ||
        actualEvent.runtimeStatus.disconnectReason != 7) {
        std::cerr << "expected queue to return first event first" << std::endl;
        return false;
    }

    if (!queue.Pop(actualEvent) ||
        actualEvent.type != WifiManagerEventType::kConnectionFailed ||
        actualEvent.runtimeStatus.disconnectReason != 42) {
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

bool ShouldStoreRuntimeStatusPayload()
{
    WifiRuntimeStatus status;
    status.ipAddress = 0x01020304;
    status.netmask = 0xffffff00;
    status.gateway = 0x01020301;

    WifiManagerEvent event;
    event.type = WifiManagerEventType::kConnectionSucceeded;
    event.runtimeStatus = status;

    WifiManagerEventQueue queue;
    if (!queue.Push(event)) {
        std::cerr << "expected runtime status event to be enqueued" << std::endl;
        return false;
    }

    WifiManagerEvent actualEvent;
    if (!queue.Pop(actualEvent)) {
        std::cerr << "expected runtime status event to be dequeued" << std::endl;
        return false;
    }

    if (!ExpectEqual(actualEvent.runtimeStatus.ipAddress, 0x01020304,
                     "expected IP address to survive queue round-trip")) {
        return false;
    }

    if (!ExpectEqual(actualEvent.runtimeStatus.netmask, 0xffffff00,
                     "expected netmask to survive queue round-trip")) {
        return false;
    }

    return ExpectEqual(actualEvent.runtimeStatus.gateway, 0x01020301,
                       "expected gateway to survive queue round-trip");
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

    if (!ShouldExpireRetrySchedulerWhenElapsedTimeReachesDelay()) {
        return EXIT_FAILURE;
    }

    if (!ShouldCancelRetryScheduler()) {
        return EXIT_FAILURE;
    }

    if (!ShouldStoreRuntimeStatusPayload()) {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}