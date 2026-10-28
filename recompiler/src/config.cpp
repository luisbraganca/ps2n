#include "config.h"

#include "ps2uc/util.h"

#define TOML_EXCEPTIONS 1
#include <toml.hpp>

#include <sstream>

namespace ps2uc {

namespace {

std::string where(const toml::node& n) {
    const auto& src = n.source();
    std::ostringstream ss;
    if (src.path) ss << *src.path << ":";
    ss << src.begin.line;
    return ss.str();
}

[[noreturn]] void fail(const toml::node& n, const std::string& msg) { throw Error(where(n) + ": " + msg); }

uint32_t toAddress(const toml::node& n) {
    if (auto i = n.value_exact<int64_t>()) {
        if (*i < 0 || *i > 0xFFFFFFFFLL) fail(n, "address out of range");
        return static_cast<uint32_t>(*i);
    }
    if (auto s = n.value_exact<std::string>()) {
        if (auto v = parseInt(*s); v && *v >= 0 && *v <= 0xFFFFFFFFLL) return static_cast<uint32_t>(*v);
    }
    fail(n, "expected an address (integer or \"0x...\" string)");
}

bool boolOr(const toml::table& t, const char* key, bool def) {
    if (const toml::node* n = t.get(key)) {
        if (auto b = n->value_exact<bool>()) return *b;
        fail(*n, std::string(key) + " must be true or false");
    }
    return def;
}

std::string stringOr(const toml::table& t, const char* key, const std::string& def) {
    if (const toml::node* n = t.get(key)) {
        if (auto s = n->value_exact<std::string>()) return *s;
        fail(*n, std::string(key) + " must be a string");
    }
    return def;
}

std::vector<std::string> stringList(const toml::table& t, const char* key) {
    std::vector<std::string> out;
    const toml::node* n = t.get(key);
    if (!n) return out;
    const toml::array* arr = n->as_array();
    if (!arr) fail(*n, std::string(key) + " must be an array of strings");
    for (const auto& e : *arr) {
        if (auto s = e.value<std::string>()) out.push_back(*s);
        else if (auto i = e.value<int64_t>()) out.push_back(hex32(static_cast<uint32_t>(*i)));
        else fail(e, std::string(key) + " entries must be strings");
    }
    return out;
}

// Bindings accept either an inline table { "target" = "handler" } or an
// array of "handler" / "handler@0xADDRESS" strings (the PS2Recomp style).
void bindingList(const toml::table& t, const char* key, std::vector<std::pair<FunctionRef, std::string>>& out) {
    const toml::node* n = t.get(key);
    if (!n) return;
    if (const toml::table* tab = n->as_table()) {
        for (const auto& [k, v] : *tab) {
            auto s = v.value<std::string>();
            if (!s) fail(v, std::string(key) + " values must be strings");
            out.emplace_back(parseFunctionRef(std::string(k.str())), *s);
        }
        return;
    }
    if (const toml::array* arr = n->as_array()) {
        for (const auto& e : *arr) {
            auto s = e.value<std::string>();
            if (!s) fail(e, std::string(key) + " entries must be strings");
            const auto at = s->find('@');
            if (at == std::string::npos) out.emplace_back(parseFunctionRef(*s), *s);
            else out.emplace_back(parseFunctionRef(s->substr(at + 1)), s->substr(0, at));
        }
        return;
    }
    fail(*n, std::string(key) + " must be a table or an array");
}

void checkKeys(const toml::table& t, std::initializer_list<const char*> allowed, const std::string& section) {
    for (const auto& [k, v] : t) {
        bool ok = false;
        for (const char* a : allowed)
            if (k.str() == a) ok = true;
        if (!ok) fail(v, "unknown key '" + std::string(k.str()) + "' in [" + section + "]");
    }
}

} // namespace

FunctionRef parseFunctionRef(const std::string& text) {
    FunctionRef r;
    r.text = text;
    const std::string t(trim(text));
    if (startsWith(t, "0x") || startsWith(t, "0X")) {
        if (auto v = parseInt(t); v && *v >= 0 && *v <= 0xFFFFFFFFLL) r.address = static_cast<uint32_t>(*v);
    }
    return r;
}

std::filesystem::path Config::resolve(const std::filesystem::path& p) const {
    if (p.empty() || p.is_absolute()) return p;
    return (configDir / p).lexically_normal();
}

Config Config::forElf(const std::filesystem::path& elf) {
    Config c;
    c.configDir = std::filesystem::current_path();
    c.elf = std::filesystem::absolute(elf);
    c.name = elf.stem().string();
    c.outDir = c.resolve("out/" + c.name);
    return c;
}

Config Config::load(const std::filesystem::path& file) {
    toml::table root;
    try {
        root = toml::parse_file(file.string());
    } catch (const toml::parse_error& e) {
        std::ostringstream ss;
        ss << file.string() << ":" << e.source().begin.line << ":" << e.source().begin.column << ": " << e.description();
        throw Error(ss.str());
    }
    Config c;
    c.configDir = std::filesystem::absolute(file).parent_path();
    checkKeys(root, {"input", "output", "analysis", "stubs", "functions", "patches"}, "root");

    if (const toml::table* in = root["input"].as_table()) {
        checkKeys(*in, {"elf", "ghidra_map", "map_files"}, "input");
        c.elf = c.resolve(stringOr(*in, "elf", ""));
        if (auto g = stringOr(*in, "ghidra_map", ""); !g.empty()) c.functionMaps.push_back(c.resolve(g));
        for (const auto& m : stringList(*in, "map_files")) c.functionMaps.push_back(c.resolve(m));
    }
    if (c.elf.empty()) throw Error(file.string() + ": [input] elf is required");

    c.name = c.elf.stem().string();
    c.outDir = c.resolve("out/" + c.name);
    if (const toml::table* out = root["output"].as_table()) {
        checkKeys(*out, {"dir", "name", "mode", "files", "comments", "project", "runtime_dir", "extra_sources"}, "output");
        if (auto d = stringOr(*out, "dir", ""); !d.empty()) c.outDir = c.resolve(d);
        c.name = stringOr(*out, "name", c.name);
        const std::string mode = stringOr(*out, "mode", "multi");
        if (mode != "single" && mode != "multi") fail(*out->get("mode"), "mode must be \"single\" or \"multi\"");
        c.singleFile = mode == "single";
        if (const toml::node* f = out->get("files")) {
            auto v = f->value_exact<int64_t>();
            if (!v || *v < 0 || *v > 4096) fail(*f, "files must be an integer between 0 and 4096");
            c.files = static_cast<int>(*v);
        }
        c.comments = boolOr(*out, "comments", c.comments);
        c.project = boolOr(*out, "project", c.project);
        if (auto r = stringOr(*out, "runtime_dir", ""); !r.empty()) c.runtimeDir = c.resolve(r);
        for (const auto& e : stringList(*out, "extra_sources")) c.extraSources.push_back(c.resolve(e));
    }

    if (const toml::table* an = root["analysis"].as_table()) {
        checkKeys(*an, {"symbols", "jal_targets", "data_pointers", "prologues", "jump_tables", "preempt_loops", "split_gaps",
                        "entries", "entries_files", "functions"},
                  "analysis");
        c.useSymbols = boolOr(*an, "symbols", c.useSymbols);
        c.scanJalTargets = boolOr(*an, "jal_targets", c.scanJalTargets);
        c.scanDataPointers = boolOr(*an, "data_pointers", c.scanDataPointers);
        c.scanPrologues = boolOr(*an, "prologues", c.scanPrologues);
        c.jumpTables = boolOr(*an, "jump_tables", c.jumpTables);
        c.preemptLoops = boolOr(*an, "preempt_loops", c.preemptLoops);
        c.splitGaps = boolOr(*an, "split_gaps", c.splitGaps);
        if (const toml::array* e = (*an)["entries"].as_array())
            for (const auto& n : *e) c.entries.push_back(toAddress(n));
        for (const auto& f : stringList(*an, "entries_files")) c.entriesFiles.push_back(c.resolve(f));
        if (const toml::array* fns = (*an)["functions"].as_array()) {
            for (const auto& n : *fns) {
                const toml::table* ft = n.as_table();
                if (!ft) fail(n, "[[analysis.functions]] entries must be tables");
                checkKeys(*ft, {"name", "start", "end", "size"}, "analysis.functions");
                ExplicitFunction ef;
                const toml::node* start = ft->get("start");
                if (!start) fail(n, "[[analysis.functions]] needs a start address");
                ef.start = toAddress(*start);
                ef.name = stringOr(*ft, "name", "");
                if (const toml::node* e = ft->get("end")) ef.end = toAddress(*e);
                if (const toml::node* s = ft->get("size")) ef.end = ef.start + toAddress(*s);
                c.functions.push_back(ef);
            }
        }
    }

    if (const toml::table* st = root["stubs"].as_table()) {
        checkKeys(*st, {"auto", "bind", "exclude"}, "stubs");
        c.autoStubs = boolOr(*st, "auto", c.autoStubs);
        bindingList(*st, "bind", c.stubBindings);
        c.stubExclude = stringList(*st, "exclude");
    }

    if (const toml::table* fn = root["functions"].as_table()) {
        checkKeys(*fn, {"skip", "override"}, "functions");
        for (const auto& s : stringList(*fn, "skip")) c.skip.push_back(parseFunctionRef(s));
        bindingList(*fn, "override", c.overrides);
    }

    if (const toml::node* pn = root.get("patches")) {
        const toml::array* arr = pn->as_array();
        if (!arr) fail(*pn, "patches must be an array of tables ([[patches]])");
        for (const auto& n : *arr) {
            const toml::table* pt = n.as_table();
            if (!pt) fail(n, "[[patches]] entries must be tables");
            checkKeys(*pt, {"address", "value", "expect"}, "patches");
            Patch p;
            if (!pt->get("address") || !pt->get("value")) fail(n, "[[patches]] needs address and value");
            p.address = toAddress(*pt->get("address"));
            p.value = toAddress(*pt->get("value"));
            if (const toml::node* e = pt->get("expect")) p.expect = toAddress(*e);
            c.patches.push_back(p);
        }
    }
    return c;
}

} // namespace ps2uc
