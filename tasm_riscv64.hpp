class TypeTable {

private:

    std::vector<std::array<std::pair<uint32_t, TypeDesc> > > types;

public:

    TypeTable() {}

    void addtype(...) { ... }
};

class RV64Label {

private:

    static uint32_t serial = 0;
    const uint32_t n;

public:

    RVLabel() : n{serial} { serial++; }

    uint32_t id() const { return n; }
};

class RV64Function {

private:

    /* x8, x9, x18 to x27 are callee save */
    constexpr uint32_t SAVEMASK = 0x0FFC0300;

    std::vector<uint32_t> code;
    std::vector<LabelTypeInfo> labels;

    void write32(uint32_t inst) {
        code.push_back(inst);
    }

    void checkreg(int r) {
        // registers x1 to x4 are special; only modified by special macros
        if(r >= 1 && r <= 4) {
            throw std::runtime_exception{"banned register"};
        }
    }

    /* R-type instructions, no pointer types */
    void rtype(uint32_t funct7, uint32_t rs2, uint32_t rs1, uint32_t funct3, uint32_t rd, uint32_t opcode) {
        update types
        checkreg(rs2);
        checkreg(rs1);
        checkreg(rd);
        write32((funct7 << 25) | (rs2 << 20) | (rs1 << 15) | (funct3 << 12) | (rd << 7) | opcode);
    }

    /* I-type instructions, no pointer types */
    void itype(uint32_t imm12, uint32_t rs1, uint32_t funct3, uint32_t rd, uint32_t opcode) {
        update types
        checkreg(rs1);
        checkreg(rd);
        write32((imm12 << 20) | (rs1 << 15) | (funct3 << 12) | (rd << 7) | opcode);
    }

    void utype(uint32_t imm20, uint32_t rd, uint32_t opcode) {
        update types
        checkreg(rd);
        write32((imm20 << 12) | (rd << 7) | opcode);
    }

    /* expects offset in bytes */
    void branch(RV64Label target, uint32_t rs2, uint32_t rs1, uint32_t cond) {
        check target range
        record current type, add to fixup table
        uint32_t word = (rs2 << 20) | (rs1 << 15) | (cond << 12) | 99;
        word |= ...;
        write32(word)
    }

public:

    void maketarget(RV64Label target) {
        save current type state in labels
        return lnum;
    }

    void lui(int rd, uint32_t imm20) {
        utype(imm20, rd, 55);
    }

    // call, jump, ret (must check return type, callee save)

    void beq(uint32_t rs1, uint32_t rs2, RV64Label target) {
        branch(target, rs2, rs1, 0);
    }

    void bne(uint32_t rs1, uint32_t rs2, RV64Label target) {
        branch(target, rs2, rs1, 1);
    }

    void blt(uint32_t rs1, uint32_t rs2, RV64Label target) {
        branch(target, rs2, rs1, 4);
    }

    void bge(uint32_t rs1, uint32_t rs2, RV64Label target) {
        branch(target, rs2, rs1, 5);
    }

    void bltu(uint32_t rs1, uint32_t rs2, RV64Label target) {
        branch(target, rs2, rs1, 6);
    }

    void bgeu(uint32_t rs1, uint32_t rs2, RV64Label target) {
        branch(target, rs2, rs1, 7);
    }

    void addi(int rd, int rs1, uint32_t imm12) {
        itype(imm12, rs1, 0, rd, 19);
    }

    void slti(int rd, int rs1, uint32_t imm12) {
        itype(imm12, rs1, 2, rd, 19);
    }

    void sltiu(int rd, int rs1, uint32_t imm12) {
        itype(imm12, rs1, 3, rd, 19);
    }

    void xori(int rd, int rs1, uint32_t imm12) {
        itype(imm12, rs1, 4, rd, 19);
    }

    void ori(int rd, int rs1, uint32_t imm12) {
        itype(imm12, rs1, 6, rd, 19);
    }

    void andi(int rd, int rs1, uint32_t imm12) {
        itype(imm12, rs1, 7, rd, 19);
    }

    // shifts take 6 bit immediates

    void slli(int rd, int rs1, uint32_t shamt) {
        itype(shamt, rs1, 1, rd, 19);
    }

    void srli(int rd, int rs1, uint32_t shamt) {
        itype(shamt, rs1, 5, rd, 19);
    }

