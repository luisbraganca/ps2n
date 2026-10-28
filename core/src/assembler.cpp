#include "ps2uc/assembler.h"

#include "ps2uc/elf.h"
#include "ps2uc/r5900.h"
#include "ps2uc/util.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <optional>
#include <set>
#include <unordered_map>

namespace ps2uc {

using r5900::Op;

namespace {

// ---------------------------------------------------------------------------
// Mnemonic tables built from r5900_ops.inc
// ---------------------------------------------------------------------------
struct MnemonicTables {
    std::unordered_map<std::string, Op> exact;     // ops without a broadcast suffix
    std::unordered_map<std::string, Op> bcBase;    // "vadd" -> VADDbc, ...
    MnemonicTables() {
        for (size_t i = 1; i < static_cast<size_t>(Op::Count); ++i) {
            const auto op = static_cast<Op>(i);
            const auto& oi = r5900::info(op);
            if (oi.flags & r5900::F_VUBC) bcBase.emplace(oi.mnemonic, op);
            else exact.emplace(oi.mnemonic, op);
        }
    }
};
const MnemonicTables& tables() {
    static const MnemonicTables t;
    return t;
}

std::optional<uint32_t> gprIndex(std::string_view t) {
    if (!t.empty() && t[0] == '$') t.remove_prefix(1);
    if (t.empty()) return std::nullopt;
    if (std::isdigit(static_cast<unsigned char>(t[0]))) {
        auto v = parseInt(t);
        if (v && *v >= 0 && *v < 32) return static_cast<uint32_t>(*v);
        return std::nullopt;
    }
    const std::string n = toLower(t);
    if (n == "s8") return 30;
    for (uint32_t r = 0; r < 32; ++r)
        if (n == r5900::gprName(r)) return r;
    return std::nullopt;
}

std::optional<uint32_t> prefixedIndex(std::string_view t, std::string_view prefix, uint32_t limit) {
    if (!t.empty() && t[0] == '$') t.remove_prefix(1);
    const std::string n = toLower(t);
    if (!startsWith(n, prefix)) return std::nullopt;
    auto v = parseInt(std::string_view(n).substr(prefix.size()));
    if (!v || *v < 0 || *v >= limit) return std::nullopt;
    return static_cast<uint32_t>(*v);
}

int componentIndex(char c) {
    switch (std::tolower(static_cast<unsigned char>(c))) {
    case 'x': return 0;
    case 'y': return 1;
    case 'z': return 2;
    case 'w': return 3;
    default: return -1;
    }
}

// "$vf3" / "$vf3x" -> (3, component or -1)
std::optional<std::pair<uint32_t, int>> vfOperand(std::string_view t) {
    if (!t.empty() && t[0] == '$') t.remove_prefix(1);
    std::string n = toLower(t);
    if (!startsWith(n, "vf")) return std::nullopt;
    int comp = -1;
    if (!n.empty() && componentIndex(n.back()) >= 0 && !std::isdigit(static_cast<unsigned char>(n.back()))) {
        comp = componentIndex(n.back());
        n.pop_back();
    }
    auto v = parseInt(std::string_view(n).substr(2));
    if (!v || *v < 0 || *v > 31) return std::nullopt;
    return std::make_pair(static_cast<uint32_t>(*v), comp);
}

// Splits "a, b(c), d" at top-level commas.
std::vector<std::string> splitOperands(std::string_view s) {
    std::vector<std::string> out;
    int depth = 0;
    bool inStr = false;
    std::string cur;
    for (char c : s) {
        if (c == '"') inStr = !inStr;
        if (!inStr) {
            if (c == '(') ++depth;
            if (c == ')') --depth;
            if (c == ',' && depth == 0) {
                out.emplace_back(trim(cur));
                cur.clear();
                continue;
            }
        }
        cur.push_back(c);
    }
    if (!trim(cur).empty() || !out.empty()) out.emplace_back(trim(cur));
    return out;
}

} // namespace

// ---------------------------------------------------------------------------
// Assembler implementation
// ---------------------------------------------------------------------------
class AsmImpl {
public:
    AsmImpl(Assembler& a, std::string_view file) : A(a), file_(file) {}

    void run(std::string_view source);

    // Encoding of a single instruction; used by assembleInstruction().
    bool encodeOne(std::string_view mnemonic, std::string_view operands, uint32_t pc, std::vector<uint32_t>& words);
    void encodeOneForTest(std::string_view mnemonic, std::string_view operands, uint32_t pc, std::vector<uint32_t>& words) {
        pass_ = 2;
        encodeOne(mnemonic, operands, pc, words);
    }

private:
    struct Line {
        int number;
        std::string text;
    };
    struct LabelDef {
        size_t stmt;
        int section;
        uint32_t offset;
    };

    Assembler& A;
    std::string file_;
    int pass_ = 1;
    int lineNo_ = 0;
    size_t stmtIndex_ = 0;
    int section_ = 0;
    uint32_t sectionSize_[4] = {0, 0, 0, 0};
    std::map<std::string, std::pair<int, uint32_t>> labels_;   // name -> (section, offset)
    std::map<std::string, std::vector<LabelDef>> numeric_;    // "1" -> definitions in order
    bool unresolved_ = false;                                 // current expression used an unknown symbol

    [[noreturn]] void fail(const std::string& msg) const {
        throw Error(strprintf("%s:%d: %s", file_.c_str(), lineNo_, msg.c_str()));
    }

    uint32_t here() const { return A.sections_[section_].base + sectionSize_[section_]; }

    void emitBytes(const void* p, size_t n) {
        auto& sec = A.sections_[section_];
        if (section_ == 3) {
            // .bss: only zero-fill is allowed
            const auto* b = static_cast<const uint8_t*>(p);
            for (size_t i = 0; i < n; ++i)
                if (b[i] != 0) fail("initialised data in .bss");
        } else if (pass_ == 2) {
            const auto* b = static_cast<const uint8_t*>(p);
            sec.bytes.insert(sec.bytes.end(), b, b + n);
        }
        sectionSize_[section_] += static_cast<uint32_t>(n);
    }
    void emit32(uint32_t w) {
        uint8_t b[4] = {static_cast<uint8_t>(w), static_cast<uint8_t>(w >> 8), static_cast<uint8_t>(w >> 16),
                        static_cast<uint8_t>(w >> 24)};
        emitBytes(b, 4);
    }
    void alignTo(uint32_t a) {
        if (a == 0) return;
        A.sections_[section_].align = std::max(A.sections_[section_].align, a);
        while (sectionSize_[section_] % a) {
            uint8_t z = 0;
            emitBytes(&z, 1);
        }
    }

