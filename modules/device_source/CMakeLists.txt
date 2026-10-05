add_library(wb_device_source SHARED device_source.cpp)
target_compile_definitions(wb_device_source PRIVATE WB_SOURCE_BUILD)
target_include_directories(wb_device_source PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(wb_device_source PRIVATE wb_contracts wb_wire_protocol Qt6::Core Qt6::Network)
