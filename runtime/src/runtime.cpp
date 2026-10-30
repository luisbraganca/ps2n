// ps2::run(): command line, ELF loading, subsystem start-up and the main
// dispatch loop.
#include "audio/audio.h"
#include "clock.h"
#include "hw/dmac.h"
#include "hw/gs.h"
#include "hw/hw.h"
#include "hw/ipu.h"
#include "hw/vif.h"
#include "input/input.h"
#include "internal.h"
#include "iop/hostfs.h"
#include "iop/iop.h"
#include "kernel/kernel.h"
#include "log.h"
#include "settings.h"
#include "video/video.h"

#include "ps2n/elf.h"
#include "ps2n/util.h"

#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#include <process.h>
#include <windows.h>
#else
#include <pthread.h>
#endif

namespace ps2 {

using ps2n::ElfFile;
using ps2n::ElfSegment;
namespace elf = ps2n::elf;

Options g_opt;

std::string_view romVersion() {
    // BIOS 1.60, console type C (retail), dated 2001-07-04; the region comes from --region.
    static char romver[] = "0160AC20010704\n";
    romver[4] = g_opt.region;
    return {romver, sizeof romver - 1};
}

volatile std::sig_atomic_t g_interruptRequested = 0;

namespace {

// Ctrl+C: stop at the next safepoint so the exit summary still prints.
extern "C" void onInterruptSignal(int) { g_interruptRequested = 1; }

constexpr const char* kUsage =
    "usage: %s [options] [-- guest args...]\n"
    "\n"
    "  --elf PATH            ELF to load data from (default: the one recompiled)\n"
    "  --host0 DIR           directory behind host:/host0: (default: .)\n"
    "  --cdrom PATH          the disc: an ISO image or a directory\n"
    "  --memcards DIR        memory cards: DIR/mc0 and DIR/mc1 (default: memcards)\n"
    "  --interpret           ignore recompiled code; interpret everything\n"
    "  --virtual-clock       deterministic time (advances with execution)\n"
    "  --no-throttle         skip idle time instead of sleeping\n"
    "  --max-vsyncs N        exit after N vertical blanks\n"
    "  --dump-frames DIR     write displayed frames as PPM files\n"
    "  --dump-every N        only dump every Nth frame\n"
    "  --dump-audio FILE     write the sound as a 48 kHz stereo WAV file\n"
    "  --no-window           do not open a window (SDL builds)\n"
    "  --deinterlace MODE    adaptive (default), weave or bob, for games that\n"
    "                        show interlaced fields\n"
    "  --gs-threads N        threads that draw (default 0: half the host's, at\n"
    "                        most 8); 1 draws on the guest thread alone\n"
    "  --fpu-round MODE      zero (default, like the EE) or nearest\n"
    "  --strict              stop at the first unimplemented path (exit code 4)\n"
    "  --trace-syscalls      log every syscall\n"
    "  --trace-functions     log every dispatched function\n"
    "  --log-level LEVEL     error, warn, info (default), debug, trace\n"
    "  --pad-script FILE     controller input from a script instead of the keyboard\n"
    "                        and game controllers\n"
    "  --settings FILE       controller bindings and hotkeys (default: ps2n.ini next to\n"
    "                        the executable, else the per-user one)\n"
    "  --region REGION       console region: usa (default), europe, japan, asia, china\n"
    "  --missing-out FILE    where to record interpreted entry points\n"
    "                        (default: ps2n_missing.toml; empty disables)\n";

bool parseOptions(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto value = [&]() -> std::string {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "%s needs a value\n", a.c_str());
                std::exit(2);
            }
            return argv[++i];
        };
        if (a == "--") {
            for (++i; i < argc; ++i) g_opt.guestArgs.emplace_back(argv[i]);
            break;
        }
        if (a == "-h" || a == "--help") {
            std::printf(kUsage, argv[0]);
            return false;
        }
        if (a == "--elf") g_opt.elfPath = value();
        else if (a == "--host0") g_opt.host0Dir = value();
        else if (a == "--cdrom") g_opt.cdrom = value();
        else if (a == "--memcards") g_opt.memcardDir = value();
        else if (a == "--interpret") g_opt.interpretOnly = true;
        else if (a == "--virtual-clock") g_opt.virtualClock = true;
        else if (a == "--no-throttle") g_opt.noThrottle = true;
        else if (a == "--max-vsyncs") g_opt.maxVsyncs = std::strtoull(value().c_str(), nullptr, 0);
        else if (a == "--dump-frames") g_opt.dumpFrames = value();
        else if (a == "--dump-every") g_opt.dumpEvery = static_cast<uint32_t>(std::strtoul(value().c_str(), nullptr, 0));
        else if (a == "--dump-audio") g_opt.dumpAudio = value();
        else if (a == "--no-window") g_opt.window = false;
        else if (a == "--gs-threads") gs::setDrawThreads(static_cast<int>(std::strtol(value().c_str(), nullptr, 0)));
        else if (a == "--deinterlace") {
            const std::string m = ps2n::toLower(value());
            if (m == "adaptive") gs::setDeinterlace(gs::Deinterlace::Adaptive);
            else if (m == "weave") gs::setDeinterlace(gs::Deinterlace::Weave);
            else if (m == "bob") gs::setDeinterlace(gs::Deinterlace::Bob);
            else {
                std::fprintf(stderr, "unknown deinterlace mode %s (adaptive, weave or bob)\n", m.c_str());
                std::exit(2);
            }
        }
        else if (a == "--fpu-round") g_opt.fpuRoundToZero = value() != "nearest";
        else if (a == "--strict") g_opt.strict = true;
        else if (a == "--trace-syscalls") g_opt.traceSyscalls = true;
        else if (a == "--trace-functions") g_opt.traceFunctions = true;
        else if (a == "--missing-out") g_opt.missingOut = value();
        else if (a == "--pad-script") g_opt.padScript = value();
        else if (a == "--settings") g_opt.settingsFile = value();
        else if (a == "--region") {
            const std::string r = ps2n::toLower(value());
            if (r == "usa" || r == "us" || r == "a") g_opt.region = 'A';
            else if (r == "europe" || r == "eu" || r == "e") g_opt.region = 'E';
            else if (r == "japan" || r == "jp" || r == "j") g_opt.region = 'J';
            else if (r == "asia" || r == "h") g_opt.region = 'H';
            else if (r == "china" || r == "c") g_opt.region = 'C';
            else {
                std::fprintf(stderr, "unknown region %s (usa, europe, japan, asia or china)\n", r.c_str());
                std::exit(2);
            }
        }
        else if (a == "--log-level") {
            const std::string l = value();
            g_logLevel = l == "error" ? LogLevel::Error : l == "warn" ? LogLevel::Warn : l == "debug" ? LogLevel::Debug
                       : l == "trace" ? LogLevel::Trace : LogLevel::Info;
        } else {
            std::fprintf(stderr, "unknown option %s (see --help)\n", a.c_str());
            std::exit(2);
        }
    }
    return true;
}

