add_executable(ra_tests
	core/containers_test.cpp
	core/coord_test.cpp
	core/dir_test.cpp
	core/fastmath_test.cpp
	core/fpu_test.cpp
	core/rng_test.cpp
	formats/compression_test.cpp
	formats/formats_test.cpp
	rules/rules_test.cpp
	game/install_test.cpp
	vfs/mix_test.cpp
	voxel/voxel_test.cpp
	world/world_test.cpp
	test_data.h
)
target_include_directories(ra_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(ra_tests PRIVATE
	RA_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
	RA_REPO_ROOT="${PROJECT_SOURCE_DIR}")
target_link_libraries(ra_tests PRIVATE ra_core ra_vfs ra_ini ra_formats ra_rules ra_world ra_voxel GTest::gtest_main)
ra_set_target_options(ra_tests)

include(GoogleTest)
gtest_discover_tests(ra_tests DISCOVERY_MODE PRE_TEST)
