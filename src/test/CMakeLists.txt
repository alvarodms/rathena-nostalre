# nostalRE: unit tests for rules with no map-server state in them. ctest runs them:
#   cmake --build <build> --target population-tests && ctest --test-dir <build>
add_custom_target(population-tests)
foreach(name manners levels)
	add_executable(population-${name}-tests EXCLUDE_FROM_ALL population_${name}_test.cpp)
	target_include_directories(population-${name}-tests PRIVATE ${RA_INCLUDE_DIRS})
	set_target_properties(population-${name}-tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
	add_test(NAME population-${name} COMMAND population-${name}-tests)
	add_dependencies(population-tests population-${name}-tests)
endforeach()
