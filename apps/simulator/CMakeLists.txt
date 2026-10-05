add_executable(device-simulator main.cpp)
target_link_libraries(device-simulator PRIVATE wb_contracts wb_device_simulator Qt6::Core)

