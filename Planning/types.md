### This is how  the type system works

Below are types, excluding primitives, as they follow a simple pattern.

For unsigned, you put a 'u' plus how many bits it is: `u8`, `u32`, `u128`.
The same is for signed and floats, with 'i' and 'f' used respectively: `i64`, `f32`, `f128`.
There are no 8 or 16-bit floats. 

| Type              | Syntax                       |
|-------------------|------------------------------|
| Unit              | `unit`                       |
| Never **(und)**   | `never`                      |
| Boolean           | `bool`                       |
| String            | `str`                        |
| Character         | `char`                       |
| Pointer **(und)** | (prim)`*`                    |
| Array             | `{}`                         |
| Struct            | `struct`                     |
| Function          | (type) (name)(params) (body) |

`unit` is like C's void where a function is used for its side effects
`never` is for things that provably never end or return, like `exit(1)`


Primitive types can be NULL which means their value is unknown

hollow

`null` is the value you'd assign to a hollow pointer: `hollow i64* ptr = null|` or `NULL`

Things that can be hollow(and why):
- Pointers and slices
  - A null pointer already has a distinguishable bit pattern
- Prims, fixed arrays, and structs
  - For these, no spare bit pattern exists, every pattern is a valid one, so use a tag wrapper over a reserved sentinel value as that would burn a legitimate value and there's no universal 'safe' value to sacrifice. Also, something like an 8-bit number that only holds 256 values should just have one missing, that's not very good

The only thing that can't be hollow are per-element arrays(sparse arrays), that'll come later

Hollow flag is 1 byte, but alignment forces it from 9 → 16
Unit, never, and the internal type vile can't be hollow, there is no 'unknown' state for any of them.

Functions are first-class types.
Const-ness is stored in the ASTNode instead of the type.

The type struct is opaque to enforce interning only though provided functions