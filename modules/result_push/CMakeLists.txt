add_library(wb_result_push SHARED result_push.cpp)
target_compile_definitions(wb_result_push PRIVATE WB_PUSH_BUILD)
target_include_directories(wb_result_push PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(wb_result_push PUBLIC wb_contracts PRIVATE wb_wire_protocol Qt6::Core Qt6::Network)
