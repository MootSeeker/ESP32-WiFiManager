# ESP32-WiFiManager

Reusable ESP-IDF WiFi manager component for ESP32-class devices.

The goal of this repository is to provide a reusable module for:

- loading stored WiFi credentials
- trying station mode on boot
- falling back to provisioning when connection fails
- exposing a clean state-machine-oriented API for application integration

This repository is intentionally focused on a standalone WiFi manager module. It does not embed application-specific MQTT, LED, or product logic.

## Current Scope

The current implementation stage establishes the reusable component boundary and public API:

- standalone ESP-IDF component layout
- public C++ API for configuration and state handling
- callback-based integration points instead of product-specific dependencies
- clear separation between module code and consuming application code

## Planned Behaviour

- Load stored credentials from NVS
- Connect as station on boot when credentials exist
- Open provisioning mode when no credentials exist or connection repeatedly fails
- Expose state transitions to the application
- Keep provisioning, connection, and credential storage logic reusable across projects

## Non-Goals for v1

- product-specific MQTT integration
- LED/status-indicator integration
- board-specific application logic
- non-ESP-IDF framework support

## Repository Layout

```text
include/esp32_wifi_manager/
	WifiManager.hpp
	WifiManagerTypes.hpp
src/
	WifiManager.cpp
examples/basic/
	main/main.cpp
CMakeLists.txt
idf_component.yml
```

## Integration Direction

The module is intended to be consumable as an ESP-IDF component from GitHub in future projects.

## Status

This repository is currently in active extraction/setup. The first milestone is establishing a stable reusable API and component structure before porting the full provisioning and connection logic from the source project.