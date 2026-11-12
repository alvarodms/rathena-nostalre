# nostalRE: unit tests for rules with no map-server state in them. ctest runs them:
#   cmake --build <build> --target population-tests && ctest --test-dir <build>
add_executable(population-tests EXCLUDE_FROM_ALL population_manners_test.cpp)
target_include_directories(population-tests PRIVATE ${RA_INCLUDE_DIRS})
add_test(NAME population-manners COMMAND population-tests)
set_target_properties(population-tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
