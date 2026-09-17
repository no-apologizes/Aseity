## What is this?

This is a compiler for the Aseity language
- Statically typed
- Strongly typed
- No implicit conversions
- Mostly postfix
- At least postfix for math

Aseity is a low-level language that is heavily inspired by C and fixes its weak typing while also enforcing more explicit typing.
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

Fix:
- fix nested comments ./ ./ comment \. \. will stop at the first '\.'
    - in_comment flag
        - ./ increments depth by 1
        - \. decrements depth by 1
        - stop when depth = 0
        - save first ./ row and col for errors, when depth goes from 0 to 1

W/error flags:
- truncation/lossy casting - Wlossy-cast
- slice out of bounds checking -Wslice-bounds
    - sub slicing out of bounds checking -Wsubslice-bounds
        - can't do arr{4..} if i64{3} arr = {0, 1, 2}

## Arrays:

``` i64{} view = passed_array{@}```
is a mutable copy of the view into ```passed_array```,
whereas ```i64 view = passed_array{0}``` is the first element of ```passed_array```

params are non-aliasing by default with an explicit keyword

array sub slicing syntax
if ```i64{10} arr = {0, 2, 3, 4, 5, 6, 7, 8, 9, 10}```

```arr{start..end}``` means arr{@}, implied full length: arr{0, len}

```arr{3..}``` implies upper bound, 3 to len: {4, 5, 6... 9, 10}, [3, len]

```arr{..5}``` implies lower bound, 0 to 5: {0, 2, 3, 4, 5}, [0, 5]

```arr{2..5}``` means slice from index 2 to 5: {3, 4, 5}, [2, 5]

aliasing keyword: alias

```
i64{3} passed_array = {1, 2, 3}|

./ don't know how func calls will work, ufcs, postfix, or ufcs sugar \.
return array(passed_array)

i64 array(passed_array: const i64{}) [
    const i64{} view_of_passed_array = passed_array{@}| ./ view into passed array \.
    
	i64{} mutable_array = copy(passed_array)| ./ intrinsic or runtime that allocates a new buffer \.
    
    i64 first_element = passed_array{0}| ./ from view: 1 \.
    i64 mutable_first_element = mutable_array {0}| ./ from local: 1 \.
    
    ./
    view_of_passed_array{0} = 0|
    passed_array{0} = 0| ./ can't do either of these, they are both const \. \.
    
    mutable_array {0} = 0| ./ can do this, mutable_array{0} is now 0 instead of 1 \.
    
    return mutable_first_element first_element +|
]|
```

Arena is a dual one, bump and recycle

transient is short-term pass-wide free once, recycle is frequent, uniform sized-nodes