    // ---- expressions ------------------------------------------------------
    // Assembler arithmetic (numbers, symbols, + - * / % << >> & | ^ ~, %hi/%lo).
    // A plain recursive-descent parser: nothing is ever executed.
    struct ExprParser {
        AsmImpl& self;
        std::string_view s;
        size_t i = 0;

        void ws() {
            while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
        }
        bool eat(std::string_view tok) {
            ws();
            if (s.substr(i, tok.size()) == tok) {
                i += tok.size();
                return true;
            }
            return false;
        }
        int64_t primary() {
            ws();
            if (i >= s.size()) self.fail("expected expression");
            if (eat("(")) {
                int64_t v = orExpr();
                if (!eat(")")) self.fail("expected ')'");
                return v;
            }
            if (eat("-")) return -primary();
            if (eat("~")) return ~primary();
            if (eat("+")) return primary();
            if (eat("%hi(")) {
                int64_t v = orExpr();
                if (!eat(")")) self.fail("expected ')'");
                return ((v + 0x8000) >> 16) & 0xFFFF;
            }
            if (eat("%lo(")) {
                int64_t v = orExpr();
                if (!eat(")")) self.fail("expected ')'");
                return static_cast<int16_t>(v & 0xFFFF);
            }
            if (s[i] == '\'') {
                if (i + 2 < s.size() && s[i + 2] == '\'') {
                    int64_t v = static_cast<unsigned char>(s[i + 1]);
                    i += 3;
                    return v;
                }
                self.fail("bad character literal");
            }
            if (std::isdigit(static_cast<unsigned char>(s[i]))) {
                size_t j = i;
                while (j < s.size() && (std::isalnum(static_cast<unsigned char>(s[j])) || s[j] == '_')) ++j;
                std::string_view tok = s.substr(i, j - i);
                i = j;
                // numeric local label reference: 1b / 1f
                if (tok.size() >= 2 && (tok.back() == 'b' || tok.back() == 'f') &&
                    std::all_of(tok.begin(), tok.end() - 1, [](char c) { return std::isdigit(static_cast<unsigned char>(c)); }) &&
                    !(tok.size() > 2 && tok[0] == '0' && (tok[1] == 'x' || tok[1] == 'X')))
                    return self.numericRef(std::string(tok.substr(0, tok.size() - 1)), tok.back() == 'f');
                auto v = parseInt(tok);
                if (!v) self.fail("bad number '" + std::string(tok) + "'");
                return *v;
            }
            if (s[i] == '.' && (i + 1 >= s.size() || !(std::isalnum(static_cast<unsigned char>(s[i + 1])) || s[i + 1] == '_'))) {
                ++i;
                return self.here();
            }
            if (std::isalpha(static_cast<unsigned char>(s[i])) || s[i] == '_' || s[i] == '.') {
                size_t j = i;
                while (j < s.size() && (std::isalnum(static_cast<unsigned char>(s[j])) || s[j] == '_' || s[j] == '.' || s[j] == '$')) ++j;
                std::string name(s.substr(i, j - i));
                i = j;
                return self.symbolValue(name);
            }
            self.fail("unexpected '" + std::string(s.substr(i)) + "' in expression");
        }
        int64_t mul() {
            int64_t v = primary();
            for (;;) {
                if (eat("*")) v *= primary();
                else if (eat("/")) {
                    int64_t d = primary();
                    if (d == 0) self.fail("division by zero");
                    v /= d;
                } else if (eat("%")) {
                    int64_t d = primary();
                    if (d == 0) self.fail("division by zero");
                    v %= d;
                } else return v;
            }
        }
        int64_t add() {
            int64_t v = mul();
            for (;;) {
                if (eat("+")) v += mul();
                else if (eat("-")) v -= mul();
                else return v;
            }
        }
        int64_t shift() {
            int64_t v = add();
            for (;;) {
                if (eat("<<")) v = static_cast<int64_t>(static_cast<uint64_t>(v) << add());
                else if (eat(">>")) v >>= add();
                else return v;
            }
        }
        int64_t andExpr() {
            int64_t v = shift();
            while (eat("&")) v &= shift();
            return v;
        }
        int64_t xorExpr() {
            int64_t v = andExpr();
            while (eat("^")) v ^= andExpr();
            return v;
        }
        int64_t orExpr() {
            int64_t v = xorExpr();
            while (eat("|")) v |= xorExpr();
            return v;
        }
    };

    int64_t evalExpr(std::string_view s, bool* usedSymbols = nullptr) {
        const bool prev = unresolved_;
        unresolved_ = false;
        usedSymbol_ = false;
        ExprParser p{*this, trim(s)};
        int64_t v = p.orExpr();
        p.ws();
        if (p.i != p.s.size()) fail("junk after expression: '" + std::string(p.s.substr(p.i)) + "'");
        if (pass_ == 2 && unresolved_) fail("undefined symbol in '" + std::string(trim(s)) + "'");
        if (usedSymbols) *usedSymbols = usedSymbol_;
        unresolved_ = prev || unresolved_;
        return v;
    }
    bool usedSymbol_ = false;

    struct FuncMark {
        std::pair<int, uint32_t> start{0, 0};
        uint32_t end = 0;
        bool open = false;
    };
    std::map<std::string, FuncMark> functions_;              // .ent / .end
    std::set<std::string> globals_, functionTypes_, objectTypes_;
    std::map<std::string, uint32_t> sizes_;                  // .size

    int64_t symbolValue(const std::string& name) {
        usedSymbol_ = true;
        if (auto e = A.equates_.find(name); e != A.equates_.end()) return e->second;
        if (auto s = A.symbols_.find(name); s != A.symbols_.end() && s->second.section >= 0 && pass_ == 2)
            return s->second.value;
        if (auto l = labels_.find(name); l != labels_.end()) {
            if (pass_ == 2) return A.sections_[l->second.first].base + l->second.second;
            unresolved_ = true;   // address not final until pass 2
            return 0;
        }
        unresolved_ = true;
        return 0;
    }

