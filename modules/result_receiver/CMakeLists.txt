add_library(wb_result_receiver SHARED result_receiver.cpp)
target_compile_definitions(wb_result_receiver PRIVATE WB_RECEIVER_BUILD)
target_include_directories(wb_result_receiver PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(wb_result_receiver PUBLIC wb_contracts PRIVATE wb_wire_protocol Qt6::Core Qt6::Network)