// The folder of the running executable, where a portable ps2n.ini lives.
std::filesystem::path executableDir(const char* argv0) {
#if defined(_WIN32)
    (void)argv0;
    std::wstring buf(MAX_PATH, L'\0');
    for (;;) {
        const DWORD n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (n == 0) return {};
        if (n < buf.size()) return std::filesystem::path(buf.substr(0, n)).parent_path();
        buf.resize(buf.size() * 2);
    }
#else
    std::error_code ec;
    return std::filesystem::absolute(argv0, ec).parent_path();
#endif
}

std::filesystem::path findElf(const char* argv0, const GameInfo& game) {
    if (!g_opt.elfPath.empty()) return g_opt.elfPath;
    std::filesystem::path p = game.defaultElf ? game.defaultElf : "";
    std::error_code ec;
    if (std::filesystem::exists(p, ec)) return p;
    const auto besideExe = std::filesystem::path(argv0).parent_path() / p.filename();
    if (std::filesystem::exists(besideExe, ec)) return besideExe;
    return p;
}

// Whether every loadable segment of the ELF fits in RAM.
bool fitsInRam(const ElfFile& image) {
    for (const ElfSegment& s : image.segments) {
        if (s.type != elf::PT_LOAD) continue;
        const uint32_t phys = toPhysical(s.vaddr);
        if (phys >= kRamSize || s.memsz > kRamSize - phys || s.filesz > s.memsz) {
            PS2_ERROR("runtime", "%s: segment at 0x%08x (0x%x bytes) does not fit in RAM", image.name().c_str(), s.vaddr, s.memsz);
            return false;
        }
    }
    return true;
}