    int64_t numericRef(const std::string& n, bool forward) {
        usedSymbol_ = true;
        auto it = numeric_.find(n);
        if (it != numeric_.end()) {
            const auto& defs = it->second;
            if (forward) {
                for (const auto& d : defs)
                    if (d.stmt > stmtIndex_) return pass_ == 2 ? A.sections_[d.section].base + d.offset : 0;
            } else {
                for (auto d = defs.rbegin(); d != defs.rend(); ++d)
                    if (d->stmt <= stmtIndex_) return pass_ == 2 ? A.sections_[d->section].base + d->offset : 0;
            }
        }
        unresolved_ = true;
        return 0;
    }

    // ---- statements -------------------------------------------------------
    void defineLabel(const std::string& name) {
        const bool numeric = std::all_of(name.begin(), name.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); });
        if (numeric) {
            if (pass_ == 1) numeric_[name].push_back({stmtIndex_, section_, sectionSize_[section_]});
            return;
        }
        if (pass_ == 1) {
            if (labels_.count(name) || A.equates_.count(name)) fail("symbol '" + name + "' defined twice");
            labels_[name] = {section_, sectionSize_[section_]};
        }
    }

    void directive(const std::string& name, std::string_view args);
    void instruction(const std::string& mnemonic, std::string_view operands);
    void statement(std::string_view text);
    std::vector<std::string> mergeStringArgs(std::string_view args);
    std::string parseString(std::string_view tok);

    void finalizePass1();
};

std::string AsmImpl::parseString(std::string_view tok) {
    tok = trim(tok);
    if (tok.size() < 2 || tok.front() != '"' || tok.back() != '"') fail("expected string literal");
    std::string out;
    for (size_t i = 1; i + 1 < tok.size(); ++i) {
        char c = tok[i];
        if (c == '\\' && i + 2 < tok.size()) {
            char e = tok[++i];
            switch (e) {
            case 'n': out.push_back('\n'); break;
            case 't': out.push_back('\t'); break;
            case 'r': out.push_back('\r'); break;
            case '0': out.push_back('\0'); break;
            case '\\': out.push_back('\\'); break;
            case '"': out.push_back('"'); break;
            case 'x': {
                int v = 0, k = 0;
                while (k < 2 && i + 1 < tok.size() - 1 && std::isxdigit(static_cast<unsigned char>(tok[i + 1]))) {
                    v = v * 16 + (std::isdigit(static_cast<unsigned char>(tok[i + 1])) ? tok[i + 1] - '0' : (std::tolower(tok[i + 1]) - 'a' + 10));
                    ++i;
                    ++k;
                }
                out.push_back(static_cast<char>(v));
                break;
            }
            default: out.push_back(e); break;
            }
        } else {
            out.push_back(c);
        }
    }
    return out;
}

void AsmImpl::directive(const std::string& name, std::string_view args) {
    const auto list = splitOperands(args);
    if (name == ".text") { section_ = 0; return; }
    if (name == ".rodata") { section_ = 1; return; }
    if (name == ".data") { section_ = 2; return; }
    if (name == ".bss") { section_ = 3; return; }
    if (name == ".section") {
        const std::string sec = list.empty() ? "" : list[0];
        if (startsWith(sec, ".text")) section_ = 0;
        else if (startsWith(sec, ".rodata")) section_ = 1;
        else if (startsWith(sec, ".data") || startsWith(sec, ".sdata")) section_ = 2;
        else if (startsWith(sec, ".bss") || startsWith(sec, ".sbss")) section_ = 3;
        else fail("unsupported section '" + sec + "'");
        return;
    }
    if (name == ".globl" || name == ".global") {
        for (const auto& s : list) globals_.insert(s);
        return;
    }
    if (name == ".set") {
        if (list.size() >= 2) {   // .set name, value
            if (pass_ == 1) A.equates_[list[0]] = evalExpr(list[1]);
        }
        return;                   // .set noreorder / noat / ... are no-ops here
    }
    if (name == ".equ") {
        if (list.size() != 2) fail(".equ needs name, value");
        if (pass_ == 1) A.equates_[list[0]] = evalExpr(list[1]);
        return;
    }
    if (name == ".ent") {
        if (list.empty()) fail(".ent needs a name");
        functions_[list[0]].start = {section_, sectionSize_[section_]};
        functions_[list[0]].open = true;
        return;
    }
    if (name == ".end") {
        if (list.empty()) return;
        auto it = functions_.find(list[0]);
        if (it == functions_.end() || !it->second.open) fail(".end without matching .ent for " + list[0]);
        it->second.end = sectionSize_[section_];
        it->second.open = false;
        return;
    }
    if (name == ".type") {
        if (list.size() >= 2) {
            if (list[1] == "@function" || list[1] == "%function") functionTypes_.insert(list[0]);
            else if (list[1] == "@object" || list[1] == "%object") objectTypes_.insert(list[0]);
        }
        return;
    }
    if (name == ".size") {
        if (list.size() == 2 && pass_ == 2) sizes_[list[0]] = static_cast<uint32_t>(evalExpr(list[1]));
        return;
    }
    if (name == ".align" || name == ".p2align") {
        alignTo(1u << static_cast<uint32_t>(evalExpr(list.at(0))));
        return;
    }
    if (name == ".balign") {
        alignTo(static_cast<uint32_t>(evalExpr(list.at(0))));
        return;
    }
    if (name == ".word" || name == ".long" || name == ".int") {
        for (const auto& e : list) emit32(static_cast<uint32_t>(evalExpr(e)));
        return;
    }
    if (name == ".half" || name == ".hword" || name == ".short") {
        for (const auto& e : list) {
            const auto v = static_cast<uint16_t>(evalExpr(e));
            uint8_t b[2] = {static_cast<uint8_t>(v), static_cast<uint8_t>(v >> 8)};
            emitBytes(b, 2);
        }
        return;
    }
    if (name == ".byte") {
        for (const auto& e : list) {
            const auto v = static_cast<uint8_t>(evalExpr(e));
            emitBytes(&v, 1);
        }
        return;
    }
    if (name == ".dword" || name == ".quad") {
        for (const auto& e : list) {
            const auto v = static_cast<uint64_t>(evalExpr(e));
            emit32(static_cast<uint32_t>(v));
            emit32(static_cast<uint32_t>(v >> 32));
        }
        return;
    }
    if (name == ".float" || name == ".single") {
        for (const auto& e : list) {
            const float f = std::stof(std::string(trim(e)));
            uint32_t w;
            std::memcpy(&w, &f, 4);
            emit32(w);
        }
        return;
    }
    if (name == ".ascii" || name == ".asciz" || name == ".string") {
        for (const auto& e : list) {
            const std::string s = parseString(e);
            emitBytes(s.data(), s.size());
            if (name != ".ascii") {
                uint8_t z = 0;
                emitBytes(&z, 1);
            }
        }
        return;
    }
    if (name == ".incbin") {   // a file's bytes, looked up next to the source, then from the working directory
        const std::string path = parseString(list.at(0));
        std::filesystem::path p = std::filesystem::path(file_).parent_path() / path;
        std::error_code ec;
        if (!std::filesystem::exists(p, ec)) p = path;
        if (!std::filesystem::exists(p, ec)) fail(".incbin: cannot find " + path);
        const std::vector<uint8_t> bytes = readFile(p);
        emitBytes(bytes.data(), bytes.size());
        return;
    }
    if (name == ".space" || name == ".skip" || name == ".zero") {
        const auto n = static_cast<uint32_t>(evalExpr(list.at(0)));
        const auto fill = static_cast<uint8_t>(list.size() > 1 ? evalExpr(list[1]) : 0);
        for (uint32_t i = 0; i < n; ++i) emitBytes(&fill, 1);
        return;
    }
    if (name == ".entry") {
        A.opt_.entrySymbol = list.at(0);
        return;
    }
    if (name == ".file" || name == ".ident" || name == ".frame" || name == ".mask" || name == ".fmask" ||
        name == ".previous" || name == ".local" || name == ".gnu_attribute" || name == ".module" || name == ".abicalls" ||
        name == ".option" || name == ".nan" || name == ".cfi_startproc" || name == ".cfi_endproc")
        return;
    fail("unknown directive " + name);
}

