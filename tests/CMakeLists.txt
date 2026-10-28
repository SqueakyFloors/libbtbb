if(BUILD_TESTING)
    add_executable(test_smoke test_smoke.c)
    target_include_directories(test_smoke PRIVATE ${PROJECT_SOURCE_DIR}/lib/src)
    if(BUILD_SHARED_LIB)
        target_link_libraries(test_smoke btbb)
    else()
        target_link_libraries(test_smoke btbb-static)
    endif()
    add_test(NAME smoke COMMAND test_smoke)
    set_tests_properties(smoke PROPERTIES TIMEOUT 10)

    add_executable(test_le_print test_le_print.c)
    target_include_directories(test_le_print PRIVATE ${PROJECT_SOURCE_DIR}/lib/src)
    if(BUILD_SHARED_LIB)
        target_link_libraries(test_le_print btbb)
    else()
        target_link_libraries(test_le_print btbb-static)
    endif()
    add_test(NAME le_print COMMAND test_le_print)
    set_tests_properties(le_print PROPERTIES TIMEOUT 30)

    # The functions this one covers are internal, so it compiles the library
    # source itself rather than linking the library.
    add_executable(test_internal test_internal.c)
    target_include_directories(test_internal PRIVATE ${PROJECT_SOURCE_DIR}/lib/src)
    # the library source is built the way the library builds it
    target_compile_options(test_internal PRIVATE -std=gnu90)
    add_test(NAME internal COMMAND test_internal)
    set_tests_properties(internal PROPERTIES TIMEOUT 30)

    # The Bluetooth Core specification's own sample data. These also reach
    # library internals, so they compile the translation unit under test.
    add_executable(test_spec_bredr test_spec_bredr.c)
    target_include_directories(test_spec_bredr PRIVATE ${PROJECT_SOURCE_DIR}/lib/src)
    target_compile_options(test_spec_bredr PRIVATE -std=gnu90)
    add_test(NAME spec_bredr COMMAND test_spec_bredr)
    set_tests_properties(spec_bredr PROPERTIES TIMEOUT 30)

    add_executable(test_spec_hop test_spec_hop.c)
    target_include_directories(test_spec_hop PRIVATE ${PROJECT_SOURCE_DIR}/lib/src)
    if(BUILD_SHARED_LIB)
        target_link_libraries(test_spec_hop btbb)
    else()
        target_link_libraries(test_spec_hop btbb-static)
    endif()
    add_test(NAME spec_hop COMMAND test_spec_hop)
    set_tests_properties(spec_hop PROPERTIES TIMEOUT 60)

    add_executable(test_spec_le test_spec_le.c)
    target_include_directories(test_spec_le PRIVATE ${PROJECT_SOURCE_DIR}/lib/src)
    if(BUILD_SHARED_LIB)
        target_link_libraries(test_spec_le btbb)
    else()
        target_link_libraries(test_spec_le btbb-static)
    endif()
    add_test(NAME spec_le COMMAND test_spec_le)
    set_tests_properties(spec_le PROPERTIES TIMEOUT 30)

    add_executable(test_hop test_hop.c)
    target_include_directories(test_hop PRIVATE ${PROJECT_SOURCE_DIR}/lib/src)
    if(BUILD_SHARED_LIB)
        target_link_libraries(test_hop btbb)
    else()
        target_link_libraries(test_hop btbb-static)
    endif()
    add_test(NAME hop COMMAND test_hop)
    # Generating a whole hopping sequence is 2^27 hops, twice over.
    set_tests_properties(hop PROPERTIES TIMEOUT 300)

    add_test(NAME release_version COMMAND ${CMAKE_COMMAND}
             -DSET_RELEASE_CMAKE=${PROJECT_SOURCE_DIR}/cmake/set_release.cmake
             -P ${CMAKE_CURRENT_SOURCE_DIR}/test_release_version.cmake)
    set_tests_properties(release_version PROPERTIES TIMEOUT 30)

    add_executable(test_le_long test_le_long.c)
    target_include_directories(test_le_long PRIVATE ${PROJECT_SOURCE_DIR}/lib/src)
    if(BUILD_SHARED_LIB)
        target_link_libraries(test_le_long btbb)
    else()
        target_link_libraries(test_le_long btbb-static)
    endif()
    add_test(NAME le_long_packets COMMAND test_le_long)
    set_tests_properties(le_long_packets PROPERTIES TIMEOUT 30)
endif()
