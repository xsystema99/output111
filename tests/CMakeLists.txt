include(FetchContent)

# Prefer an installed Catch2 v3; otherwise fetch it. Override with -DHC_CATCH2_TAG=... if needed.
set(HC_CATCH2_TAG "v3.9.1" CACHE STRING "Catch2 git tag fetched when no installed Catch2 is found")
find_package(Catch2 3 QUIET)
if(NOT Catch2_FOUND)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG ${HC_CATCH2_TAG}
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(Catch2)
    list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
endif()

add_executable(chordcore_tests
    support/SmfParser.cpp
    test_theory.cpp
    test_roman.cpp
    test_progression.cpp
    test_noteengine.cpp
    test_scheduler.cpp
    test_serialization.cpp
    test_document.cpp
    test_progressiontext.cpp
    test_timeline.cpp
    test_chordedit.cpp
    test_preview.cpp
    test_octave_length.cpp
    test_grid.cpp
    test_midifile.cpp)
target_include_directories(chordcore_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(chordcore_tests PRIVATE chordcore Catch2::Catch2WithMain)

include(Catch)
catch_discover_tests(chordcore_tests)

# ---- end-to-end: the CLI writes demo.mid, then an independent Python parser checks the file ------
if(TARGET export_demo)
    add_test(NAME export_demo_writes_mid
             COMMAND export_demo ${CMAKE_CURRENT_BINARY_DIR}/demo.mid)
    set_tests_properties(export_demo_writes_mid PROPERTIES FIXTURES_SETUP demo_mid)

    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_FOUND)
        add_test(NAME python_verifies_demo_mid
                 COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/python/verify_midi.py
                         --demo ${CMAKE_CURRENT_BINARY_DIR}/demo.mid)
        set_tests_properties(python_verifies_demo_mid PROPERTIES FIXTURES_REQUIRED demo_mid)
    else()
        message(STATUS "Python3 not found: python_verifies_demo_mid test skipped")
    endif()
endif()
