cmake_minimum_required(VERSION 3.21)
project(VoidPit VERSION 1.0.0 LANGUAGES CXX)

add_executable(void_pit
    main.cpp
    battle.cpp
    data.cpp
    effects.cpp
    effects_extra.cpp
    session.cpp
    ui.cpp
    tests.cpp
)
target_compile_features(void_pit PRIVATE cxx_std_17)
set_target_properties(void_pit PROPERTIES
    CXX_EXTENSIONS OFF
    INTERPROCEDURAL_OPTIMIZATION FALSE
    OUTPUT_NAME void-pit-windows-x64
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

if(MSVC)
    target_compile_options(void_pit PRIVATE
            /W4
            /utf-8
            /EHsc
            $<$<CONFIG:Release>:/Zi>
            $<$<CONFIG:Release>:/O1>
            $<$<CONFIG:Release>:/Os>
    )

    set_property(TARGET void_pit PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
    )

    target_link_options(void_pit PRIVATE
            /INCREMENTAL:NO
            $<$<CONFIG:Release>:/DEBUG:FULL>
            $<$<CONFIG:Release>:/OPT:REF>
            $<$<CONFIG:Release>:/OPT:ICF>
    )

    if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        target_compile_options(void_pit PRIVATE /clang:-fno-lto)
    endif()
else()
    target_compile_options(void_pit PRIVATE -Wall -Wextra -Wpedantic -fno-lto)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(void_pit PRIVATE -finput-charset=UTF-8 -fexec-charset=UTF-8 -fno-ipa-icf)
    endif()
    target_compile_options(void_pit PRIVATE $<$<CONFIG:Release>:-O2>)
    target_link_options(void_pit PRIVATE -fno-lto)
    if(MINGW)
        target_link_options(void_pit PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
endif()

include(CTest)
if(BUILD_TESTING)
    add_test(NAME self-test COMMAND void_pit --self-test --plain)
    add_test(NAME plain-clear-rejected COMMAND void_pit --plain --redraw=clear)
    set_tests_properties(plain-clear-rejected PROPERTIES WILL_FAIL TRUE)

    add_executable(arena_route_check route_check.cpp battle.cpp data.cpp effects.cpp effects_extra.cpp)
    target_compile_features(arena_route_check PRIVATE cxx_std_17)
    if(MSVC)
        target_compile_options(arena_route_check PRIVATE /utf-8 /EHsc)
    else()
        target_compile_options(arena_route_check PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    if(MINGW)
        target_link_options(arena_route_check PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
    add_test(NAME ordinary-routes COMMAND arena_route_check)
    set_tests_properties(ordinary-routes PROPERTIES TIMEOUT 600)
endif()
