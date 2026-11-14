// SPDX-License-Identifier: GPL-3.0-or-later
//
// See vu_codegen.h.

#include "vu_codegen.h"

#include "ps2n/util.h"

#include <algorithm>
#include <sstream>

namespace ps2n::recomp {

// ---- capture files ---------------------------------------------------------------------------

void VuCapture::add(const VuCapture& other) { blocks.insert(blocks.end(), other.blocks.begin(), other.blocks.end()); }

void VuCapture::normalize() {
    const auto before = [](const Block& a, const Block& b) { return a.vu != b.vu ? a.vu < b.vu : a.key < b.key; };
    const auto same = [](const Block& a, const Block& b) { return a.vu == b.vu && a.key == b.key; };
    std::stable_sort(blocks.begin(), blocks.end(), before);
    blocks.erase(std::unique(blocks.begin(), blocks.end(), same), blocks.end());
}

VuCapture parseVuCapture(const std::string& text, const std::string& fileName) {
    VuCapture capture;
    enum class List { None, Words, Steps } list = List::None;
    bool inBlock = false;
    std::vector<uint32_t> words;

    std::istringstream in(text);
    std::string line;
    int lineNo = 0;
    const auto fail = [&](const std::string& what) -> Error { return Error(fileName + ":" + std::to_string(lineNo) + ": " + what); };
    const auto number = [&](std::string_view token) -> uint32_t {
        const auto v = parseInt(token);
        if (!v || *v < 0 || *v > 0xFFFFFFFFLL) throw fail("'" + std::string(token) + "' is not a 32-bit number");
        return static_cast<uint32_t>(*v);
    };
    // One step: its name and its operands, as planText() writes them.
    const auto step = [&](std::string_view s) -> vu::Step {
        const std::vector<std::string> tokens = split(std::string(trim(s)), ' ');
        if (tokens.empty()) throw fail("an empty step");
        vu::Step out;
        out.op = vu::stepByName(tokens[0].data(), tokens[0].size());
        if (out.op == vu::StepOp::Count) throw fail("'" + tokens[0] + "' is not a step (was the file written by another version?)");
        const uint32_t n = vu::stepOperandCount(out.op);
        if (tokens.size() != n + 1) throw fail("'" + tokens[0] + "' takes " + std::to_string(n) + " operands");
        for (uint32_t k = 0; k < n; ++k) out.o[k] = number(tokens[k + 1]);
        return out;
    };
    const auto endBlock = [&]() {
        if (!inBlock) return;
        VuCapture::Block& b = capture.blocks.back();
        if (b.vu != 0 && b.vu != 1) throw fail("a [[block]] needs vu = 0 or 1");
        if (words.size() % 2) throw fail("a [[block]]'s words are pairs: a lower and an upper word each");
        for (size_t i = 0; i + 1 < words.size(); i += 2) b.words.push_back(static_cast<uint64_t>(words[i]) | static_cast<uint64_t>(words[i + 1]) << 32);
        words.clear();
        b.key = vu::planKey(b.vu, b.steps.data(), b.steps.size());
    };

    while (std::getline(in, line)) {
        ++lineNo;
        if (auto hash = line.find('#'); hash != std::string::npos) line.resize(hash);
        std::string_view rest = trim(line);
        if (rest.empty()) continue;

        if (list == List::None) {
            if (rest == "[[block]]") {
                endBlock();
                capture.blocks.emplace_back();
                capture.blocks.back().vu = -1;
                inBlock = true;
                continue;
            }
            const size_t eq = rest.find('=');
            if (eq == std::string_view::npos) throw fail("expected 'name = value' or [[block]]");
            const std::string_view key = trim(rest.substr(0, eq));
            rest = trim(rest.substr(eq + 1));
            if (!inBlock) throw fail("'" + std::string(key) + "' outside a [[block]]");
            if (key == "vu") {
                capture.blocks.back().vu = static_cast<int>(number(rest));
                continue;
            }
            if (key == "at") {
                capture.blocks.back().at = number(rest);
                continue;
            }
            if (key == "key") continue;   // made again from the steps
            if (key == "words") list = List::Words;
            else if (key == "steps") list = List::Steps;
            else throw fail("unexpected '" + std::string(key) + "'");
            if (rest.empty() || rest[0] != '[') throw fail("expected '[' after '" + std::string(key) + " ='");
            rest.remove_prefix(1);
        }

        // Inside a list: numbers or quoted steps, commas, and the closing bracket.
        while (!rest.empty()) {
            rest = trim(rest);
            if (rest.empty()) break;
            if (rest[0] == ']') {
                list = List::None;
                if (!trim(rest.substr(1)).empty()) throw fail("text after ']'");
                break;
            }
            if (rest[0] == ',') {
                rest.remove_prefix(1);
                continue;
            }
            if (list == List::Steps) {
                if (rest[0] != '"') throw fail("a step is written in double quotes");
                const size_t close = rest.find('"', 1);
                if (close == std::string_view::npos) throw fail("a step's closing quote is missing");
                capture.blocks.back().steps.push_back(step(rest.substr(1, close - 1)));
                rest.remove_prefix(close + 1);
            } else {
                const size_t cut = rest.find_first_of(", \t]");
                words.push_back(number(rest.substr(0, cut)));
                if (cut == std::string_view::npos) break;
                rest.remove_prefix(cut);
            }
        }
    }
    if (list != List::None) throw fail("a list is not closed");
    endBlock();
    return capture;
}

VuCapture loadVuCaptureFile(const std::filesystem::path& file) { return parseVuCapture(readTextFile(file), file.string()); }

std::string writeVuCapture(const VuCapture& capture, const std::string& comment) {
    std::string out;
    if (!comment.empty()) out += "# " + comment + "\n";
    out += "# Blocks of micro code as their plans (core/include/ps2n/vu_plan.h). Give this file to the\n"
           "# recompiler (ps2n-recomp vu) to have them compiled.\n";
    for (const VuCapture::Block& b : capture.blocks)
        out += vu::planText(b.vu, b.steps.data(), b.steps.size(), b.at, b.words.data(), b.words.size());
    return out;
}

// ---- generation --------------------------------------------------------------------------------

namespace {

std::string fnName(const VuCapture::Block& b) { return strprintf("b%d_%s", b.vu, vu::planKeyText(b.key).c_str()); }

std::string operand(uint32_t v) { return v < 10 ? strprintf("%uu", v) : strprintf("0x%xu", v); }

std::string emitBlock(const VuCapture::Block& b, const VuGenerateOptions& options, bool split) {
    std::string out;
    if (options.comments) {
        out += strprintf("// VU%d, first met at %04x: %zu pairs, %zu steps\n", b.vu, b.at, b.words.size(), b.steps.size());
        for (size_t i = 0; i < b.words.size(); ++i) {
            if (i % 6 == 0) out += i ? "\n//  " : "//  ";
            out += strprintf(" %08x:%08x", static_cast<uint32_t>(b.words[i] >> 32), static_cast<uint32_t>(b.words[i]));
        }
        if (!b.words.empty()) out += "\n";
    }
    out += strprintf("%sconst Link* PS2N_VU_CC %s(Ctx& c, const Link* b)\n{\n\tPS2N_VU_ACCESS a(c, b->base);\n", split ? "" : "static ",
                     fnName(b).c_str());
    bool left = false;
    for (const vu::Step& s : b.steps) {
        if (left) throw Error("a plan goes on after its step Leave");
        if (s.op == vu::StepOp::Leave) {
            // The block's end: its link, what the steps wrote put back, and
            // the program goes on at the next block, or stops.
            out += "\tPS2N_VU_LEAVE(" + operand(s.o[0]) + ", " + operand(s.o[1]) + ", " + operand(s.o[2]) + ");\n";
            left = true;
            continue;
        }
        std::string call = std::string("step::") + vu::stepName(s.op) + "(a";
        const uint32_t n = vu::stepOperandCount(s.op);
        for (uint32_t k = 0; k < n; ++k) call += ", " + operand(s.o[k]);
        call += ")";
        if (s.op == vu::StepOp::TBit) {
            // The program ends here if FBRST says so: what the steps so far
            // wrote goes back first.
            out += "\tif (" + call + ")\n\t{\n\t\ta.finish();\n\t\treturn Stop(c, b, kStopTBit + " + operand(s.o[0]) + ");\n\t}\n";
        } else {
            out += "\t" + call + ";\n";
        }
    }
    // A plan from before every plan ended with Leave: the provider sees to its end.
    if (!left) out += "\ta.finish();\n\treturn Stop(c, b, kStopEnd);\n";
    out += "}\n\n";
    return out;
}

} // namespace

std::vector<GeneratedFile> generateVuFiles(const VuCapture& capture, const VuGenerateOptions& options, VuGenerateStats* statsOut) {
    if (options.id.empty() || identifierFor(options.id) != options.id) throw Error("'" + options.id + "' cannot name a generated namespace");
    if (options.parts < 1 || options.parts > 999) throw Error("the number of parts must be between 1 and 999");
    const bool split = options.parts > 1;
    if (split && options.headerName.empty()) throw Error("code split into parts needs a name for its header");

    VuGenerateStats stats;
    const std::string banner = strprintf("// Generated by %s from the plans of blocks of micro code that runs captured. Do not edit.\n", kGeneratorName) +
                               "//\n// This file is derived from the programs whose micro code those blocks were and is under\n"
                               "// their terms, not PS2Native's. It may never leave the build tree.\n\n";
    const std::string open = "namespace ps2n_vu_image_" + options.id + "\n{\n\nusing namespace ps2n::vu;\n\n";
    const std::string close = "} // namespace ps2n_vu_image_" + options.id + "\n";

    // The functions of each part: in the order of their keys, and of about
    // the same number of steps each.
    uint64_t stepsInAll = 0, stepsBefore = 0;
    for (const VuCapture::Block& b : capture.blocks) stepsInAll += b.steps.size() + 1;
    std::vector<std::string> bodies(options.parts);
    std::string declarations;
    std::string table = "static const BlockInfo blocks[] = {\n";
    for (const VuCapture::Block& b : capture.blocks) {
        std::string& out = bodies[std::min<uint64_t>(options.parts - 1, stepsBefore * options.parts / std::max<uint64_t>(stepsInAll, 1))];
        stepsBefore += b.steps.size() + 1;
        out += emitBlock(b, options, split);
        declarations += strprintf("%sconst Link* PS2N_VU_CC %s(Ctx& c, const Link* b);\n", split ? "" : "static ", fnName(b).c_str());
        table += strprintf("\t{0x%016llXull, 0x%016llXull, &%s, %d},\n", static_cast<unsigned long long>(b.key.a),
                           static_cast<unsigned long long>(b.key.b), fnName(b).c_str(), b.vu);
        ++stats.blocks[b.vu ? 1 : 0];
        stats.steps += b.steps.size();
    }
    // The last element is null, so that the array is never empty.
    table += "\t{0, 0, nullptr, 0},\n};\n\n";

    const std::string tables = table + "extern const ImageInfo image;\nconst ImageInfo image = {\n\t\"" + options.id + "\",\n\tblocks,\n\t" +
                               std::to_string(capture.blocks.size()) + ",\n\t\"" + kGeneratorName + "\",\n};\n\n";

    std::vector<GeneratedFile> files;
    if (!split) {
        files.push_back({".cpp", banner + "#include \"ps2n/vu_recomp.h\"\n\n" + open + declarations + "\n" + bodies[0] + tables + close});
    } else {
        const std::vector<std::string> suffixes = generatedSuffixes(options.parts);
        files.push_back({suffixes[0], banner + "#include \"" + options.headerName + "\"\n\n" + open + tables + close});
        files.push_back({suffixes[1], banner + "#pragma once\n\n#include \"ps2n/vu_recomp.h\"\n\n" + open + declarations + "\n" + close});
        for (unsigned i = 0; i < options.parts; ++i)
            files.push_back({suffixes[2 + i], banner + "#include \"" + options.headerName + "\"\n\n" + open + bodies[i] + close});
    }
    if (statsOut) *statsOut = stats;
    return files;
}

} // namespace ps2n::recomp
