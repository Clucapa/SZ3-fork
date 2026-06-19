add_executable(e2e e2e_main.cpp)
target_link_libraries(e2e PRIVATE SZ3)
target_include_directories(e2e PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# CI gate: fast end-to-end test
add_test(NAME e2e_fast COMMAND e2e --fast)
add_test(NAME e2e_compose COMMAND e2e --compose)
