This is an experiment to write a type-aware assembler for RISC-V. It supports fixed-point arithmetic, Java-style classes, arrays, all RISC-V control flow instructions, and a realistic calling convention.

Raw binary files can be disassembled using `riscv64-linux-gnu-objdump -D -b binary -m riscv <file>`

## Motivation (why do we care about typed assembly?)

Consider a Javascript engine in a web browser. The whole point is to download untrusted and potentially malicious code from random sites and run it on the user's machine, meaning the sandbox must be very tight. Even worse, in most browsers the JIT is turned on by default for speed, meaning the untrusted code goes through a fairly complex compiler and produces native code that runs on the user's machine.

Understandably, there is a lot of time and effort spent finding bugs in these engines, but people regularly contribute new code to them, and even a single type confusion vulnerability (for example) may be enough to trigger memory corruption, which is often the beginning of a remote exploit.

Formal verification (as CompCert did) is impractical given new features are regularly added. CompCert took years to verify, and C99 is not a moving target. (Even then, CompCert's optimizer is comparable to a mainstream compiler's -O1 or maybe a little better.)

## Typed Assembly Language

[Typed assembly language](https://www.cs.cornell.edu/talc/overview.html), first researched by [Morrisett et al.](https://www.cs.cornell.edu/talc/papers.html), involves imposing a type system on assembly language. The authors showed that common compiler optimizations can preserve type information all the way to instruction selection.

A hypothetical Javascript JIT that emits typed assembly would generate provably type-safe and memory-safe code, protecting the user against most traditional exploits. The trusted code base would be small, since the assembler must reject programs that fail type checking, but other than that a wrongly-typed piece of IR will eventually be caught before producing machine code.

## An example RISC-V type system

For simplicity, this project does not support floating point instructions. Primitive types are taken from Java (`byte`, `short`, `int`, `long`), and all registers are sized for `long`. Pointer types can point to an instance of a class or an array, and are 8 bytes. Classes can have members of any type, and start with a 4 byte opaque header. All classes start with the fields of their immediate parent class. Arrays have a header with a 4 byte length.

calling conventions, basic blocks, type rules