// Copies the ELF's segments into RAM, then applies the program's [[patches]]:
// the recompiled code has them built in, RAM needs them for the interpreter
// and for code that reads its own instructions.
void loadImage(const ElfFile& image, const GameInfo* program) {
    for (const ElfSegment& s : image.segments) {
        if (s.type != elf::PT_LOAD) continue;
        const uint32_t phys = toPhysical(s.vaddr);
        std::memcpy(ram + phys, image.bytes().data() + s.offset, s.filesz);
        std::memset(ram + phys + s.filesz, 0, s.memsz - s.filesz);
    }
    if (!program) return;
    for (size_t i = 0; i < program->patchCount; ++i) write32(program->patches[i].address, program->patches[i].value);
    if (program->patchCount) PS2_INFO("runtime", "applied %zu instruction patch(es) from the config", program->patchCount);
}

bool loadElf(const GameInfo& game) {
    try {
        const ElfFile image = ElfFile::load(g_opt.elfPath);
        if (image.crc32() != game.elfCrc32)
            PS2_WARN("runtime", "%s has CRC32 %08x but the code was recompiled from a file with CRC32 %08x; "
                     "expect mismatches unless you are sure the data is compatible",
                     g_opt.elfPath.string().c_str(), image.crc32(), game.elfCrc32);
        if (image.entry != game.entry)
            PS2_WARN("runtime", "ELF entry point 0x%08x differs from the recompiled one (0x%08x)", image.entry, game.entry);
        if (!fitsInRam(image)) return false;
        PS2_INFO("runtime", "loaded %s (entry 0x%08x)", g_opt.elfPath.string().c_str(), image.entry);
        loadImage(image, &game);
        return true;
    } catch (const std::exception& e) {
        PS2_ERROR("runtime", "cannot load ELF %s: %s", g_opt.elfPath.string().c_str(), e.what());
        return false;
    }
}

// The boot program's arguments, as SetupThread hands them over.
std::vector<std::string> bootArgv() {
    std::vector<std::string> argv{"host:" + g_opt.elfPath.filename().string()};
    argv.insert(argv.end(), g_opt.guestArgs.begin(), g_opt.guestArgs.end());
    return argv;
}

// A LoadExecPS2 waiting for the old program's host frames to unwind.
struct PendingExec {
    ElfFile image;
    const GameInfo* program;            // null: no recompiled code for it
    std::string name;                   // its file name, for logs and its missing-entries file
    std::vector<std::string> argv;
};
std::optional<PendingExec> g_pendingExec;

// "cdrom0:\DATA\MAIN.ELF;1" -> "MAIN.ELF"
std::string fileNameOf(std::string_view path) {
    if (const auto semi = path.rfind(';'); semi != std::string_view::npos) path = path.substr(0, semi);
    if (const auto cut = path.find_last_of(":\\/"); cut != std::string_view::npos) path = path.substr(cut + 1);
    return std::string(path);
}

void mainLoop() {
    applyGuestFloatEnvironment();
    while (!g_exitRequested) {
        if (g_interruptRequested) {
            PS2_INFO("runtime", "interrupted");
            requestExit(130);
            break;
        }
        if (!kernel::hasRunningThread()) {
            kernel::idle();
            continue;
        }
        dispatch();
    }
}

// Recursion in guest code becomes recursion in host code, and unoptimised
// builds of large generated functions use big frames, so the guest runs on a
// thread with a generous stack. The main thread runs the window meanwhile
// (macOS only allows windows and events there).
struct GuestThread {
    void (*fn)();
    std::atomic<bool> done{false};
};

void runGuestThread(void (*fn)()) {
    constexpr size_t kStack = size_t(512) << 20;
    GuestThread guest{fn};
#if defined(_WIN32)
    HANDLE h = reinterpret_cast<HANDLE>(_beginthreadex(
        nullptr, static_cast<unsigned>(kStack),
        [](void* p) -> unsigned {
            auto* g = static_cast<GuestThread*>(p);
            g->fn();
            g->done = true;
            return 0;
        },
        &guest, STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr));
    if (!h) {
        PS2_WARN("runtime", "could not create a large-stack thread; running on the main thread without a window");
        fn();
        return;
    }
    video::hostLoop(guest.done);
    WaitForSingleObject(h, INFINITE);
    CloseHandle(h);
#else
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, kStack);
    pthread_t t;
    if (pthread_create(&t, &attr, [](void* p) -> void* {
            auto* g = static_cast<GuestThread*>(p);
            g->fn();
            g->done = true;
            return nullptr;
        }, &guest) != 0) {
        PS2_WARN("runtime", "could not create a large-stack thread; running on the main thread without a window");
        fn();
    } else {
        video::hostLoop(guest.done);
        pthread_join(t, nullptr);
    }
    pthread_attr_destroy(&attr);
#endif
}

} // namespace

