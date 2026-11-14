// SPDX-License-Identifier: GPL-3.0-or-later
//
// See iop_codegen.h.

#include "iop_codegen.h"

#include "ps2n/util.h"

#include <algorithm>
#include <sstream>

namespace ps2n::recomp {

// ---- capture files ---------------------------------------------------------------------------

void IopCapture::add(const IopCapture& other) { blocks.insert(blocks.end(), other.blocks.begin(), other.blocks.end()); }

void IopCapture::normalize() {
    const auto before = [](const Block& x, const Block& y) {
        if (x.key.a != y.key.a) return x.key.a < y.key.a;
        if (x.key.b != y.key.b) return x.key.b < y.key.b;
        return x.pc < y.pc;
    };
    const auto same = [](const Block& x, const Block& y) { return x.key == y.key; };
    std::stable_sort(blocks.begin(), blocks.end(), before);
    blocks.erase(std::unique(blocks.begin(), blocks.end(), same), blocks.end());
}

IopCapture parseIopCapture(const std::string& text, const std::string& fileName) {
    IopCapture capture;
    bool inBlock = false, inList = false;

    std::istringstream in(text);
    std::string line;
    int lineNo = 0;
    const auto fail = [&](const std::string& what) -> Error { return Error(fileName + ":" + std::to_string(lineNo) + ": " + what); };
    const auto number = [&](std::string_view token) -> uint32_t {
        const auto v = parseInt(token);
        if (!v || *v < 0 || *v > 0xFFFFFFFFLL) throw fail("'" + std::string(token) + "' is not a 32-bit number");
        return static_cast<uint32_t>(*v);
    };
    const auto endBlock = [&]() {
        if (!inBlock) return;
        IopCapture::Block& b = capture.blocks.back();
        if (b.words.empty()) throw fail("a [[block]] needs its words");
        if (b.words.size() > r3000a::kMaxBlockWords) throw fail("a [[block]] of more words than a block can have");
        b.key = r3000a::key(b.words.data(), static_cast<uint32_t>(b.words.size()));
    };

    while (std::getline(in, line)) {
        ++lineNo;
        if (auto hash = line.find('#'); hash != std::string::npos) line.resize(hash);
        std::string_view rest = trim(line);
        if (rest.empty()) continue;

        if (!inList) {
            if (rest == "[[block]]") {
                endBlock();
                capture.blocks.emplace_back();
                inBlock = true;
                continue;
            }
            const size_t eq = rest.find('=');
            if (eq == std::string_view::npos) throw fail("expected 'name = value' or [[block]]");
            const std::string_view key = trim(rest.substr(0, eq));
            rest = trim(rest.substr(eq + 1));
            if (!inBlock) throw fail("'" + std::string(key) + "' outside a [[block]]");
            if (key == "pc") {
                capture.blocks.back().pc = number(rest);
                continue;
            }
            if (key == "key") continue;   // made again from the words
            if (key != "words") throw fail("unexpected '" + std::string(key) + "'");
            if (rest.empty() || rest[0] != '[') throw fail("expected '[' after 'words ='");
            rest.remove_prefix(1);
            inList = true;
        }

        // Inside the list: numbers, commas, and the closing bracket.
        while (!rest.empty()) {
            rest = trim(rest);
            if (rest.empty()) break;
            if (rest[0] == ']') {
                inList = false;
                if (!trim(rest.substr(1)).empty()) throw fail("text after ']'");
                break;
            }
            if (rest[0] == ',') {
                rest.remove_prefix(1);
                continue;
            }
            const size_t cut = rest.find_first_of(", \t]");
            capture.blocks.back().words.push_back(number(rest.substr(0, cut)));
            if (cut == std::string_view::npos) break;
            rest.remove_prefix(cut);
        }
    }
    if (inList) throw fail("a list is not closed");
    endBlock();
    return capture;
}

IopCapture loadIopCaptureFile(const std::filesystem::path& file) { return parseIopCapture(readTextFile(file), file.string()); }

std::string writeIopCapture(const IopCapture& capture, const std::string& comment) {
    std::string out;
    if (!comment.empty()) out += "# " + comment + "\n";
    out += "# Blocks of IOP code, each as PCSX2's IOP recompiler formed it: the address it was first met\n"
           "# at, and its instructions. Give this file to the recompiler (ps2n-recomp iop) to have them\n"
           "# compiled.\n";
    for (const IopCapture::Block& b : capture.blocks) {
        out += strprintf("\n[[block]]\npc = 0x%08X\nwords = [", b.pc);
        for (size_t i = 0; i < b.words.size(); ++i) out += strprintf("%s0x%08X", i == 0 ? "" : (i % 8 == 0 ? ",\n\t" : ", "), b.words[i]);
        out += "]\n";
    }
    return out;
}

// ---- generation --------------------------------------------------------------------------------

namespace {

using r3000a::Op;

std::string fnName(const IopCapture::Block& b) {
    return strprintf("iopb_%016llX%016llX", static_cast<unsigned long long>(b.key.a), static_cast<unsigned long long>(b.key.b));
}

// One block's function, written from its first instruction to its last. The
// IOP's general registers, HI and LO are locals of the function: taken from
// the core the first time the block reads them, put back before anything
// else can look.
class Emitter {
public:
    std::string out;

