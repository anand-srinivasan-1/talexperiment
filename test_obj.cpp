#include <iostream>
#include <cassert>

#include "tasm_riscv64.hpp"

#define _ f.

/* test objects */

int main(int argc, char **argv) {
    if(argc < 2) {
        std::cout << "expected file argument\n";
        return 1;
    }
    ClassDesc c1;
    c1.addfield(0, 2); // int
    c1.addfield(0, 1); // short
    c1.addfield(0, 0); // byte
    c1.addfield(0, 0);
    c1.finish();
    assert(c1.sizebytes() == 12);
    ClassDesc c2{c1};
    c2.addfield(0, 3); // long
    c2.finish();
    assert(c2.sizebytes() == 20);
    std::vector args{c1.gettypeid(), c2.gettypeid()};
    RV64Function f{100, false, args, 4};
    _ open_frame();
    _ load_field(15, 10, c1, 1); // field 1: short
    _ store_field(11, 15, c2, 4); // field 4: long
    _ load_field(16, 10, c1, 2); // field 2: short
    _ spill_reg(70, 16);
    _ unspill_reg(17, 70);
    _ store_field(11, 17, c2, 0); // field 0: int
    _ close_frame();
    //_ ret(); // no need to undo stack frame
    f.dumptofile(argv[1]);
    return 0;
}
