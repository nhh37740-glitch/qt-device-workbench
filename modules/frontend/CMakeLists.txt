add_library(wb_frontend SHARED frontend.cpp)
target_compile_definitions(wb_frontend PRIVATE WB_FRONTEND_BUILD)
target_include_directories(wb_frontend PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(wb_frontend PRIVATE wb_contracts Qt6::Widgets)
