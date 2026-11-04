add_library(chordcore STATIC
    src/Types.cpp
    src/Theory.cpp
    src/RomanNumeral.cpp
    src/Progression.cpp
    src/NoteEngine.cpp
    src/MidiFile.cpp)

target_include_directories(chordcore PUBLIC include)
target_compile_features(chordcore PUBLIC cxx_std_17)
set_target_properties(chordcore PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(MSVC)
    target_compile_options(chordcore PRIVATE /W4 /permissive- /utf-8)
    if(HC_WARNINGS_AS_ERRORS)
        target_compile_options(chordcore PRIVATE /WX)
    endif()
else()
    target_compile_options(chordcore PRIVATE -Wall -Wextra -Wpedantic -Wshadow)
    if(HC_WARNINGS_AS_ERRORS)
        target_compile_options(chordcore PRIVATE -Werror)
    endif()
endif()
