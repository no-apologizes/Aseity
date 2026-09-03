cmake_minimum_required(VERSION 4.1)
project(aseity C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)

# find_package(LLVM REQUIRED CONFIG)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_executable(aseity
        main.c
        lexer.c
)

if(CMAKE_BUILD_TYPE STREQUAL "Qodana")
    target_compile_options(aseity PRIVATE
            -std=c17 -O0
            -Wall -Wextra -Wshadow -ggdb3 -Wconversion
    )
else()
    target_compile_options(aseity PRIVATE
            -march=native
            -std=c17
            -flto # Link-time optimization
            -fno-strict-aliasing # Pointers of different types can point to the same memory location
            -Wall -Wextra -Wshadow -ggdb3 -Wconversion # Errors
            $<$<CONFIG:Debug>:-O0>
            $<$<CONFIG:Release>:-O3>
    )
endif()

target_include_directories(aseity PRIVATE
        ${CMAKE_SOURCE_DIR}/Headers
        # ${LLVM_INCLUDE_DIRS}
)

# target_compile_definitions(aseity PRIVATE ${LLVM_DEFINITIONS})

# Link to the monolithic arch lib target
target_link_libraries(aseity PRIVATE m) # LLVM