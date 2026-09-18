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

Primitive types can be NULL which means their value is unknown
hollow = null

Most types can be hollow and for an 8-byte number, 7 bytes of padding would be added for alignment, same with structs and arrays.
Unit, never, and the internal type vile can't be hollow, there is no 'unknown' state for any of them.

Functions are first-class types.
Const-ness is stored in the ASTNode instead of the type.

The type struct is opaque to enforce interning only though provided functions