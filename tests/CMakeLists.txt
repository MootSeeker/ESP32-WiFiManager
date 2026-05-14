cmake_minimum_required(VERSION 3.16)

project(esp32_wifi_manager_tests LANGUAGES CXX)

add_executable(
    wifi_manager_state_machine_tests
    WifiManagerStateMachine.test.cpp
    ../src/WifiManagerStateMachine.cpp
)

target_compile_features(wifi_manager_state_machine_tests PRIVATE cxx_std_17)

target_include_directories(
    wifi_manager_state_machine_tests
    PRIVATE
        ../include
)