    void srai(int rd, int rs1, uint32_t shamt) {
        itype((1 << 10) | shamt, rs1, 5, rd, 19);
    }

    void add(int rd, int rs1, int rs2) {
        rtype(0, rs2, rs1, 0, rd, 51);
    }

    void sub(int rd, int rs1, int rs2) {
        rtype(1 << 5, rs2, rs1, 0, rd, 51);
    }

    void sll(int rd, int rs1, int rs2) {
        rtype(0, rs2, rs1, 1, rd, 51);
    }

    void slt(int rd, int rs1, int rs2) {
        rtype(0, rs2, rs1, 2, rd, 51);
    }

    void sltu(int rd, int rs1, int rs2) {
        rtype(0, rs2, rs1, 3, rd, 51);
    }

    void xor_(int rd, int rs1, int rs2) {
        rtype(0, rs2, rs1, 4, rd, 51);
    }

    void srl(int rd, int rs1, int rs2) {
        rtype(0, rs2, rs1, 5, rd, 51);
    }

    void sra(int rd, int rs1, int rs2) {
        rtype(1 << 5, rs2, rs1, 5, rd, 51);
    }

    void or_(int rd, int rs1, int rs2) {
        rtype(0, rs2, rs1, 6, rd, 51);
    }

    void and_(int rd, int rs1, int rs2) {
        rtype(0, rs2, rs1, 7, rd, 51);
    }

    /* M extension */

    void mul(int rd, int rs1, int rs2) {
        rtype(1, rs2, rs1, 0, rd, 51);
    }

    void mulh(int rd, int rs1, int rs2) {
        rtype(1, rs2, rs1, 1, rd, 51);
    }

    void mulhsu(int rd, int rs1, int rs2) {
        rtype(1, rs2, rs1, 2, rd, 51);
    }

    void mulhu(int rd, int rs1, int rs2) {
        rtype(1, rs2, rs1, 3, rd, 51);
    }

    void div(int rd, int rs1, int rs2) {
        rtype(1, rs2, rs1, 4, rd, 51);
    }

    void divu(int rd, int rs1, int rs2) {
        rtype(1, rs2, rs1, 5, rd, 51);
    }

    void rem(int rd, int rs1, int rs2) {
        rtype(1, rs2, rs1, 6, rd, 51);
    }

    void remu(int rd, int rs1, int rs2) {
        rtype(1, rs2, rs1, 7, rd, 51);
    }

    /* specific to RV64 */

    void addiw(int rd, int rs1, uint32_t imm12) {
        itype(imm12, rs1, 0, rd, 27);
    }

    void slliw(int rd, int rs1, uint32_t shamt) {
        itype(shamt, rs1, 1, rd, 27);
    }

    void srliw(int rd, int rs1, uint32_t shamt) {
        itype(shamt, rs1, 5, rd, 27);
    }

    void sraiw(int rd, int rs1, uint32_t shamt) {
        itype((1 << 10) | shamt, rs1, 5, rd, 27);
    }

    void addw(int rd, int rs1, int rs2) {
        rtype(0, rs2, rs1, 0, rd, 59);
    }

    void subw(int rd, int rs1, int rs2) {
        rtype(1 << 5, rs2, rs1, 0, rd, 59);
    }

    void sllw(int rd, int rs1, int rs2) {
        rtype(0, rs2, rs1, 1, rd, 59);
    }

    void srlw(int rd, int rs1, int rs2) {
        rtype(0, rs2, rs1, 5, rd, 59);
    }

    void sraw(int rd, int rs1, int rs2) {
        rtype(1 << 5, rs2, rs1, 5, rd, 59);
    }

    /* RV64M specific */

    void mulw(int rd, int rs1, int rs2) {
        rtype(1, rs2, rs1, 0, rd, 59);
    }

    void divw(int rd, int rs1, int rs2) {
        rtype(1, rs2, rs1, 4, rd, 59);
    }

    void divuw(int rd, int rs1, int rs2) {
        rtype(1, rs2, rs1, 5, rd, 59);
    }

    void remw(int rd, int rs1, int rs2) {
        rtype(1, rs2, rs1, 6, rd, 59);
    }

    void remuw(int rd, int rs1, int rs2) {
        rtype(1, rs2, rs1, 7, rd, 59);
    }
};
