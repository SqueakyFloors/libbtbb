if(BUILD_TESTING)
    # Whichever library this configuration built. lib/src refuses a
    # configuration that builds neither, so one of the two targets is there;
    # where both are, the shared one is what an install links against.
    if(TARGET btbb)
        set(btbb_library btbb)
    else()
        set(btbb_library btbb-static)
    endif()

    add_executable(test_smoke test_smoke.c)
    target_include_directories(test_smoke PRIVATE ${PROJECT_SOURCE_DIR}/lib/src)
    target_link_libraries(test_smoke ${btbb_library})
    add_test(NAME smoke COMMAND test_smoke)
    set_tests_properties(smoke PROPERTIES TIMEOUT 10)

    add_executable(test_le_print test_le_print.c)
    target_include_directories(test_le_print PRIVATE ${PROJECT_SOURCE_DIR}/lib/src)
    target_link_libraries(test_le_print ${btbb_library})
    add_test(NAME le_print COMMAND test_le_print)
    set_tests_properties(le_print PROPERTIES TIMEOUT 30)

    # The same checks with the printer compiled into the test and plain char
    # unsigned, which is what it is on every ARM host. A signed field read
    # through a plain char reads differently there than on the x86 and Apple
    # hosts, and CI runs on the latter, so this is how the ARM reading is
    # checked at all.
    # The company identifier table comes with it: the printer calls into it,
    # and the library does not export that function.
    add_executable(test_le_print_unsigned_char test_le_print.c
                   ${PROJECT_SOURCE_DIR}/lib/src/companies.c)
    target_include_directories(test_le_print_unsigned_char
                               PRIVATE ${PROJECT_SOURCE_DIR}/lib/src)
    target_compile_options(test_le_print_unsigned_char
                           PRIVATE -funsigned-char)
    target_compile_definitions(test_le_print_unsigned_char
                               PRIVATE LE_PRINT_COMPILE_PRINTER)
    target_link_libraries(test_le_print_unsigned_char ${btbb_library})
    add_test(NAME le_print_unsigned_char COMMAND test_le_print_unsigned_char)
    set_tests_properties(le_print_unsigned_char PROPERTIES TIMEOUT 30)

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

    # The eSCO length decision, also internal: it compiles the translation
    # unit the payload decoders live in.
    add_executable(test_esco test_esco.c)
    target_include_directories(test_esco PRIVATE ${PROJECT_SOURCE_DIR}/lib/src)
    target_compile_options(test_esco PRIVATE -std=gnu90)
    add_test(NAME esco COMMAND test_esco)
    # A few seconds, most of it the sweep over 8,000 payloads, which a
    # sanitizer build takes twice as long over.
    set_tests_properties(esco PROPERTIES TIMEOUT 60)

    # The company identifier table. Its one function is internal, so this
    # compiles the translation unit it lives in.
    add_executable(test_companies test_companies.c)
    target_include_directories(test_companies PRIVATE ${PROJECT_SOURCE_DIR}/lib/src)
    target_compile_options(test_companies PRIVATE -std=gnu90)
    add_test(NAME companies COMMAND test_companies)
    # One walk over all 65,536 identifiers.
    set_tests_properties(companies PROPERTIES TIMEOUT 30)

    # The hop kernel is internal, so this one compiles the two translation
    # units it reaches into.
    add_executable(test_spec_hop test_spec_hop.c)
    target_include_directories(test_spec_hop PRIVATE ${PROJECT_SOURCE_DIR}/lib/src)
    target_compile_options(test_spec_hop PRIVATE -std=gnu90)
    add_test(NAME spec_hop COMMAND test_spec_hop)
    set_tests_properties(spec_hop PROPERTIES TIMEOUT 60)

    add_executable(test_spec_le test_spec_le.c)
    target_include_directories(test_spec_le PRIVATE ${PROJECT_SOURCE_DIR}/lib/src)
    target_link_libraries(test_spec_le ${btbb_library})
    add_test(NAME spec_le COMMAND test_spec_le)
    set_tests_properties(spec_le PROPERTIES TIMEOUT 30)

    # Clock recovery, also internal: it compiles the two translation units it
    # reaches into.
    add_executable(test_piconet test_piconet.c)
    target_include_directories(test_piconet PRIVATE ${PROJECT_SOURCE_DIR}/lib/src)
    target_compile_options(test_piconet PRIVATE -std=gnu90)
    add_test(NAME piconet COMMAND test_piconet)
    # Under a second, but each candidate list is a walk over 2^21 clock
    # values, which a sanitizer build takes a few seconds over.
    set_tests_properties(piconet PROPERTIES TIMEOUT 60)

    # The two clock-recovery measurements the issues quote. Internal, so it
    # compiles the translation units it reaches into.
    add_executable(test_clock_sweep test_clock_sweep.c)
    target_include_directories(test_clock_sweep PRIVATE ${PROJECT_SOURCE_DIR}/lib/src)
    target_compile_options(test_clock_sweep PRIVATE -std=gnu90)
    add_test(NAME clock_sweep COMMAND test_clock_sweep)
    # Each trial walks 2^21 clock values, and the default is two dozen of
    # them twice over; a sanitizer build takes several times as long.
    set_tests_properties(clock_sweep PROPERTIES TIMEOUT 300)

    # Likewise internal: it compiles the two translation units it reaches
    # into.
    add_executable(test_hop test_hop.c)
    target_include_directories(test_hop PRIVATE ${PROJECT_SOURCE_DIR}/lib/src)
    target_compile_options(test_hop PRIVATE -std=gnu90)
    add_test(NAME hop COMMAND test_hop)
    # A million hops, for the one case that walks a range of clock values.
    set_tests_properties(hop PROPERTIES TIMEOUT 60)

    # What the library's hash tables do when they cannot be allocated.
    # Internal again, so it compiles the two translation units it reaches
    # into, and it replaces the allocator uthash makes its own allocations
    # with.
    add_executable(test_uthash_oom test_uthash_oom.c)
    target_include_directories(test_uthash_oom PRIVATE ${PROJECT_SOURCE_DIR}/lib/src)
    target_compile_options(test_uthash_oom PRIVATE -std=gnu90)
    add_test(NAME uthash_oom COMMAND test_uthash_oom)
    # Four child processes and a few hundred survey entries: under a tenth of
    # a second. The limit is for a case where the library does not exit.
    set_tests_properties(uthash_oom PROPERTIES TIMEOUT 30)

    add_test(NAME release_version COMMAND ${CMAKE_COMMAND}
             -DSET_RELEASE_CMAKE=${PROJECT_SOURCE_DIR}/cmake/set_release.cmake
             -P ${CMAKE_CURRENT_SOURCE_DIR}/test_release_version.cmake)
    set_tests_properties(release_version PROPERTIES TIMEOUT 30)

    # Capture file writers, checked by reading the bytes back out of a file.
    # Two of its cases call the generic PCAPNG writer, which is internal, so
    # it compiles that translation unit as well as linking the library.
    add_executable(test_capture test_capture.c)
    target_include_directories(test_capture PRIVATE ${PROJECT_SOURCE_DIR}/lib/src)
    target_link_libraries(test_capture ${btbb_library})
    add_test(NAME capture COMMAND test_capture)
    set_tests_properties(capture PROPERTIES TIMEOUT 30)

    # The record assemblers are internal, so this one compiles the two
    # translation units they live in and links the library for the rest.
    add_executable(test_capture_internal test_capture_internal.c)
    target_include_directories(test_capture_internal PRIVATE ${PROJECT_SOURCE_DIR}/lib/src)
    target_link_libraries(test_capture_internal ${btbb_library})
    add_test(NAME capture_internal COMMAND test_capture_internal)
    set_tests_properties(capture_internal PROPERTIES TIMEOUT 30)

    add_executable(test_le_long test_le_long.c)
    target_include_directories(test_le_long PRIVATE ${PROJECT_SOURCE_DIR}/lib/src)
    target_link_libraries(test_le_long ${btbb_library})
    add_test(NAME le_long_packets COMMAND test_le_long)
    set_tests_properties(le_long_packets PROPERTIES TIMEOUT 30)

    # Fuzz targets for the decoders and the capture writers, where the
    # compiler builds them.
    add_subdirectory(fuzz)

    # This one configures and builds the tree itself, in library
    # configurations the build running it is not in.
    # The generator, compiler and toolchain file are handed on, so that the
    # trees it configures are built the way this one was.
    add_test(NAME library_config COMMAND ${CMAKE_COMMAND}
             -DSOURCE_DIR=${PROJECT_SOURCE_DIR}
             -DBINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}/library-config-test
             -DGENERATOR=${CMAKE_GENERATOR}
             -DC_COMPILER=${CMAKE_C_COMPILER}
             -DTOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}
             -DSHARED_SUFFIX=${CMAKE_SHARED_LIBRARY_SUFFIX}
             -P ${CMAKE_CURRENT_SOURCE_DIR}/test_library_config.cmake)
    # Twelve configures, nine compiles and ten installs: 35 to 80 s on an
    # arm64 Mac, depending on what else is building at the time. The limit is
    # for a case that hangs rather than for a slow machine.
    set_tests_properties(library_config PROPERTIES TIMEOUT 300)

    # The exported names against the header the install offers. It reads the
    # built library with nm, so it needs the shared one: a static archive's
    # symbol table is not what a program links against.
    if(TARGET btbb)
        find_program(ABI_NM NAMES ${CMAKE_NM} nm)
        if(ABI_NM)
            # A Mach-O symbol name carries a leading underscore that an ELF
            # one does not.
            set(abi_strip_underscore OFF)
            # Mach-O has one symbol table and its nm has no -D; elsewhere the
            # dynamic table is the one a program resolves against.
            set(abi_dynamic_symbols ON)
            if(APPLE)
                set(abi_strip_underscore ON)
                set(abi_dynamic_symbols OFF)
            endif()
            add_test(NAME abi_surface COMMAND ${CMAKE_COMMAND}
                     -DLIBRARY=$<TARGET_FILE:btbb>
                     -DHEADER=${PROJECT_SOURCE_DIR}/lib/src/btbb.h
                     -DNM=${ABI_NM}
                     -DSTRIP_UNDERSCORE=${abi_strip_underscore}
                     -DDYNAMIC_SYMBOLS=${abi_dynamic_symbols}
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/test_abi_surface.cmake)
            # One nm run: 0.1 s on Linux, 0.5 s on a macOS CI runner.
            set_tests_properties(abi_surface PROPERTIES TIMEOUT 15)
        else()
            message(STATUS "nm was not found: leaving out the abi_surface test")
        endif()
    endif()
endif()