void AsmImpl::statement(std::string_view text) {
    text = trim(text);
    if (text.empty()) return;

    // Labels (possibly several on one line).
    for (;;) {
        size_t j = 0;
        while (j < text.size() && (std::isalnum(static_cast<unsigned char>(text[j])) || text[j] == '_' || text[j] == '.' || text[j] == '$')) ++j;
        if (j > 0 && j < text.size() && text[j] == ':') {
            defineLabel(std::string(text.substr(0, j)));
            ++stmtIndex_;
            text = trim(text.substr(j + 1));
            if (text.empty()) return;
            continue;
        }
        break;
    }

    // name = expr
    if (auto eq = text.find('='); eq != std::string_view::npos && text.find('(') == std::string_view::npos) {
        std::string_view lhs = trim(text.substr(0, eq));
        if (!lhs.empty() && lhs.find(' ') == std::string_view::npos && lhs[0] != '.') {
            if (pass_ == 1) A.equates_[std::string(lhs)] = evalExpr(text.substr(eq + 1));
            return;
        }
    }

    size_t sp = 0;
    while (sp < text.size() && !std::isspace(static_cast<unsigned char>(text[sp]))) ++sp;
    const std::string head = toLower(text.substr(0, sp));
    const std::string_view rest = trim(text.substr(sp));
    if (head[0] == '.' && !(head.size() > 1 && std::isdigit(static_cast<unsigned char>(head[1])))) {
        directive(head, rest);
    } else {
        if (section_ == 3) fail("instruction in .bss");
        if (sectionSize_[section_] % 4) fail("instruction is not word-aligned");
        instruction(head, rest);
    }
    ++stmtIndex_;
}

void AsmImpl::instruction(const std::string& mnemonic, std::string_view operands) {
    std::vector<uint32_t> words;
    if (!encodeOne(mnemonic, operands, here(), words)) fail("cannot encode '" + mnemonic + " " + std::string(operands) + "'");
    for (uint32_t w : words) emit32(w);
}

