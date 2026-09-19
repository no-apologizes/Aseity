`i64{} view = passed_array{@}`
is a mutable copy of the view into `passed_array`,
whereas `i64 view = passed_array{0}` is the first element of `passed_array`

params are non-aliasing by default with an explicit keyword

`@` is an operator, valid only in the value position between {} on an existing array.
Something like `i64{@} array = ...` isn't correct, `i64{} view = existing_array{@}` is though.
A view should never outlive what it's viewing, as a fixed-size array, `i64{@}` owns its data, a view into it does not. This is exactly the borrow-checker problem.

A bare `{}` in the value position `i64{4} array = {}` is an error

array sub slicing syntax
if `i64{10} arr = {0, 2, 3, 4, 5, 6, 7, 8, 9, 10}`

arr{start..end}`means arr{@}, implied full length: arr{0, len}

`rr{3..}` implies upper bound, 3 to len: {4, 5, 6... 9, 10}, [3, len]

`arr{..5}` implies lower bound, 0 to 5: {0, 2, 3, 4, 5}, [0, 5]

`arr{2..5}` means slice from index 2 to 5: {3, 4, 5}, [2, 5]

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