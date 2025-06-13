#include <fstream>

#include <optional>
#include <vector>
#include <cstdint>

class ClassDesc {

private:

    /* mutually recursive classes may be unfinished
    for example:
    class A, class B
    add member of type B to A,
    add member of type A to B,
    finish A and B */

    /* 0 to 3 reserved for byte, short, int, long
    4 to 32 reserved to allow checking of callee save registers */
    static unsigned int serial;

    /* top 12 bits reserved for array depth */
    static constexpr uint32_t MAX_CLASS_ID = 1 << 20;

    uint32_t id;
    bool done;
    ClassDesc *superclass; // this is a pointer, no better way to handle null
    std::vector<std::pair<uint32_t, uint32_t> > fields;
    uint32_t size;

public:

    ClassDesc(ClassDesc& _superclass) :
        id{serial},
        done{false},
        superclass{&_superclass},
        size{4}
    {
        if(serial >= MAX_CLASS_ID) {
            throw std::runtime_error{"too many classes loaded"};
        }
        serial++;
    }

    ClassDesc() :
        id{serial},
        done{false},
        superclass{nullptr},
        size{4}
    {
        serial++;
    }

    void addfield(uint32_t arraydepth, ClassDesc& cls) {
        if(done) {
            throw std::runtime_error{"class fields are already frozen"};
        }
        if(arraydepth >= 4096) {
            throw std::runtime_error{"no more than 4096 nested arrays allowed"};
        }
        fields.push_back(std::make_pair(size, (arraydepth << 20) | cls.gettypeid()));
        size += 8; // pointers are 8 bytes
    }

    void addfield(uint32_t arraydepth, unsigned int typ) {
        if(typ < 0 || typ > 3) {
            throw std::runtime_error{"illegal primitive type"};
        }
        if(arraydepth >= 4096) {
            throw std::runtime_error{"no more than 4096 nested arrays allowed"};
        }
        fields.push_back(std::make_pair(size, (arraydepth << 20) | typ));
        size += (1 << typ);
    }

    /* Java style is-a relation */
    bool is_a(ClassDesc& rhs) {
        if(id == rhs.id) {
            return true;
        }
        if(superclass == nullptr) {
            return false;
        }
        return superclass->is_a(rhs);
    }

    uint32_t gettypeid() {
        return id;
    }

    void finish() {
        done = true;
    }

    bool is_finished() {
        return done;
    }
};

unsigned int ClassDesc::serial = 32;

/* always generates position independent code */
class RV64Function {

private:

    /* x8, x9, x18 to x27 are callee save */
    static constexpr uint32_t SAVEMASK = 0x0FFC0300;

    static constexpr unsigned int MAXFRAMESLOTS = 255;

    unsigned int frameslots; /* in 64 bit words */
    bool usex5ret; /* leaf functions can use x5 as return address */
    std::vector<uint32_t> code;
    std::vector<uint32_t> typestate; /* first 32 for registers */

    //TODO: std::map<> jumptargets;
    //TODO: std::map<> jumps;

    void write32(uint32_t inst) {
        code.push_back(inst);
    }

    void assertinteger(unsigned int r) {
        // registers x1 to x4 are special; only modified by special macros
        if(r >= 1 && r <= 4) {
            throw std::runtime_error{"banned register"};
        }
        if(readregtype(r) >= 32) {
            throw std::runtime_error{"expected integer type"};
        }
    }

    uint32_t readregtype(unsigned int r) {
        if(r >= 32) {
            throw std::runtime_error{"illegal register number"};
        }
        return typestate.at(r);
    }

    void writeregtype(unsigned int r, uint32_t t) {
        if(r >= 32) {
            throw std::runtime_error{"illegal register number"};
        }
        typestate.at(r) = t;
        typestate.at(0) = 4; // x0 is always an integer
    }

    /* R-type instructions, no pointer types */
    void rtype(uint32_t funct7, uint32_t rs2, uint32_t rs1, uint32_t funct3, uint32_t rd, uint32_t opcode) {
        assertinteger(rs2);
        assertinteger(rs1);
        writeregtype(rd, 4);
        write32((funct7 << 25) | (rs2 << 20) | (rs1 << 15) | (funct3 << 12) | (rd << 7) | opcode);
    }

