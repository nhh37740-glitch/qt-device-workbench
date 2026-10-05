add_library(wb_record_store SHARED record_store.cpp)
target_compile_definitions(wb_record_store PRIVATE WB_STORE_BUILD)
target_include_directories(wb_record_store PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(wb_record_store PUBLIC wb_contracts PRIVATE Qt6::Core)
