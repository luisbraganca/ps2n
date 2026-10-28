// Plumbing for the IOP kernel libraries that run as host code.
//
// A kernel library (thbase, sifcmd, sysclib, ...) is a table of exports by
// index, the numbering IRX import stubs use. Each export is a trap address
// (cpu.h) or, for routines that must run as guest code because they call
// back into module code and may block, an address in a small block of
// generated guest code.
#pragma once

#include "iop/cpu.h"
#include "stubs/format.h"

#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

namespace ps2::iop::hle {

// o32: arguments 0-3 in $a0-$a3, the rest at 16($sp) onward.
inline uint32_t arg(int i) {
    return i < 4 ? g_cpu.gpr[R_A0 + i] : mem::read32(g_cpu.gpr[R_SP] + 16 + 4 * static_cast<uint32_t>(i - 4));
}
inline int32_t sarg(int i) { return static_cast<int32_t>(arg(i)); }
inline void ret(int32_t v) { g_cpu.gpr[R_V0] = static_cast<uint32_t>(v); }
inline void retU(uint32_t v) { g_cpu.gpr[R_V0] = v; }

struct Export {
    uint16_t index;
    const char* name;
    HleFn fn;   // nullptr: known but unimplemented (reported when called)
};

// Declares a kernel library implemented on the host.
void defineLibrary(const char* name, uint16_t version, std::initializer_list<Export> exports);
// Points one export at guest code instead of a trap.
void setExport(const char* library, uint16_t index, uint32_t addr);
// Address of a host library's export (for generated guest code that calls it).
uint32_t exportAddress(const char* library, uint16_t index);
bool isHostLibrary(const std::string& name);
// Reports a call to an export with no implementation; returns 0.
void unimplementedExport();

// Libraries exported by loaded modules (loadcore RegisterLibraryEntries).
int registerGuestLibrary(uint32_t exportTable);
int releaseGuestLibrary(uint32_t exportTable);
// Address of export `index` of a guest library, or 0.
uint32_t guestLibraryTable(const std::string& name);

// Links every import table in a module image: each stub becomes `j target`.
// Imports from a library nobody provides yet link to reporting traps and are
// linked again when a module registers that library.
void linkImports(uint32_t textStart, uint32_t textSize, const std::string& moduleName);

// Name of the export a trap stands for ("thbase::CreateThread").
std::string exportName(const std::string& library, uint32_t index);

// Writes small guest routines into IOP memory.
class CodeBuilder {
public:
    explicit CodeBuilder(uint32_t addr) : addr_(addr) {}
    uint32_t here() const { return addr_; }
    void emit(uint32_t insn);

    using Label = int;
    Label label();
    void bind(Label l);

    void nop() { emit(0); }
    void move(uint32_t rd, uint32_t rs) { emit((rs << 21) | (rd << 11) | 0x21); }   // addu rd, rs, $zero
    void addiu(uint32_t rt, uint32_t rs, int16_t imm) { emit(itype(0x09, rs, rt, static_cast<uint16_t>(imm))); }
    void lw(uint32_t rt, int16_t off, uint32_t base) { emit(itype(0x23, base, rt, static_cast<uint16_t>(off))); }
    void sw(uint32_t rt, int16_t off, uint32_t base) { emit(itype(0x2B, base, rt, static_cast<uint16_t>(off))); }
    void jal(uint32_t target) { emit((3u << 26) | ((target >> 2) & 0x03FFFFFFu)); }
    void j(uint32_t target) { emit((2u << 26) | ((target >> 2) & 0x03FFFFFFu)); }
    void jr(uint32_t rs) { emit((rs << 21) | 0x08); }
    void jalr(uint32_t rs) { emit((rs << 21) | (31u << 11) | 0x09); }
    void beq(uint32_t rs, uint32_t rt, Label l) { branch(0x04, rs, rt, l); }
    void bne(uint32_t rs, uint32_t rt, Label l) { branch(0x05, rs, rt, l); }

private:
    static uint32_t itype(uint32_t op, uint32_t rs, uint32_t rt, uint16_t imm) { return (op << 26) | (rs << 21) | (rt << 16) | imm; }
    void branch(uint32_t op, uint32_t rs, uint32_t rt, Label l);

    uint32_t addr_;
    std::vector<uint32_t> labels_;
    std::vector<std::pair<uint32_t, Label>> fixups_;
};

// printf arguments under o32, from registers and the stack or from a va_list.
class IopArgs final : public PrintfSource {
public:
    explicit IopArgs(int firstSlot) : slot_(firstSlot) {}
    static IopArgs fromVaList(uint32_t va) {
        IopArgs a(0);
        a.va_ = va;
        return a;
    }
    uint64_t nextInt(bool wide) override;
    double nextDouble() override;
    std::string readString(uint32_t addr) override { return mem::readString(addr); }

private:
    uint32_t slot(int i) const;
    int slot_;
    uint32_t va_ = 0;
};

std::string formatIop(uint32_t fmtAddr, int firstArg);

void initRegistry();

} // namespace ps2::iop::hle