    /* I-type instructions, no pointer types */
    void itype(uint32_t imm12, uint32_t rs1, uint32_t funct3, uint32_t rd, uint32_t opcode) {
        assertinteger(rs1);
        writeregtype(rd, 4);
        write32((imm12 << 20) | (rs1 << 15) | (funct3 << 12) | (rd << 7) | opcode);
    }

    void utype(uint32_t imm20, uint32_t rd, uint32_t opcode) {
        writeregtype(rd, 4);
        write32((imm20 << 12) | (rd << 7) | opcode);
    }

    /* expects offset in bytes */
    void branch(uint32_t target, uint32_t rs2, uint32_t rs1, uint32_t cond) {
        // TODO: check target range
        // TODO: record current type, add to fixup table
        uint32_t word = (rs2 << 20) | (rs1 << 15) | (cond << 12) | 99;
        /* fill in offset field later during branch type checking */
        write32(word);
    }

public:

    RV64Function(unsigned int _frameslots, bool _usex5ret) :
        frameslots{_frameslots},
        usex5ret{_usex5ret}
    {
        if(frameslots > MAXFRAMESLOTS) {
            throw std::runtime_error{"exceeded maximum frame size"};
        }
        for(int i = 0; i <= 4; i++) {
            // anything under 32 is an integer
            typestate.push_back(4);
        }
        for(int i = 5; i < 32; i++) {
            typestate.push_back(i);
        }
        for(int i = 0; i < frameslots; i++) {
            // frame slots start as unknown integers
            typestate.push_back(4);
        }
    }

    void dumptofile(const char *filename) {
        std::ofstream out(filename);
        for(uint32_t word : code) {
            out << static_cast<char>(word & 255);
            out << static_cast<char>((word >> 8) & 255);
            out << static_cast<char>((word >> 16) & 255);
            out << static_cast<char>((word >> 24) & 255);
        }
        out.close();
    }

    uint32_t maketarget() {
        uint32_t offset = code.size() * 4; /* offset in bytes */
        // TODO: save current type state in labels
        return offset;
    }

    // TODO: spill_reg, unspill_reg, load_field, store_field, arrays

    // nop and move are pseudoinstructions

    void nop() {
        addi(0, 0, 0);
    }

    void move(int rd, int rs) {
        writeregtype(rd, readregtype(rs));
        // move is a special case, to preserve pointer and singleton types
        write32((rs << 15) | (rd << 7) | 19); // addi rd, rs, 0
    }

    void lui(int rd, uint32_t imm20) {
        utype(imm20, rd, 55);
    }

    // call (disabled if returning to x5), jump, ret (must check return type, callee save)

    void beq(uint32_t rs1, uint32_t rs2, uint32_t target) {
        branch(target, rs2, rs1, 0);
    }

    void bne(uint32_t rs1, uint32_t rs2, uint32_t target) {
        branch(target, rs2, rs1, 1);
    }

    void blt(uint32_t rs1, uint32_t rs2, uint32_t target) {
        branch(target, rs2, rs1, 4);
    }

    void bge(uint32_t rs1, uint32_t rs2, uint32_t target) {
        branch(target, rs2, rs1, 5);
    }

    void bltu(uint32_t rs1, uint32_t rs2, uint32_t target) {
        branch(target, rs2, rs1, 6);
    }

    void bgeu(uint32_t rs1, uint32_t rs2, uint32_t target) {
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
        itype(shamt & 63, rs1, 1, rd, 19);
    }

    void srli(int rd, int rs1, uint32_t shamt) {
        itype(shamt & 63, rs1, 5, rd, 19);
    }

    void srai(int rd, int rs1, uint32_t shamt) {
        itype((1 << 10) | (shamt & 63), rs1, 5, rd, 19);
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
        itype(shamt & 31, rs1, 1, rd, 27);
    }

    void srliw(int rd, int rs1, uint32_t shamt) {
        itype(shamt & 31, rs1, 5, rd, 27);
    }

    void sraiw(int rd, int rs1, uint32_t shamt) {
        itype((1 << 10) | (shamt & 31), rs1, 5, rd, 27);
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
