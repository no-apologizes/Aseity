cmake_minimum_required(VERSION 4.1)
project(aseity C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(LLVM REQUIRED CONFIG)

add_executable(aseity
        main.c
        lexer.c
        parser.c
        symbol_table.c
        arena.c
        ir.c
        ir_gen.c
        codegen_ir.c
)

target_compile_options(aseity PRIVATE
                        -march=znver3 # AMD Zen 3 Cpu architecture
                        -O3 -std=c17
                        -flto # Link-time optimization
                        -fno-strict-aliasing # Pointers of different types can point to the same memory location
                        -Wall -Wextra -Wshadow -ggdb3  # Errors
                        #-Wconversion -Wpedantic
                        )

target_include_directories(aseity PRIVATE
        ${CMAKE_SOURCE_DIR}/Headers
        ${LLVM_INCLUDE_DIRS}
)

target_compile_definitions(aseity PRIVATE ${LLVM_DEFINITIONS})

# Link to the monolithic arch lib target
target_link_libraries(aseity PRIVATE LLVM m)