add_library(wb_device_simulator SHARED device_simulator.cpp)
target_compile_definitions(wb_device_simulator PRIVATE WB_SIMULATOR_BUILD)
target_include_directories(wb_device_simulator PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(wb_device_simulator PRIVATE wb_contracts wb_wire_protocol Qt6::Core Qt6::Network)

