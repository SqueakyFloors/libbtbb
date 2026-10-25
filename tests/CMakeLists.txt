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
    # VERSION and RELEASE are macros lib/src/CMakeLists.txt defines for that
    # source; what they say does not matter here.
    target_compile_definitions(test_internal PRIVATE
                               VERSION="test" RELEASE="test")
    add_test(NAME internal COMMAND test_internal)
    set_tests_properties(internal PROPERTIES TIMEOUT 30)

    add_test(NAME release_version COMMAND ${CMAKE_COMMAND}
             -DSET_RELEASE_CMAKE=${PROJECT_SOURCE_DIR}/cmake/set_release.cmake
             -P ${CMAKE_CURRENT_SOURCE_DIR}/test_release_version.cmake)
    set_tests_properties(release_version PROPERTIES TIMEOUT 30)
endif()
