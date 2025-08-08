This is an experiment to write a type-aware assembler for RISC-V. It aims to support fixed-point arithmetic, Java-style classes, arrays, all RISC-V control flow instructions, and a realistic calling convention.

Raw binary files can be disassembled using `riscv64-linux-gnu-objdump -D -b binary -m riscv <file>`

## Motivation (why do we care about typed assembly?)

Consider a Javascript engine in a web browser. The whole point is to download untrusted and potentially malicious code from random sites and run it on the user's machine, meaning the sandbox must be very tight. Even worse, in most browsers the JIT is turned on by default for speed, meaning the untrusted code goes through a fairly complex compiler and produces native code that runs on the user's machine.

Understandably, there is a lot of time and effort spent finding bugs in these engines, but people regularly contribute new code to them, and even a single type confusion vulnerability (for example) may be enough to trigger memory corruption, which is often the beginning of a remote exploit.

Formal verification (as CompCert did) is impractical given new features are regularly added. CompCert took years to verify, and C99 is not a moving target. (Even then, CompCert's optimizer is comparable to a mainstream compiler's -O1 or maybe a little better.)

## Typed Assembly Language

[Typed assembly language](https://www.cs.cornell.edu/talc/overview.html), first worked out by [Morrisett et al.](https://www.cs.cornell.edu/talc/papers.html), involves imposing a type system on assembly language. The authors showed that common compiler optimizations can preserve type information all the way to instruction selection.

A hypothetical Javascript JIT that emits typed assembly would generate provably type-safe and memory-safe code, protecting the user against most traditional exploits. The trusted code base would be small, since the assembler must reject programs that fail type checking, but other than that a wrongly-typed piece of IR will eventually be caught before producing machine code.

## An example RISC-V type system

For simplicity, this project does not support floating point instructions.

Primitives are `byte`, `short`, `int`, `long` like in Java.

Pointer types are distinguished from integer types. Most arithmetic instructions only work on integers. Special instructions for accessing fields accept values that are pointers to objects, and compile down to loads and stores.

Objects are specified as lists of fields with offsets. Primitive fields may have any primitive type, unlike registers and stack spill slots which are always 64 bit values.

Objects support inheritance. If `A` has a subclass `B`, `B` begins with all fields in `A` so field accesses to a pointer of type `A` do not need to know if the underlying object is `A` or `B`.

At function entry, registers start with singleton integer types so that at exits it can be proven that callee-save registers are the same as their entry values, as is required by the standard calling convention.

Primitive types are assigned numbers 0 to 3 in class fields, 4 is the default integer type created by instructions that leave an integer in the destination register, 5 to 31 are singleton integers for their corresponding registers. All instructions expecting integer types check the type is less than 32, since numbers 32 and up are assigned to classes. Class fields can never have types from 4 to 31, since primitive fields only accept 0 to 3, and pointers only accept a class type, which must have a type number of at least 32.

### Features

- Function arguments
- Arithmetic operations
- All primitive integer sizes
- More or less standard calling convention for RISC-V
- Object types with inheritance
- Support for stack frames (without a frame pointer)
- Array accessor macros
