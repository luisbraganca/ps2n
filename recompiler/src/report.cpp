#include "report.h"

#include "version.h"

#include "ps2n/util.h"

#include <sstream>

namespace ps2n {

namespace {
size_t countBinding(const Program& p, Binding b) {
    size_t n = 0;
    for (const auto& f : p.functions) n += f.binding == b;
    return n;
}
} // namespace

std::string summarize(const Program& p) {
    size_t unresolved = 0;
    for (const auto& f : p.functions) unresolved += f.unresolvedJumps.size();
    return strprintf("%zu functions (%zu recompiled, %zu stubbed, %zu skipped, %zu overridden), %zu instructions, "
                     "%zu jump tables resolved, %zu unresolved indirect jumps, %zu unknown instructions, %zu warnings",
                     p.functions.size(), countBinding(p, Binding::Recompile), countBinding(p, Binding::Stub),
                     countBinding(p, Binding::Skip), countBinding(p, Binding::Override), p.instructionCount, p.resolvedJumpTables,
                     unresolved, p.unknownInstructions.size(), p.warnings.size());
}

std::string buildReport(const Program& p, const Config& cfg) {
    std::ostringstream r;
    r << PS2N_VERSION_STRING << " recompilation report\n\n";
    r << "input        " << cfg.elf.string() << "\n";
    r << strprintf("crc32        %08x\nentry        0x%08x\n", p.elf->crc32(), p.elf->entry);
    for (const auto& range : p.elf->codeRanges())
        r << strprintf("code         %-12s 0x%08x-0x%08x (%u bytes)\n", range.name.c_str(), range.start, range.end, range.size());
    for (const auto& s : p.elf->sections)
        if (s.alloc() && s.vuMicrocode() && s.size)
            r << strprintf("vu code      %-12s 0x%08x-0x%08x (%u bytes, not recompiled: runs on the VU interpreter)\n", s.name.c_str(),
                           s.addr, s.addr + s.size, s.size);
    r << "\nsummary      " << summarize(p) << "\n\nfunction starts by source\n";
    for (const auto& [src, n] : p.sourceCounts) r << strprintf("  %-28s %zu\n", sourceName(src), n);

    auto list = [&](Binding b, const char* title) {
        bool any = false;
        for (const auto& f : p.functions) {
            if (f.binding != b) continue;
            if (!any) r << "\n" << title << "\n";
            any = true;
            r << strprintf("  0x%08x  %-32s", f.start, f.name.c_str());
            if (!f.handler.empty()) r << " -> " << f.handler;
            r << "\n";
        }
    };
    list(Binding::Stub, "stubbed (runtime handler)");
    list(Binding::Skip, "skipped (log + return 0 when called)");
    list(Binding::Override, "overridden (your C++ function)");

    bool anyIndirect = false;
    for (const auto& f : p.functions) {
        for (uint32_t jr : f.unresolvedJumps) {
            if (!anyIndirect) r << "\nunresolved indirect jumps (dispatched at run time; add [[analysis.functions]] or entries if they misbehave)\n";
            anyIndirect = true;
            r << strprintf("  0x%08x  in %s", jr, f.name.c_str());
            if (!f.switchCandidates.empty()) r << strprintf(" (%zu data-referenced labels tried first)", f.switchCandidates.size());
            r << "\n";
        }
    }
    if (!p.unknownInstructions.empty()) {
        r << "\nunknown instructions (interpreted at run time, which will report them too)\n";
        for (const auto& d : p.unknownInstructions) r << strprintf("  0x%08x  %s\n", d.address, d.message.c_str());
    }
    if (!p.warnings.empty()) {
        r << "\nwarnings\n";
        for (const auto& d : p.warnings) r << (d.address ? strprintf("  0x%08x  ", d.address) : std::string("  ")) << d.message << "\n";
    }
    r << "\nfunctions\n";
    for (const auto& f : p.functions) {
        const char* b = f.binding == Binding::Recompile ? "" : f.binding == Binding::Stub ? " [stub]" : f.binding == Binding::Skip ? " [skip]" : " [override]";
        r << strprintf("  0x%08x-0x%08x  %-40s %-26s%s\n", f.start, f.end, f.name.c_str(), sourceName(f.source), b);
    }
    return r.str();
}

} // namespace ps2n
