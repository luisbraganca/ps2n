// A small two-pass R5900 assembler that emits PS2-style ELF executables.
//
// It exists so tests and examples can be built without a PS2 toolchain. The
// syntax is a GNU-as subset: labels, numeric local labels (1: / 1b / 1f),
// %hi/%lo, .text/.rodata/.data/.bss, .word/.half/.byte/.dword/.float/.ascii/
// .asciz/.space/.align, .globl, .ent/.end (function symbols with sizes),
// .type, .size, .equ/.set/=, and the usual pseudo-instructions (nop, move, li,
// la, b, bal, beqz, bnez, negu, not). It never reorders or fills delay slots.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace ps2uc {

struct AsmOptions {
    uint32_t textBase = 0x00100000;   // where .text is linked
    bool symbols = true;              // emit .symtab (set false to test stripped ELFs)
    std::string entrySymbol;          // default: _start, __start, or the start of .text
};

struct AsmSymbol {
    std::string name;
    uint32_t value = 0;
    uint32_t size = 0;
    bool function = false;
    bool object = false;
    bool global = false;
    int section = -1;                 // index into Assembler::kSectionNames, -1 = absolute
};

class Assembler {
public:
    explicit Assembler(AsmOptions options = {});

    // Throws ps2uc::Error ("file:line: message") on the first error.
    void assemble(std::string_view source, std::string_view fileName = "<input>");
    std::vector<uint8_t> buildElf() const;

    const std::map<std::string, AsmSymbol>& symbols() const { return symbols_; }
    uint32_t entry() const;

    static constexpr const char* kSectionNames[4] = {".text", ".rodata", ".data", ".bss"};

private:
    struct Statement;
    struct Section {
        std::vector<uint8_t> bytes;
        uint32_t base = 0;
        uint32_t size = 0;            // .bss has size without bytes
        uint32_t align = 16;
    };

    AsmOptions opt_;
    Section sections_[4];
    std::map<std::string, AsmSymbol> symbols_;
    std::map<std::string, int64_t> equates_;

    friend class AsmImpl;
};

// Encodes a single instruction line (no labels / directives) at addr.
// Returns false and fills error on failure. Pseudo-instructions that expand
// to more than one word (la, li with a 32-bit value) are rejected here.
bool assembleInstruction(std::string_view line, uint32_t addr, uint32_t& out, std::string* error = nullptr);

} // namespace ps2uc