bool AsmImpl::encodeOne(std::string_view mnemonicIn, std::string_view operandText, uint32_t pc, std::vector<uint32_t>& words) {
    const std::string mn = toLower(mnemonicIn);
    const auto ops = splitOperands(operandText);
    auto gpr = [&](size_t i) -> uint32_t {
        if (i >= ops.size()) fail("missing operand for " + mn);
        auto r = gprIndex(ops[i]);
        if (!r) fail("expected a general-purpose register, got '" + ops[i] + "'");
        return *r;
    };
    auto branchOffset = [&](int64_t target, uint32_t at) -> uint32_t {
        const int64_t diff = target - static_cast<int64_t>(at + 4);
        if (pass_ == 2) {
            if (diff & 3) fail("branch target is not word-aligned");
            if (diff / 4 < -32768 || diff / 4 > 32767) fail("branch target out of range");
        }
        return static_cast<uint32_t>(diff / 4) & 0xFFFF;
    };
    auto enc = [&](Op op) { return r5900::info(op).match; };

    // ---- pseudo-instructions -------------------------------------------
    if (mn == "nop" && ops.empty()) { words.push_back(0); return true; }
    // 64-bit registers: "move" is daddu, as the PS2 toolchain emits it (addu would truncate to 32 bits).
    if (mn == "move" && ops.size() == 2) { words.push_back(enc(Op::DADDU) | (gpr(0) << 11) | (gpr(1) << 21)); return true; }
    if (mn == "negu" && ops.size() == 2) { words.push_back(enc(Op::SUBU) | (gpr(0) << 11) | (gpr(1) << 16)); return true; }
    if (mn == "not" && ops.size() == 2) { words.push_back(enc(Op::NOR) | (gpr(0) << 11) | (gpr(1) << 21)); return true; }
    if (mn == "b" && ops.size() == 1) { words.push_back(enc(Op::BEQ) | branchOffset(evalExpr(ops[0]), pc)); return true; }
    if (mn == "bal" && ops.size() == 1) { words.push_back(enc(Op::BGEZAL) | branchOffset(evalExpr(ops[0]), pc)); return true; }
    if ((mn == "beqz" || mn == "bnez" || mn == "beqzl" || mn == "bnezl") && ops.size() == 2) {
        const Op op = mn == "beqz" ? Op::BEQ : mn == "bnez" ? Op::BNE : mn == "beqzl" ? Op::BEQL : Op::BNEL;
        words.push_back(enc(op) | (gpr(0) << 21) | branchOffset(evalExpr(ops[1]), pc));
        return true;
    }
    if (mn == "jalr" && ops.size() == 1) { words.push_back(enc(Op::JALR) | (31u << 11) | (gpr(0) << 21)); return true; }
    if ((mn == "sync" || mn == "sync.l") && ops.empty()) { words.push_back(enc(Op::SYNC)); return true; }
    if (mn == "sync.p" && ops.empty()) { words.push_back(enc(Op::SYNC) | (0x10u << 6)); return true; }
    if (mn == "vcallmsr") { words.push_back(enc(Op::VCALLMSR)); return true; }
    if (mn == "li" && ops.size() == 2) {
        const uint32_t rt = gpr(0);
        bool sym = false;
        const int64_t v = evalExpr(ops[1], &sym);
        if (!sym && v >= -32768 && v <= 32767) {
            words.push_back(enc(Op::ADDIU) | (rt << 16) | (static_cast<uint32_t>(v) & 0xFFFF));
        } else if (!sym && v >= 0 && v <= 0xFFFF) {
            words.push_back(enc(Op::ORI) | (rt << 16) | static_cast<uint32_t>(v));
        } else if (!sym && (v & 0xFFFF) == 0 && v >= INT32_MIN && v <= 0xFFFFFFFFLL) {
            words.push_back(enc(Op::LUI) | (rt << 16) | ((static_cast<uint32_t>(v) >> 16) & 0xFFFF));
        } else {
            const auto u = static_cast<uint32_t>(v);
            words.push_back(enc(Op::LUI) | (rt << 16) | (u >> 16));
            words.push_back(enc(Op::ORI) | (rt << 16) | (rt << 21) | (u & 0xFFFF));
        }
        return true;
    }
    if (mn == "la" && ops.size() == 2) {
        const uint32_t rt = gpr(0);
        const auto u = static_cast<uint32_t>(evalExpr(ops[1]));
        words.push_back(enc(Op::LUI) | (rt << 16) | (((u + 0x8000) >> 16) & 0xFFFF));
        words.push_back(enc(Op::ADDIU) | (rt << 16) | (rt << 21) | (u & 0xFFFF));
        return true;
    }

    // ---- "lw $t0, symbol": GNU-as macro form, expands through $at -------
    if (ops.size() == 2 && ops[1].find('(') == std::string::npos) {
        if (auto it = tables().exact.find(mn); it != tables().exact.end()) {
            const auto& info = r5900::info(it->second);
            if (std::string_view(info.operands).find("o(b)") != std::string_view::npos && (info.flags & (r5900::F_LOAD | r5900::F_STORE))) {
                const auto addr = static_cast<uint32_t>(evalExpr(ops[1]));
                words.push_back(enc(Op::LUI) | (1u << 16) | (((addr + 0x8000) >> 16) & 0xFFFF));
                std::vector<uint32_t> second;
                encodeOne(mn, ops[0] + ", " + strprintf("%d", static_cast<int16_t>(addr & 0xFFFF)) + "($at)", pc + 4, second);
                words.insert(words.end(), second.begin(), second.end());
                return true;
            }
        }
    }

    // ---- table lookup ---------------------------------------------------
    Op op = Op::Invalid;
    int bc = -1;
    std::string suffix;
    const auto& T = tables();
    if (auto it = T.exact.find(mn); it != T.exact.end()) {
        op = it->second;
    } else {
        std::string headStr = mn;
        if (auto dot = mn.find('.'); dot != std::string::npos) {
            headStr = mn.substr(0, dot);
            suffix = mn.substr(dot + 1);
        }
        if (auto it2 = T.exact.find(headStr); it2 != T.exact.end()) {
            op = it2->second;
        } else if (!headStr.empty() && componentIndex(headStr.back()) >= 0) {
            if (auto it3 = T.bcBase.find(headStr.substr(0, headStr.size() - 1)); it3 != T.bcBase.end()) {
                op = it3->second;
                bc = componentIndex(headStr.back());
            }
        }
    }
    if (op == Op::Invalid) fail("unknown instruction '" + mn + "'");

    const auto& oi = r5900::info(op);
    uint32_t w = oi.match;
    if (oi.flags & r5900::F_VUBC) {
        if (bc < 0) fail(mn + ": missing broadcast component (x/y/z/w)");
        w |= static_cast<uint32_t>(bc);
    }
    if (oi.flags & r5900::F_VUDEST) {
        if (suffix.empty()) fail(mn + ": missing destination mask (.xyzw)");
        uint32_t dest = 0;
        for (char c : suffix) {
            const int ci = componentIndex(c);
            if (ci < 0) fail(mn + ": bad destination mask '" + suffix + "'");
            dest |= 8u >> ci;
        }
        w |= dest << 21;
    } else if (oi.flags & r5900::F_INTERLOCK) {
        if (suffix == "i") w |= 1;
        else if (!suffix.empty() && suffix != "ni") fail(mn + ": bad suffix");
    } else if (!suffix.empty()) {
        fail("unknown instruction '" + mn + "'");
    }

    // Operand specs; MULT-style ops accept the 2-operand form (rd = 0).
    std::vector<std::string> specs = split(oi.operands, ',');
    if (specs.size() == 1 && specs[0].empty()) specs.clear();
    if ((op == Op::MULT || op == Op::MULTU || op == Op::MADD || op == Op::MADDU || op == Op::MULT1 ||
         op == Op::MULTU1 || op == Op::MADD1 || op == Op::MADDU1) && ops.size() == 2)
        specs = {"s", "t"};
    if (op == Op::JALR && ops.size() == 1) specs = {"s"};

    // Optional trailing codes (syscall / break / trap).
    size_t required = specs.size();
    if (!specs.empty() && (specs.back() == "B" || specs.back() == "c") && ops.size() + 1 == specs.size()) required = ops.size();
    if (ops.size() != required) fail(strprintf("%s expects %zu operand(s), got %zu", mn.c_str(), specs.size(), ops.size()));

    for (size_t k = 0; k < ops.size(); ++k) {
        const std::string& sp = specs[k];
        const std::string& t = ops[k];
        auto vf = [&]() {
            auto v = vfOperand(t);
            if (!v) fail("expected a $vf register, got '" + t + "'");
            return *v;
        };
        auto vi = [&](std::string_view s) {
            auto v = prefixedIndex(s, "vi", 32);
            if (!v) fail("expected a $vi register, got '" + std::string(s) + "'");
            return *v;
        };
        auto imm = [&](int64_t lo, int64_t hi) {
            const int64_t v = evalExpr(t);
            if (pass_ == 2 && (v < lo || v > hi)) fail(strprintf("immediate %lld out of range [%lld, %lld]", (long long)v, (long long)lo, (long long)hi));
            return v;
        };
        if (sp == "d") w |= gpr(k) << 11;
        else if (sp == "s") w |= gpr(k) << 21;
        else if (sp == "t") w |= gpr(k) << 16;
        else if (sp == "<") w |= static_cast<uint32_t>(imm(0, 31)) << 6;
        else if (sp == "i") w |= static_cast<uint32_t>(imm(-32768, 32767)) & 0xFFFF;
        else if (sp == "u") w |= static_cast<uint32_t>(imm(-32768, 65535)) & 0xFFFF;
        else if (sp == "p") w |= branchOffset(evalExpr(t), pc);
        else if (sp == "a") {
            const auto target = static_cast<uint32_t>(evalExpr(t));
            if (pass_ == 2 && ((target & 0xF0000000u) != ((pc + 4) & 0xF0000000u) || (target & 3)))
                fail("jump target " + hex32(target) + " not reachable from " + hex32(pc));
            w |= (target >> 2) & 0x03FFFFFFu;
        } else if (sp == "o(b)") {
            const auto open = t.rfind('(');
            if (open == std::string::npos || t.back() != ')') fail("expected offset(base), got '" + t + "'");
            const std::string offText(trim(std::string_view(t).substr(0, open)));
            auto base = gprIndex(trim(std::string_view(t).substr(open + 1, t.size() - open - 2)));
            if (!base) fail("bad base register in '" + t + "'");
            const int64_t off = offText.empty() ? 0 : evalExpr(offText);
            if (pass_ == 2 && (off < -32768 || off > 32767)) fail("offset out of range in '" + t + "'");
            w |= (*base << 21) | (static_cast<uint32_t>(off) & 0xFFFF);
        } else if (sp == "D" || sp == "S" || sp == "T") {
            auto f = prefixedIndex(t, "f", 32);
            if (!f) fail("expected an FPU register, got '" + t + "'");
            w |= *f << (sp == "D" ? 6 : sp == "S" ? 11 : 16);
        } else if (sp == "g" || sp == "G" || sp == "E") {
            std::optional<uint32_t> r;
            if (sp == "E") {
                r = prefixedIndex(t, "vi", 32);
                for (uint32_t c = 16; !r && c < 32; ++c)
                    if (const char* n = r5900::vuControlName(c); n && toLower(t) == toLower(n)) r = c;
            } else {
                std::string_view tv = t;
                if (!tv.empty() && tv[0] == '$') tv.remove_prefix(1);
                if (auto v = parseInt(tv); v && *v >= 0 && *v < 32) r = static_cast<uint32_t>(*v);
                for (uint32_t c = 0; !r && sp == "G" && c < 32; ++c)
                    if (const char* n = r5900::cop0Name(c); n && toLower(tv) == toLower(n)) r = c;
            }
            if (!r) fail("bad control register '" + t + "'");
            w |= *r << 11;
        } else if (sp == "B") w |= (static_cast<uint32_t>(imm(0, 0xFFFFF)) & 0xFFFFF) << 6;
        else if (sp == "c") w |= (static_cast<uint32_t>(imm(0, 0x3FF)) & 0x3FF) << 6;
        else if (sp == "k" || sp == "h") w |= (static_cast<uint32_t>(imm(0, 31)) & 31) << 16;
        else if (sp == "r") w |= (static_cast<uint32_t>(imm(0, 31)) & 31) << 1;
        else if (sp == "V" || sp == "X" || sp == "Y") {
            auto [r, comp] = vf();
            if (comp >= 0) fail("unexpected component on '" + t + "'");
            w |= r << (sp == "V" ? 6 : sp == "X" ? 11 : 16);
        } else if (sp == "Yb") {
            auto [r, comp] = vf();
            if (comp >= 0 && comp != bc) fail("broadcast component of '" + t + "' does not match " + mn);
            w |= r << 16;
        } else if (sp == "Yw") {
            auto [r, comp] = vf();
            if (comp >= 0 && comp != 3) fail("vclipw needs the w component");
            w |= r << 16;
        } else if (sp == "Xf" || sp == "Yf") {
            auto [r, comp] = vf();
            if (comp < 0) fail("'" + t + "' needs a component (x/y/z/w)");
            if (sp == "Xf") w |= (r << 11) | (static_cast<uint32_t>(comp) << 21);
            else w |= (r << 16) | (static_cast<uint32_t>(comp) << 23);
        } else if (sp == "e") w |= vi(t) << 6;
        else if (sp == "f") w |= vi(t) << 11;
        else if (sp == "n") w |= vi(t) << 16;
        else if (sp == "5") w |= (static_cast<uint32_t>(imm(-16, 15)) & 31) << 6;
        else if (sp == "M") {
            const int64_t a = imm(0, 0x7FFF * 8);
            if (a & 7) fail("vcallms address must be a multiple of 8");
            w |= (static_cast<uint32_t>(a / 8) & 0x7FFF) << 6;
        } else if (sp == "A" || sp == "Q" || sp == "I" || sp == "R") {
            std::string_view tv = t;
            if (!tv.empty() && tv[0] == '$') tv.remove_prefix(1);
            const std::string want = sp == "A" ? "acc" : toLower(sp);
            if (toLower(tv) != want) fail("expected $" + std::string(sp == "A" ? "ACC" : sp) + ", got '" + t + "'");
        } else if (sp == "(f++)" || sp == "(n++)" || sp == "(--f)" || sp == "(--n)" || sp == "(f)") {
            std::string_view tv = trim(t);
            if (tv.size() < 3 || tv.front() != '(' || tv.back() != ')') fail("expected ($viN...), got '" + t + "'");
            tv = trim(tv.substr(1, tv.size() - 2));
            const bool inc = sp.find("++") != std::string::npos, dec = sp.find("--") != std::string::npos;
            if (inc) {
                if (!endsWith(tv, "++")) fail("expected post-increment in '" + t + "'");
                tv = trim(tv.substr(0, tv.size() - 2));
            }
            if (dec) {
                if (!startsWith(tv, "--")) fail("expected pre-decrement in '" + t + "'");
                tv = trim(tv.substr(2));
            }
            w |= vi(tv) << (sp.find('f') != std::string::npos ? 11 : 16);
        } else if (sp == "P") {
        } else {
            fail("internal: unhandled operand spec '" + sp + "'");
        }
    }
    words.push_back(w);
    return true;
}

