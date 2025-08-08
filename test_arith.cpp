#include <iostream>

#include "tasm_riscv64.hpp"

#define _ f.

/* test arithmetic instructions (no jumps or memory access) */

int main(int argc, const char **argv) {
    if(argc < 2) {
        std::cout << "expected file argument\n";
        return 1;
    }
    std::vector<uint32_t> args;
    RV64Function f{0, args, 4};
    _ nop();
    _ move(5, 6);
    _ lui(5, -100000);
    _ addi(5, 6, -100);
    _ slti(5, 6, -100);
    _ sltiu(5, 6, -100);
    _ xori(5, 6, -100);
    _ ori(5, 6, -100);
    _ andi(5, 6, -100);
    _ slli(5, 6, 33);
    _ srli(5, 6, 33);
    _ srai(5, 6, 33);
    _ add(5, 6, 7);
    _ sub(5, 6, 7);
    _ sll(5, 6, 7);
    _ slt(5, 6, 7);
    _ sltu(5, 6, 7);
    _ xor_(5, 6, 7);
    _ srl(5, 6, 7);
    _ sra(5, 6, 7);
    _ or_(5, 6, 7);
    _ and_(5, 6, 7);
    _ mul(5, 6, 7);
    _ mulh(5, 6, 7);
    _ mulhsu(5, 6, 7);
    _ mulhu(5, 6, 7);
    _ div(5, 6, 7);
    _ divu(5, 6, 7);
    _ rem(5, 6, 7);
    _ remu(5, 6, 7);
    _ ret(); // no need to undo stack frame
    f.dumptofile(argv[1]);
    return 0;
}
