cmake_minimum_required(VERSION 4.1)
project(aseity C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(LLVM REQUIRED CONFIG)

add_executable(aseity
        main.c
        lexer.c
)

target_compile_options(aseity PRIVATE -march=znver3 -O2 -Wall -Wextra)

target_include_directories(aseity PRIVATE
        ${CMAKE_SOURCE_DIR}/Headers
        ${LLVM_INCLUDE_DIRS}
)

target_compile_definitions(aseity PRIVATE ${LLVM_DEFINITIONS})

# Link to the monolithic arch lib target
target_link_libraries(aseity PRIVATE LLVM m)