void AsmImpl::finalizePass1() {
    auto& secs = A.sections_;
    uint32_t base = A.opt_.textBase;
    for (int s = 0; s < 4; ++s) {
        const uint32_t align = std::max<uint32_t>(64, secs[s].align);
        base = (base + align - 1) & ~(align - 1);
        secs[s].base = base;
        secs[s].size = sectionSize_[s];
        base += sectionSize_[s];
    }
}

void AsmImpl::run(std::string_view source) {
    std::vector<Line> lines;
    {
        int n = 0;
        for (const auto& raw : split(source, '\n')) {
            ++n;
            std::string text;
            bool inStr = false;
            for (size_t i = 0; i < raw.size(); ++i) {
                const char c = raw[i];
                if (c == '"' && (i == 0 || raw[i - 1] != '\\')) inStr = !inStr;
                if (!inStr && (c == '#' || (c == '/' && i + 1 < raw.size() && raw[i + 1] == '/'))) break;
                text.push_back(c);
            }
            // ';' separates statements
            std::string cur;
            inStr = false;
            for (char c : text) {
                if (c == '"') inStr = !inStr;
                if (c == ';' && !inStr) {
                    lines.push_back({n, cur});
                    cur.clear();
                } else {
                    cur.push_back(c);
                }
            }
            lines.push_back({n, cur});
        }
    }

    for (pass_ = 1; pass_ <= 2; ++pass_) {
        section_ = 0;
        stmtIndex_ = 0;
        for (auto& s : sectionSize_) s = 0;
        for (auto& sec : A.sections_) sec.bytes.clear();
        for (const auto& l : lines) {
            lineNo_ = l.number;
            statement(l.text);
        }
        if (pass_ == 1) finalizePass1();
    }
    for (const auto& [name, f] : functions_)
        if (f.open) throw Error(file_ + ": .ent " + name + " has no matching .end");

    // Publish symbols.
    for (const auto& [name, loc] : labels_) {
        if (startsWith(name, ".L")) continue;
        AsmSymbol sym;
        sym.name = name;
        sym.section = loc.first;
        sym.value = A.sections_[loc.first].base + loc.second;
        sym.global = globals_.count(name) > 0;
        sym.object = objectTypes_.count(name) > 0;
        if (auto f = functions_.find(name); f != functions_.end()) {
            sym.function = true;
            sym.size = f->second.end - f->second.start.second;
        } else if (functionTypes_.count(name)) {
            sym.function = true;
        }
        if (auto sz = sizes_.find(name); sz != sizes_.end()) sym.size = sz->second;
        A.symbols_[name] = sym;
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
Assembler::Assembler(AsmOptions options) : opt_(std::move(options)) {}

void Assembler::assemble(std::string_view source, std::string_view fileName) {
    AsmImpl impl(*this, fileName);
    impl.run(source);
}

uint32_t Assembler::entry() const {
    if (!opt_.entrySymbol.empty()) {
        auto it = symbols_.find(opt_.entrySymbol);
        if (it == symbols_.end()) throw Error("entry symbol '" + opt_.entrySymbol + "' is not defined");
        return it->second.value;
    }
    for (const char* n : {"_start", "__start"})
        if (auto it = symbols_.find(n); it != symbols_.end()) return it->second.value;
    return sections_[0].base;
}

namespace {
void put16(std::vector<uint8_t>& b, size_t off, uint16_t v) {
    b[off] = static_cast<uint8_t>(v);
    b[off + 1] = static_cast<uint8_t>(v >> 8);
}
void put32(std::vector<uint8_t>& b, size_t off, uint32_t v) {
    for (int i = 0; i < 4; ++i) b[off + static_cast<size_t>(i)] = static_cast<uint8_t>(v >> (8 * i));
}
size_t align16(size_t v) { return (v + 15) & ~size_t(15); }
} // namespace

std::vector<uint8_t> Assembler::buildElf() const {
    // Segment 1: .text + .rodata (R+X). Segment 2: .data + .bss (R+W).
    struct Seg {
        uint32_t vaddr = 0, filesz = 0, memsz = 0, flags = 0;
        std::vector<uint8_t> image;
        size_t offset = 0;
    };
    auto buildSeg = [&](int first, int last, uint32_t flags) {
        Seg s;
        s.flags = flags;
        int lo = -1, hi = -1;
        for (int i = first; i <= last; ++i)
            if (sections_[i].size) {
                if (lo < 0) lo = i;
                hi = i;
            }
        if (lo < 0) return s;
        s.vaddr = sections_[lo].base;
        const uint32_t end = sections_[hi].base + sections_[hi].size;
        s.memsz = end - s.vaddr;
        uint32_t fileEnd = s.vaddr;
        for (int i = lo; i <= hi; ++i)
            if (i != 3 && sections_[i].size) fileEnd = sections_[i].base + sections_[i].size;
        s.filesz = fileEnd - s.vaddr;
        s.image.assign(s.filesz, 0);
        for (int i = lo; i <= hi; ++i)
            if (i != 3 && sections_[i].size)
                std::copy(sections_[i].bytes.begin(), sections_[i].bytes.end(), s.image.begin() + (sections_[i].base - s.vaddr));
        return s;
    };
    Seg segs[2] = {buildSeg(0, 1, elf::PF_R | elf::PF_X), buildSeg(2, 3, elf::PF_R | elf::PF_W)};
    const int nseg = segs[1].memsz ? 2 : 1;

    // String tables and symbols.
    std::string shstr(1, '\0'), strtab(1, '\0');
    auto addStr = [](std::string& tab, const std::string& s) {
        const auto off = static_cast<uint32_t>(tab.size());
        tab += s;
        tab.push_back('\0');
        return off;
    };

    struct Sh {
        std::string name;
        uint32_t type, flags, addr, size, link = 0, info = 0, align, entsize = 0;
        std::vector<uint8_t> data;
        size_t offset = 0;
        uint32_t nameOff = 0;
    };
    std::vector<Sh> shs;
    int secIndex[4] = {0, 0, 0, 0};
    const uint32_t secTypes[4] = {elf::SHT_PROGBITS, elf::SHT_PROGBITS, elf::SHT_PROGBITS, elf::SHT_NOBITS};
    const uint32_t secFlags[4] = {elf::SHF_ALLOC | elf::SHF_EXECINSTR, elf::SHF_ALLOC, elf::SHF_ALLOC | elf::SHF_WRITE,
                                  elf::SHF_ALLOC | elf::SHF_WRITE};
    for (int i = 0; i < 4; ++i) {
        if (!sections_[i].size) continue;
        secIndex[i] = static_cast<int>(shs.size()) + 1;
        shs.push_back({kSectionNames[i], secTypes[i], secFlags[i], sections_[i].base, sections_[i].size, 0, 0,
                       sections_[i].align, 0, {}, 0, 0});
    }

    if (opt_.symbols) {
        std::vector<const AsmSymbol*> locals, globals;
        for (const auto& [n, s] : symbols_) (s.global ? globals : locals).push_back(&s);
        std::vector<uint8_t> sym(16, 0);
        auto addSym = [&](const AsmSymbol& s) {
            std::vector<uint8_t> e(16, 0);
            put32(e, 0, addStr(strtab, s.name));
            put32(e, 4, s.value);
            put32(e, 8, s.size);
            const uint8_t type = s.function ? elf::STT_FUNC : s.object ? elf::STT_OBJECT : elf::STT_NOTYPE;
            e[12] = static_cast<uint8_t>(((s.global ? elf::STB_GLOBAL : elf::STB_LOCAL) << 4) | type);
            put16(e, 14, static_cast<uint16_t>(s.section >= 0 ? secIndex[s.section] : 0xFFF1));
            sym.insert(sym.end(), e.begin(), e.end());
        };
        for (auto* s : locals) addSym(*s);
        for (auto* s : globals) addSym(*s);
        const auto symtabIdx = static_cast<uint32_t>(shs.size()) + 1;
        shs.push_back({".symtab", elf::SHT_SYMTAB, 0, 0, static_cast<uint32_t>(sym.size()), symtabIdx + 1,
                       static_cast<uint32_t>(locals.size() + 1), 4, 16, std::move(sym), 0, 0});
        std::vector<uint8_t> strData(strtab.begin(), strtab.end());
        shs.push_back({".strtab", elf::SHT_STRTAB, 0, 0, static_cast<uint32_t>(strData.size()), 0, 0, 1, 0,
                       std::move(strData), 0, 0});
    }
    for (auto& s : shs) s.nameOff = addStr(shstr, s.name);
    const uint32_t shstrNameOff = addStr(shstr, ".shstrtab");
    {
        std::vector<uint8_t> d(shstr.begin(), shstr.end());
        shs.push_back({".shstrtab", elf::SHT_STRTAB, 0, 0, static_cast<uint32_t>(d.size()), 0, 0, 1, 0, std::move(d), 0,
                       shstrNameOff});
    }

    // File layout.
    size_t off = 0x100;
    for (int i = 0; i < nseg; ++i) {
        segs[i].offset = off;
        off = align16(off + segs[i].filesz);
    }
    for (auto& s : shs) {
        if (s.type == elf::SHT_PROGBITS || s.type == elf::SHT_NOBITS) {
            const int seg = (s.flags & elf::SHF_WRITE) ? 1 : 0;
            s.offset = segs[seg].offset + (s.addr - segs[seg].vaddr);
        } else {
            s.offset = off;
            off = align16(off + s.data.size());
        }
    }
    const size_t shoff = align16(off);
    const auto shnum = static_cast<uint16_t>(shs.size() + 1);
    std::vector<uint8_t> out(shoff + 40u * shnum, 0);

    // ELF header.
    const uint8_t ident[16] = {0x7F, 'E', 'L', 'F', 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    std::copy(ident, ident + 16, out.begin());
    put16(out, 16, elf::ET_EXEC);
    put16(out, 18, elf::EM_MIPS);
    put32(out, 20, 1);
    put32(out, 24, entry());
    put32(out, 28, 52);
    put32(out, 32, static_cast<uint32_t>(shoff));
    put32(out, 36, 0x20924001);   // EF_MIPS_NOREORDER | mips3 | R5900, as PS2 toolchains emit
    put16(out, 40, 52);
    put16(out, 42, 32);
    put16(out, 44, static_cast<uint16_t>(nseg));
    put16(out, 46, 40);
    put16(out, 48, shnum);
    put16(out, 50, static_cast<uint16_t>(shnum - 1));

    for (int i = 0; i < nseg; ++i) {
        const size_t p = 52 + 32u * static_cast<size_t>(i);
        put32(out, p, elf::PT_LOAD);
        put32(out, p + 4, static_cast<uint32_t>(segs[i].offset));
        put32(out, p + 8, segs[i].vaddr);
        put32(out, p + 12, segs[i].vaddr);
        put32(out, p + 16, segs[i].filesz);
        put32(out, p + 20, segs[i].memsz);
        put32(out, p + 24, segs[i].flags);
        put32(out, p + 28, 16);
        std::copy(segs[i].image.begin(), segs[i].image.end(), out.begin() + static_cast<ptrdiff_t>(segs[i].offset));
    }
    for (size_t i = 0; i < shs.size(); ++i) {
        const auto& s = shs[i];
        if (!s.data.empty()) std::copy(s.data.begin(), s.data.end(), out.begin() + static_cast<ptrdiff_t>(s.offset));
        const size_t h = shoff + 40 * (i + 1);
        put32(out, h, s.nameOff);
        put32(out, h + 4, s.type);
        put32(out, h + 8, s.flags);
        put32(out, h + 12, s.addr);
        put32(out, h + 16, static_cast<uint32_t>(s.offset));
        put32(out, h + 20, s.size);
        put32(out, h + 24, s.link);
        put32(out, h + 28, s.info);
        put32(out, h + 32, s.align);
        put32(out, h + 36, s.entsize);
    }
    return out;
}

bool assembleInstruction(std::string_view line, uint32_t addr, uint32_t& out, std::string* error) {
    try {
        Assembler a;
        AsmImpl impl(a, "<insn>");
        line = trim(line);
        size_t sp = 0;
        while (sp < line.size() && !std::isspace(static_cast<unsigned char>(line[sp]))) ++sp;
        std::vector<uint32_t> words;
        impl.encodeOneForTest(line.substr(0, sp), trim(line.substr(sp)), addr, words);
        if (words.size() != 1) {
            if (error) *error = "expands to more than one instruction";
            return false;
        }
        out = words[0];
        return true;
    } catch (const Error& e) {
        if (error) *error = e.what();
        return false;
    }
}

} // namespace ps2uc
