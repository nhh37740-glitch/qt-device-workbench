add_executable(result-receiver main.cpp)
target_link_libraries(result-receiver PRIVATE wb_contracts wb_result_receiver Qt6::Core)
