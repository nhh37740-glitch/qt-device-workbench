add_library(wb_wire_protocol SHARED wire_protocol.cpp)
target_compile_definitions(wb_wire_protocol PRIVATE WB_WIRE_BUILD)
target_include_directories(wb_wire_protocol PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(wb_wire_protocol PRIVATE wb_contracts Qt6::Core)

