## What is this?

This is a compiler for the Aseity language
- Statically typed
- Strongly typed
- No implicit conversions
- More


Aseity is a low-level language that is heavily inspired by C but fixes its weak typing and enforces more explicit typing.
Comments are ` ./ comment \. `. There are no single-line comments.


## Parts:
- Lexer:
  - Computed gotos
  - All whitespaces and comments are skipped, so everything can be written on one line
- Parser: 
  - andie...


# Planning

Maybe(Can be implemented at any time):
- type aliases

W/error flags:
- truncation/lossy casting
- slice out of bounds checking

Arrays:

``` i64{} view = passed_array{@}```
is a readonly view into ```passed_array```,
whereas ```i64 view{0} = passed_array{@}``` is the first element of ```passed_array```

```

i64{} passed_array{3} = {1, 2, 3}|

i64 array(passed_array: i64{}) [
    i64{} view_of_passed_array = passed_array{@}| ./ view of passed array \.
    
    i64 first_element = passed_array{0}| ./ from view: 1 \.
    i64 first_element_of_mutable = view_of_passed_array{0}| ./ from local: 1 \.
    
    passed_array{0} = 0| ./ can't do that \. // wait you can, you wouldn't be able to if it was passed_array: const i64{} or some variation
    view_of_passed_array{0} = 0| ./ can do this, view_of_passed_array{0} is now 0 instead of 1 \.
    
    return first_element_of_mutable first_element +
]|
```

Arena is a dual one, bump and recycle, bump just advances a pointer and is meant for lifetime storage: freed once

Recycle is a temp arena for short term scopes