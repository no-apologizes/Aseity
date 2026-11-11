## What is this?

This is a rewrite of my compiler for my own language, Aseity. I've only created up to the parser but have applied fixes and refined the grammar. This is built to run on Linux x86, I don't know how it will run on other Linux architectures, and it won't work on Windows.

Tested with:
- GCC 16.2.1
- CMake 4.4.5
- Arch Linux x64
- CachyOS Normal Kernel

This should work with any compiler in the last decade, as it uses GNU computed gotos, and at least CMake 3.25.

## For Snowglobe:
This is only built up to the parser and `ast_print.c` and `Headers/ast_print.h` are both fully AI generated because it's printing stuff in a nice format and I don't want to spend hours on it. I wired it up myself in `main.c` though.


## Current Architecture
- Lexer
  - Computed gotos
  - Single-threaded
  - UTF-8 Support for mathematical symbols[^1]
- Dual Arena
  - Bump allocation:
    - A single pointer that advances and is only freed at the end of compilation
  - Recyclable allocation:
    - A frequently used allocations, like symbols and local scopes
- String Pool
  - Strings are interned using bump allocation
- Parser
  - A shift-reduce parser

### Demo:
https://www.youtube.com/watch?v=CuCoGDCMznE

### Build & Usage:
You can either download it or build from source. To not build from source, download the release, make it executable and run it.

```
git clone https://github.com/no-apologizes/Aseity.git

cd Aseity

mkdir build && cd build

cmake ..

cmake --build .
```

### AI Disclaimer:
AI was used a little more than I usually do, it was used to plan and research. In addition to generating some repetitive code that best suits it, it also aided in speeding up production time with some functional code, I manually reviewed and hand-traced every line it wrote. It also generated tests and I guess my code is more bug free than if I didn't use AI.
I can still explain every line, AMA.

[^1]: Some mathematical operators, the CJK Unified Ideographs which are shared logographic characters from Chinese, Japanese, Korean, and historical Vietnamese. Also, greek letters for math.