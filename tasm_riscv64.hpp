#include <iostream>
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

    /* load and store instructions use 12 bit offsets */
    static constexpr uint32_t MAX_SIZE_BYTES = 2048;

public:

    ClassDesc(ClassDesc& _superclass) :
        id{serial},
        done{false},
        superclass{&_superclass}
    {
        if(serial >= MAX_CLASS_ID) {
            throw std::runtime_error{"too many classes loaded"};
        }
        for(auto field : superclass->fields) {
            fields.push_back(field);
        }
        size = superclass->size;
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
        if(typ > 3) {
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

    uint32_t sizebytes() {
        return size;
    }

    void finish() {
        if(size >= MAX_SIZE_BYTES) {
            throw std::runtime_error{"object over maximum size"};
        }
        done = true;
    }

    bool is_finished() {
        return done;
    }

    uint32_t fieldtype(unsigned int field) {
        return fields.at(field).second; // fields are (offset, type)
    }

    uint32_t fieldsize(unsigned int field) {
        switch(fieldtype(field)) {
            case 0: return 1;
            case 1: return 2;
            case 2: return 4;
            default: return 8;
        }
    }

    uint32_t fieldoffset(unsigned int field) {
        return fields.at(field).first; // fields are (offset, type)
    }
};

unsigned int ClassDesc::serial = 32;

/* always generates position independent code */
class RV64Function {

private:

    /* x8, x9, x18 to x27 are callee save */
    static constexpr uint32_t SAVEMASK = 0x0FFC0300;

    /* end of frame reserved for return address,
    frame must take less than 2048 bytes */
    static constexpr unsigned int MAXFRAMESLOTS = 254;

    unsigned int frameslots; /* in 64 bit words */
    uint32_t returntype;
    bool frame_open;
    std::vector<uint32_t> code;
    std::vector<uint32_t> typestate; /* first 32 entries for registers */

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

    void write_load_inst(uint32_t size, uint32_t rd, uint32_t rptr, uint32_t offset) {
        write32((offset << 20) | (rptr << 15) | (size << 12) | (rd << 7) | 3);
    }

    void write_store_inst(uint32_t size, uint32_t rptr, uint32_t offset, uint32_t rs) {
        uint32_t offset_11_5 = (offset >> 5) & 0b1111111;
        uint32_t offset_4_0 = offset & 0b11111;
        write32((offset_11_5 << 25) | (rs << 20) | (rptr << 15) | (size << 12) | (offset_4_0 << 7) | 35);
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

    RV64Function(unsigned int _frameslots, std::vector<uint32_t> params, uint32_t _returntype) :
        frameslots{_frameslots},
        returntype{_returntype},
        frame_open{false}
    {
        if(frameslots > MAXFRAMESLOTS) {
            throw std::runtime_error{"exceeded maximum frame size"};
        }
        if(params.size() > 8) {
            throw std::runtime_error{"exceeded maximum parameter count"};
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
        for(int i = 0; i < params.size(); i++) {
            /* argument registers a0 to a7 are x10 to x17 */
            writeregtype(10 + i, params.at(i));
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

    void demoteclass(uint32_t r, ClassDesc& oldtype, ClassDesc& newtype) {
        if(!oldtype.is_a(newtype)) {
            throw std::runtime_error{"illegal type demotion"};
        }
        writeregtype(r, (readregtype(r) & 0xFFF00000) | newtype.gettypeid());
    }

    void open_frame() {
        if(frame_open) {
            throw std::runtime_error{"frame is already open"};
        }
        frame_open = true;
        // some number of 64 bit words, then return address
        uint32_t slot_bytes = frameslots * 8;
        uint32_t frame_bytes = slot_bytes + 8;
        write32((-frame_bytes << 20) | 0x10113); // addi sp, sp, -frame_bytes
        write_store_inst(3, 2, slot_bytes, 1); // sd slot_bytes(sp), ra
    }

    void close_frame() {
        if(!frame_open) {
            throw std::runtime_error{"frame is not open"};
        }
        frame_open = false;
        uint32_t slot_bytes = frameslots * 8;
        uint32_t frame_bytes = slot_bytes + 8;
        write_load_inst(3, 1, 2, slot_bytes); // ld ra, slot_bytes(sp)
        write32((frame_bytes << 20) | 0x10113); // addi sp, sp, frame_bytes
    }

    void array_length(uint32_t rd, uint32_t rs) {
        if((readregtype(rs) >> 20) == 0) {
            throw std::runtime_error{"expected array type"};
        }
        writeregtype(rd, 4);
        // first 4 bytes in array are signed length field, like Java
        // first 8 bytes in array are reserved for header
        write_load_inst(2, rd, rs, 0); // lw rd, (rs)
    }

    void array_get(uint32_t rd, uint32_t rptr, uint32_t ri) {
        if(readregtype(ri) >= 32) {
            throw std::runtime_error{"index must be integer type"};
        }
        uint32_t rptype = readregtype(rptr);
        if((rptype >> 20) == 0) {
            throw std::runtime_error{"expected array type"};
        }
        uint32_t basetype = rptype & 0xFFFFF;
        uint32_t member_adepth = (rptype >> 20) - 1;
        uint32_t logsize = std::max(basetype, 3u);
        if(member_adepth > 0) {
            logsize = 3; // all pointers are 8 bytes
        }
        write_load_inst(2, rd, rptr, 0); // lw rd, (rptr)
        write32(0x6463 | (ri << 15) | (rd << 20)); // bltu ri, rd, +8 bytes
        write32(0); // illegal instruction
        write32(0x1013 | (rd << 7) | (ri << 15) | (logsize << 20)); // slli rd, ri, log2(size)
        write_load_inst(logsize, rd, rd, 8); // load <size> rd, [rd + 8]
        writeregtype(rd, (member_adepth << 20) | basetype);
    }

    void array_put(uint32_t rptr, uint32_t ri, uint32_t rs, uint32_t rtmp) {
        if(readregtype(ri) >= 32) {
            throw std::runtime_error{"index must be integer type"};
        }
        uint32_t rptype = readregtype(rptr);
        if((rptype >> 20) == 0) {
            throw std::runtime_error{"expected array type"};
        }
        if(rptype - (1 << 20) != readregtype(rs)) {
            throw std::runtime_error{"data type does not fit in array"};
        }
        uint32_t basetype = rptype & 0xFFFFF;
        uint32_t member_adepth = (rptype >> 20) - 1;
        uint32_t logsize = std::max(basetype, 3u);
        if(member_adepth > 0) {
            logsize = 3; // all pointers are 8 bytes
        }
        writeregtype(rtmp, 4);
        uint32_t rt = rtmp;
        write_load_inst(2, rt, rptr, 0); // lw rt, (rptr)
        write32(0x6463 | (ri << 15) | (rt << 20)); // bltu ri, rt, +8 bytes
        write32(0); // illegal instruction
        write32(0x1013 | (rt << 7) | (ri << 15) | (logsize << 20)); // slli rt, ri, log2(size)
        write_store_inst(logsize, rt, 8, rs); // store <size> [rt + 8], rt
    }

    void spill_reg(uint32_t slot, uint32_t rs) {
        typestate.at(32 + slot) = readregtype(rs);
        write_store_inst(3, 2, slot * 8, rs); // sd offset(sp), rs
    }

    void unspill_reg(uint32_t rd, uint32_t slot) {
        writeregtype(rd, typestate.at(32 + slot));
        write_load_inst(3, rd, 2, slot * 8); // ld rd, offset(sp)
    }

    void load_field(uint32_t rd, uint32_t rptr, ClassDesc& cls, unsigned int field) {
        if(!cls.is_finished()) {
            throw std::runtime_error{"class is not finished"};
        }
        if(readregtype(rptr) != cls.gettypeid()) {
            throw std::runtime_error{"pointer has wrong type"};
        }
        uint32_t desttype = cls.fieldtype(field);
        uint32_t destsize = cls.fieldsize(field);
        uint32_t offset = cls.fieldoffset(field);
        writeregtype(rd, desttype);
        write_load_inst(destsize, rd, rptr, offset);
    }

    void store_field(uint32_t rptr, uint32_t rs, ClassDesc& cls, unsigned int field) {
        if(!cls.is_finished()) {
            throw std::runtime_error{"class is not finished"};
        }
        uint32_t desttype = cls.fieldtype(field);
        uint32_t destsize = cls.fieldsize(field);
        uint32_t offset = cls.fieldoffset(field);
        if(readregtype(rs) != desttype && desttype >= 32) {
            /* if desttype is a primitive then we don't care about
            the pointer value - only coining a new pointer is illegal */
            throw std::runtime_error{"value incompatible with field type"};
        }
        if(readregtype(rptr) != cls.gettypeid()) {
            throw std::runtime_error{"pointer has wrong type"};
        }
        uint32_t offset_11_5 = (offset >> 5) & 127;
        uint32_t offset_4_0 = offset & 31;
        write_store_inst(destsize, rptr, offset, rs);
    }

    // nop and move are pseudoinstructions

    void nop() {
        addi(0, 0, 0);
    }

    void move(uint32_t rd, uint32_t rs) {
        writeregtype(rd, readregtype(rs));
        // move is a special case, to preserve pointer and singleton types
        write32((rs << 15) | (rd << 7) | 19); // addi rd, rs, 0
    }

    void lui(uint32_t rd, uint32_t imm20) {
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