    // The value of a register, as an expression. $zero is a literal.
    std::string use(uint32_t reg) {
        if (reg == 0) return "0u";
        if (!declared_[reg]) {
            out += strprintf("\tuint32_t %s = r[%u];\n", name(reg).c_str(), reg);
            declared_[reg] = valid_[reg] = true;
        } else if (!valid_[reg]) {
            out += strprintf("\t%s = r[%u];\n", name(reg).c_str(), reg);
            valid_[reg] = true;
        }
        return name(reg);
    }

    // A register gets a value. Writes to $zero are dropped: the callers have
    // seen to what the value's reckoning does besides.
    void def(uint32_t reg, const std::string& value) {
        if (reg == 0) return;
        if (!declared_[reg]) {
            out += strprintf("\tuint32_t %s = %s;\n", name(reg).c_str(), value.c_str());
            declared_[reg] = true;
        } else {
            out += strprintf("\t%s = %s;\n", name(reg).c_str(), value.c_str());
        }
        valid_[reg] = dirty_[reg] = true;
    }

    // What the block wrote goes back to the core.
    void flush() {
        out += flushText("\t");
        for (bool& d : dirty_) d = false;
    }

    // The core's registers may have changed: the locals are taken again
    // where they are next read. Nothing may be waiting to be put back.
    void invalidate() {
        for (bool& v : valid_) v = false;
    }

    // An import's function of the core's reads and writes the IOP's
    // registers where the core has them: inside the branch that calls it,
    // what the block wrote is put back first and the locals are taken again
    // after. Outside that branch nothing has changed.
    std::string flushText(const char* indent) const {
        std::string text;
        for (uint32_t reg = 1; reg < 34; ++reg)
            if (dirty_[reg]) text += strprintf("%sr[%u] = %s;\n", indent, reg, name(reg).c_str());
        return text;
    }
    std::string reloadText(const char* indent) const {
        std::string text;
        for (uint32_t reg = 1; reg < 34; ++reg)
            if (declared_[reg] && valid_[reg]) text += strprintf("%s%s = r[%u];\n", indent, name(reg).c_str(), reg);
        return text;
    }

private:
    static std::string name(uint32_t reg) { return reg == 32 ? "hi" : reg == 33 ? "lo" : strprintf("g%u", reg); }

