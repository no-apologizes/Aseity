## What is this?

This is a compiler for the Aseity language
- Statically typed
- Strongly typed
- No implicit conversions
- Mostly postfix
- At least postfix for math
- Single Threaded (for now)
- Linux x86 only

Aseity is a low-level language that is heavily inspired by C and fixes its weak typing while also enforcing more explicit typing.
Comments are ` ./ comment \. `. There are no single-line comments.

## Parts:
- Lexer:
    - Computed gotos
    - All whitespaces and comments are skipped, so everything can be written on one line
- Parser:
    - k

# Planning

TODO: Bold means **completed**
- **hollow and alias keywords and finalize keywords for lexer**

const-ness lives on the ASTNode, not inside Type

Maybe(Can be implemented at any time):
- type aliases

Fix:

W/error flags:
- truncation/lossy casting -Wlossy-cast
- slice out of bounds checking -Wslice-bounds
    - sub slicing out of bounds checking -Wsubslice-bounds
        - can't do arr{4..} if i64{3} arr = {0, 1, 2}

Probably not default to no-alias

Arena is a dual one, bump and recycle

transient is short-term pass-wide free once, recycle is frequent, uniform sized-nodes