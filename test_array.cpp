#include <iostream>

#include <cstdint>

#include "tasm_riscv64.hpp"

#define _ f.

/* test arrays */

int main(int argc, char **argv) {
    if(argc < 2) {
        std::cout << "expected file argument\n";
        return 1;
    }
    std::vector<uint32_t> args{(2 << 20) | 4, 4};
    RV64Function f{0, args, 4};
    _ array_length(5, 10);
    _ array_get(6, 10, 11);
    _ array_put(10, 11, 6, 7);
    //_ ret();
    f.dumptofile(argv[1]);
    return 0;
}