    bool declared_[34] = {};
    bool valid_[34] = {};
    bool dirty_[34] = {};
};

std::string hex(uint32_t v) { return strprintf("0x%Xu", v); }

// The address of the instruction `offset` bytes into the block.
std::string at(uint32_t offset) { return offset ? "pc + " + hex(offset) : std::string("pc"); }

// One instruction that is not a branch or jump, `offset` bytes into the block
// and number `index` in it.
void instruction(Emitter& e, uint32_t code, uint32_t offset, uint32_t index, uint32_t& soFar, uint32_t& import) {
    const Op op = r3000a::decode(code);
    const uint32_t s = r3000a::rs(code), t = r3000a::rt(code), d = r3000a::rd(code);
    const std::string imm = strprintf("%d", r3000a::imm(code));
    const std::string immu = hex(r3000a::immu(code));
    soFar += r3000a::cycles(op);

    // rs + the immediate: the address of a load or a store.
    const auto address = [&]() {
        const std::string base = e.use(s);
        const uint32_t add = static_cast<uint32_t>(r3000a::imm(code));
        if (s == 0) return hex(add);
        return add ? base + " + " + hex(add) : base;
    };
    const auto immediate = [&](const char* fn, const std::string& operand) {
        if (!t) return;
        const std::string a = e.use(s);
        e.def(t, std::string("val::") + fn + "(" + a + ", " + operand + ")");
    };
    const auto three = [&](const char* fn) {
        if (!d) return;
        const std::string a = e.use(s), b = e.use(t);
        e.def(d, std::string("val::") + fn + "(" + a + ", " + b + ")");
    };
    const auto shift = [&](const char* fn, const std::string& by) {
        if (!d) return;
        const std::string a = e.use(t);
        e.def(d, std::string("val::") + fn + "(" + a + ", " + by + ")");
    };
    const auto product = [&](const char* fn) {
        const std::string a = e.use(s), b = e.use(t);
        e.out += strprintf("\tconst uint64_t v%u = val::%s(%s, %s);\n", index, fn, a.c_str(), b.c_str());
        e.def(33, strprintf("static_cast<uint32_t>(v%u)", index));
        e.def(32, strprintf("static_cast<uint32_t>(v%u >> 32)", index));
    };

    switch (op) {
        case Op::ADDIU: immediate("ADDIU", imm); break;
        case Op::SLTI: immediate("SLTI", imm); break;
        case Op::SLTIU: immediate("SLTIU", imm); break;
        case Op::ANDI: immediate("ANDI", immu); break;
        case Op::ORI: immediate("ORI", immu); break;
        case Op::XORI: immediate("XORI", immu); break;
        case Op::LUI:
            if (t) e.def(t, "val::LUI(" + immu + ")");
            break;
        case Op::ADDU: three("ADDU"); break;
        case Op::SUBU: three("SUBU"); break;
        case Op::AND: three("AND"); break;
        case Op::OR: three("OR"); break;
        case Op::XOR: three("XOR"); break;
        case Op::NOR: three("NOR"); break;
        case Op::SLT: three("SLT"); break;
        case Op::SLTU: three("SLTU"); break;
        case Op::SLL: shift("SLL", hex(r3000a::sa(code))); break;
        case Op::SRL: shift("SRL", hex(r3000a::sa(code))); break;
        case Op::SRA: shift("SRA", hex(r3000a::sa(code))); break;
        case Op::SLLV:
        case Op::SRLV:
        case Op::SRAV:
            if (d) {
                const std::string by = e.use(s);
                shift(op == Op::SLLV ? "SLL" : op == Op::SRLV ? "SRL" : "SRA", by);
            }
            break;
        case Op::MULT: product("MULT"); break;
        case Op::MULTU: product("MULTU"); break;
        case Op::DIV: product("DIV"); break;
        case Op::DIVU: product("DIVU"); break;
        case Op::MFHI:
            if (d) e.def(d, e.use(32));
            break;
        case Op::MTHI: e.def(32, e.use(s)); break;
        case Op::MFLO:
            if (d) e.def(d, e.use(33));
            break;
        case Op::MTLO: e.def(33, e.use(s)); break;
        case Op::LB:
        case Op::LBU:
        case Op::LH:
        case Op::LHU:
        case Op::LW: {
            const char* fn = op == Op::LB ? "LoadS8" : op == Op::LBU ? "Load8" : op == Op::LH ? "LoadS16" : op == Op::LHU ? "Load16" : "Load32";
            const unsigned bits = (op == Op::LB || op == Op::LBU) ? 8 : (op == Op::LH || op == Op::LHU) ? 16 : 32;
            const std::string a = address();
            if (t) e.def(t, std::string(fn) + "(" + a + ")");
            else e.out += strprintf("\tLoadNowhere(%s, %uu);\n", a.c_str(), bits);
            break;
        }
        case Op::LWL:
        case Op::LWR: {
            const std::string a = address();
            const std::string old = t ? e.use(t) : std::string();
            e.out += strprintf("\tconst uint32_t a%u = %s;\n\tconst uint32_t m%u = Read32(a%u & ~3u);\n", index, a.c_str(), index, index);
            if (t) e.def(t, strprintf("val::%s(%s, m%u, a%u)", op == Op::LWL ? "LWL" : "LWR", old.c_str(), index, index));
            else e.out += strprintf("\tstatic_cast<void>(m%u);\n", index);
            break;
        }
        case Op::SB:
        case Op::SH:
        case Op::SW: {
            const std::string a = address();
            const std::string v = e.use(t);
            e.out += strprintf("\t%s(%s, %s);\n", op == Op::SB ? "Store8" : op == Op::SH ? "Store16" : "Store32", a.c_str(), v.c_str());
            break;
        }
        case Op::SWL:
        case Op::SWR: {
            const std::string a = address();
            const std::string v = e.use(t);
            e.out += strprintf("\tconst uint32_t a%u = %s;\n\tStore32(a%u & ~3u, val::%s(%s, Read32(a%u & ~3u), a%u));\n", index, a.c_str(), index,
                               op == Op::SWL ? "SWL" : "SWR", v.c_str(), index, index);
            break;
        }
        case Op::MFC0:
            if (t) e.def(t, strprintf("psxRegs.CP0.r[%u]", d));
            break;
        case Op::MTC0: {
            const std::string v = e.use(t);
            e.out += strprintf("\tpsxRegs.CP0.r[%u] = %s;\n", d, v.c_str());
            break;
        }
        case Op::RFE: e.out += "\tRFE();\n"; break;
        case Op::SYSCALL:
        case Op::BREAK:
            // The exception's handler reads the IOP's registers where the core
            // has them.
            e.flush();
            e.out += strprintf("\tif (Exception(%s, %s, %s, %uu)) [[unlikely]]\n\t\treturn true;\n", hex(code).c_str(), at(offset).c_str(),
                               op == Op::SYSCALL ? "0x20u" : "0x24u", soFar);
            break;
        case Op::GTE:
            // The core's function reads and writes them there too.
            e.flush();
            e.out += "\tGte(" + hex(code) + ");\n";
            e.invalidate();
            break;
        case Op::IMPORT:
            e.out += strprintf("\tif (b->imports[%u]) [[unlikely]]\n\t{\n", import);
            e.out += e.flushText("\t\t");
            e.out += strprintf("\t\tif (Import(b, %uu, %s, %s))\n\t\t\treturn true;\n", import, hex(code).c_str(), at(offset).c_str());
            e.out += e.reloadText("\t\t");
            e.out += "\t}\n";
            ++import;
            break;
        default:
            // An encoding PCSX2's IOP recompiler has no instruction for:
            // nothing. (A branch is not given to this function.)
            e.out += "\t// " + hex(code) + ": no instruction\n";
            break;
    }
}

std::string emitBlock(const IopCapture::Block& b, const r3000a::Shape& shape, const IopGenerateOptions& options, bool split) {
    const uint32_t count = static_cast<uint32_t>(b.words.size());
    std::string head;
    if (options.comments) {
        head += strprintf("// first met at %08X: %u instructions, %u cycles%s\n", b.pc, count, shape.cycles,
                          shape.branch ? "" : "; cut short, it runs on into the next block");
        for (uint32_t i = 0; i < count; ++i) {
            if (i % 8 == 0) head += i ? "\n//  " : "//  ";
            head += strprintf(" %08X", b.words[i]);
        }
        head += "\n";
    }
    head += strprintf("%sbool PS2N_IOP_CC %s(Block* b)\n{\n\tPS2N_IOP_ENTER();\n", split ? "" : "static ", fnName(b).c_str());

    Emitter e;
    const uint32_t body = shape.branch ? count - 2 : count;
    uint32_t soFar = 0, import = 0;
    for (uint32_t i = 0; i < body; ++i) instruction(e, b.words[i], i * 4, i, soFar, import);

    if (!shape.branch) {
        e.flush();
        e.out += strprintf("\tPS2N_IOP_RUN_ON(%s, %uu);\n", at(count * 4).c_str(), shape.cycles);
        return head + e.out + "}\n\n";
    }

    const uint32_t code = b.words[body], slot = b.words[body + 1];
    const uint32_t off = body * 4;
    const Op op = r3000a::decode(code);
    const uint32_t s = r3000a::rs(code), t = r3000a::rt(code), d = r3000a::rd(code);

    // A block that may be a wait loop: everything in it but its branch is a
    // NOP, and the branch says where it goes. Whether it is one, the record
    // says when the block runs (Block::flags).
    bool nops = true;
    for (uint32_t i = 0; i < count; ++i) nops = nops && (i == body || b.words[i] == 0);
    const char* leave = nops ? "PS2N_IOP_LEAVE_LOOP" : "PS2N_IOP_LEAVE";

    switch (op) {
        case Op::J:
        case Op::JAL: {
            if (op == Op::JAL) e.def(31, at(off + 8));
            instruction(e, slot, off + 4, body + 1, soFar, import);
            e.flush();
            e.out += strprintf("\t%s(1u, ((%s) & 0xF0000000u) | %s, %uu);\n", leave, at(off + 4).c_str(), hex((code & 0x03FFFFFFu) << 2).c_str(),
                               shape.cycles);
            break;
        }
        case Op::JR:
        case Op::JALR: {
            // The order PCSX2's recompiler keeps (r3000a::swapsDelaySlot).
            const bool swap = op == Op::JR ? r3000a::swapsDelaySlot(s, 0, 0, slot) : (d != s && r3000a::swapsDelaySlot(s, 0, d, slot));
            const auto link = [&]() {
                if (op == Op::JALR && d) e.def(d, at(off + 8));
            };
            const auto target = [&]() {
                const std::string to = e.use(s);
                e.out += "\tconst uint32_t target = " + to + ";\n";
            };
            if (swap) {
                instruction(e, slot, off + 4, body + 1, soFar, import);
                link();
                target();
            } else {
                target();
                link();
                instruction(e, slot, off + 4, body + 1, soFar, import);
            }
            e.flush();
            e.out += strprintf("\tPS2N_IOP_LEAVE_ANYWHERE(target, %uu);\n", shape.cycles);
            break;
        }
        default: {
            // The conditional branches; the two that link, link first.
            if (op == Op::BLTZAL || op == Op::BGEZAL) e.def(31, at(off + 8));
            std::string taken;
            if ((op == Op::BEQ || op == Op::BNE) && s == t) {
                // A register against itself: always, or never.
                taken = op == Op::BEQ ? "true" : "false";
            } else {
                const std::string a = e.use(s);
                switch (op) {
                    case Op::BEQ: taken = a + " == " + e.use(t); break;
                    case Op::BNE: taken = a + " != " + e.use(t); break;
                    case Op::BLEZ: taken = "static_cast<int32_t>(" + a + ") <= 0"; break;
                    case Op::BGTZ: taken = "static_cast<int32_t>(" + a + ") > 0"; break;
                    case Op::BLTZ:
                    case Op::BLTZAL: taken = "static_cast<int32_t>(" + a + ") < 0"; break;
                    default: taken = "static_cast<int32_t>(" + a + ") >= 0"; break;
                }
            }
            e.out += "\tconst bool taken = " + taken + ";\n";
            instruction(e, slot, off + 4, body + 1, soFar, import);
            e.flush();
            const uint32_t to = off + 4 + static_cast<uint32_t>(r3000a::imm(code) * 4);
            e.out += strprintf("\tif (taken)\n\t\t%s(1u, %s, %uu);\n\tPS2N_IOP_LEAVE(0u, %s, %uu);\n", leave, at(to).c_str(), shape.cycles,
                               at(off + 8).c_str(), shape.cycles);
            break;
        }
    }
    return head + e.out + "}\n\n";
}

} // namespace

std::vector<GeneratedFile> generateIopFiles(const IopCapture& capture, const IopGenerateOptions& options, IopGenerateStats* statsOut) {
    if (options.id.empty() || identifierFor(options.id) != options.id) throw Error("'" + options.id + "' cannot name a generated namespace");
    if (options.parts < 1 || options.parts > 999) throw Error("the number of parts must be between 1 and 999");
    const bool split = options.parts > 1;
    if (split && options.headerName.empty()) throw Error("code split into parts needs a name for its header");

    IopGenerateStats stats;
    const std::string banner = strprintf("// Generated by %s from blocks of IOP code that runs captured. Do not edit.\n", kGeneratorName) +
                               "//\n// This file is derived from the programs whose code those blocks were (a BIOS's modules, a\n"
                               "// game's) and is under their terms, not PS2Native's. It may never leave the build tree.\n\n";
    const std::string open = "namespace ps2n_iop_image_" + options.id + "\n{\n\nusing namespace ps2n::iop;\n\n";
    const std::string close = "} // namespace ps2n_iop_image_" + options.id + "\n";

    // The blocks the provider carries out: one it refuses never runs, so
    // nothing is compiled for it.
    struct Kept {
        const IopCapture::Block* block;
        r3000a::Shape shape;
    };
    std::vector<Kept> kept;
    uint64_t wordsInAll = 0;
    for (const IopCapture::Block& b : capture.blocks) {
        const r3000a::Shape shape = r3000a::shape(b.words.data(), static_cast<uint32_t>(b.words.size()));
        if (!shape.valid || shape.refused) {
            ++stats.skipped;
            continue;
        }
        kept.push_back({&b, shape});
        wordsInAll += b.words.size() + 2;
    }

    // The functions of each part: in the order of their keys, and of about
    // the same number of instructions each.
    uint64_t wordsBefore = 0;
    std::vector<std::string> bodies(options.parts);
    std::string declarations;
    std::string table = "static const BlockInfo blocks[] = {\n";
    for (const Kept& k : kept) {
        const IopCapture::Block& b = *k.block;
        std::string& out = bodies[std::min<uint64_t>(options.parts - 1, wordsBefore * options.parts / std::max<uint64_t>(wordsInAll, 1))];
        wordsBefore += b.words.size() + 2;
        out += emitBlock(b, k.shape, options, split);
        declarations += strprintf("%sbool PS2N_IOP_CC %s(Block* b);\n", split ? "" : "static ", fnName(b).c_str());
        table += strprintf("\t{0x%016llXull, 0x%016llXull, &%s},\n", static_cast<unsigned long long>(b.key.a),
                           static_cast<unsigned long long>(b.key.b), fnName(b).c_str());
        ++stats.blocks;
        stats.instructions += b.words.size();
    }
    // The last element is null, so that the array is never empty.
    table += "\t{0, 0, nullptr},\n};\n\n";

    const std::string tables = table + "extern const ImageInfo image;\nconst ImageInfo image = {\n\t\"" + options.id + "\",\n\tblocks,\n\t" +
                               std::to_string(kept.size()) + ",\n\t\"" + kGeneratorName + "\",\n};\n\n";

    std::vector<GeneratedFile> files;
    if (!split) {
        files.push_back({".cpp", banner + "#include \"ps2n/iop_recomp.h\"\n\n" + open + declarations + "\n" + bodies[0] + tables + close});
    } else {
        const std::vector<std::string> suffixes = generatedSuffixes(options.parts);
        files.push_back({suffixes[0], banner + "#include \"" + options.headerName + "\"\n\n" + open + tables + close});
        files.push_back({suffixes[1], banner + "#pragma once\n\n#include \"ps2n/iop_recomp.h\"\n\n" + open + declarations + "\n" + close});
        for (unsigned i = 0; i < options.parts; ++i)
            files.push_back({suffixes[2 + i], banner + "#include \"" + options.headerName + "\"\n\n" + open + bodies[i] + close});
    }
    if (statsOut) *statsOut = stats;
    return files;
}

} // namespace ps2n::recomp
