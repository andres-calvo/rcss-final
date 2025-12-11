add_executable(test_game_logic test_game_logic.cpp)
target_link_libraries(test_game_logic 
    PRIVATE 
    robocup::common
    GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(test_game_logic)