bool requestExec(std::vector<std::string> argv) {
    const std::string path = argv[0];
    std::vector<uint8_t> bytes;
    if (!iop::hostfs::readWholeFile(path, bytes)) {
        PS2_ERROR("runtime", "LoadExecPS2(\"%s\"): cannot read the file (see --cdrom and --host0)", path.c_str());
        return false;
    }
    std::string name = fileNameOf(path);
    try {
        ElfFile image = ElfFile::parse(std::move(bytes), name);
        if (image.type != elf::ET_EXEC || !fitsInRam(image)) {
            PS2_ERROR("runtime", "LoadExecPS2(\"%s\"): not an EE executable", path.c_str());
            return false;
        }
        // Recompiled code runs only for the very file it was made from.
        const GameInfo* program = nullptr;
        bool sameName = false;
        for (size_t i = 0; i <= g_game->programCount && !program; ++i) {
            const GameInfo* p = i == 0 ? g_game : g_game->programs[i - 1];
            if (p->elfCrc32 == image.crc32()) program = p;
            else if (ps2n::toLower(std::filesystem::path(p->defaultElf).filename().string()) == ps2n::toLower(name)) sameName = true;
        }
        if (!program && sameName)
            PS2_FALLBACK("runtime", "interpreted program " + name,
                         "LoadExecPS2(\"%s\"): %s is not the file its code was recompiled from; interpreting it", path.c_str(), name.c_str());
        else if (!program)
            PS2_FALLBACK("runtime", "interpreted program " + name,
                         "LoadExecPS2(\"%s\"): %s has no recompiled code; interpreting it (list it in [input] programs and recompile)",
                         path.c_str(), name.c_str());
        g_pendingExec = PendingExec{std::move(image), program, std::move(name), std::move(argv)};
    } catch (const std::exception& e) {
        PS2_ERROR("runtime", "LoadExecPS2(\"%s\"): %s", path.c_str(), e.what());
        return false;
    }
    cpu.pc = kTrampExec;   // every host frame unwinds; the dispatcher then calls startPendingProgram()
    return true;
}

void startPendingProgram() {
    if (!g_pendingExec) {
        PS2_ERROR("runtime", "reached the LoadExecPS2 trampoline with no program to start");
        requestExit(1);
        return;
    }
    PendingExec next = std::move(*g_pendingExec);
    g_pendingExec.reset();
    PS2_INFO("runtime", "LoadExecPS2: starting %s (%s), entry 0x%08x", next.name.c_str(), next.program ? "recompiled" : "interpreted",
             next.image.entry);
    // As on the console, the kernel drops the old program's threads, handlers
    // and pending interrupts, and the BIOS reboots the IOP before it loads the
    // next executable. Memory, the GS and the VUs keep their contents.
    hw::resetInterrupts();
    iop::reboot();
    loadImage(next.image, next.program);
    cpu = Cpu{};
    initCop0();
    programLoaded(next.program, next.name);
    kernel::init(next.image.entry, std::move(next.argv));
    ++g_switchGeneration;
}

int run(int argc, char** argv, const GameInfo& game) {
#if defined(_WIN32)
    // Guest output is a byte stream; text mode would turn every "\n" into "\r\n".
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    if (!parseOptions(argc, argv)) return 0;
    g_opt.elfPath = findElf(argc > 0 ? argv[0] : "", game);
    PS2_INFO("runtime", "%s, recompiled by %s: %zu functions", game.name, game.generator, game.functionCount);

    clockInit(g_opt.virtualClock, g_opt.noThrottle);
    if (!loadElf(game)) return 1;
    initCop0();
    hw::init();
    gs::init();
    // The GS draws on its own thread, except where runs must repeat exactly
    // or stop where a gap is.
    gs::setOwnThread(!g_opt.virtualClock && !g_opt.strict);
    dmac::init();
    vif::init();
    ipu::init();
    iop::init();
    // Bindings come from the settings file when someone is there to press
    // them; headless runs (the tests) keep the defaults whatever the file says.
    if (g_opt.window || !g_opt.settingsFile.empty())
        input::init(settings::locate(executableDir(argc > 0 ? argv[0] : ""), g_opt.settingsFile));
    video::init();
    audio::init();
    initDispatch(game);
    kernel::init(game.entry, bootArgv());
    hw::startVsync();

    std::signal(SIGINT, onInterruptSignal);
    runGuestThread(&mainLoop);
    std::signal(SIGINT, SIG_DFL);
    gs::shutdown();

    writeMissingEntries();
    printIssueSummary();
    audio::shutdown();
    video::shutdown();
    PS2_INFO("runtime", "exit code %d", g_exitCode);
    return g_exitCode;
}

} // namespace ps2
