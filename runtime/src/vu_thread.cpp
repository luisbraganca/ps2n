// VU1 on a thread of its own, as PCSX2's MTVU runs it (real-time runs).
//
// The guest thread queues what acts on VU1 in a ring: VIF1's writes to its
// data and micro memory (UNPACK, MPG) and the programs it starts (MSCAL,
// MSCALF, MSCNT, CTC2 to CMSAR1), each with VIF1's TOP and ITOP as its start
// latched them and the guest's rounding mode. The VU1 thread carries them out
// in order. The EE sees a program finished the moment it starts, as it did
// when the program ran on the guest thread; whatever reads or writes VU1's
// state from the guest thread (EE loads and stores in VU1 memory, VU0's
// window on VU1's registers, the resets) waits for the VU1 thread first.
//
// A program's XGKICK packets go to the GS thread in a ring of their own
// (gs::path1Kick). The guest queues each program in the GS thread's ring too
// (gs::path1Run), where the GS thread decodes the program's packets as they
// come: PATH1 keeps its place among the paths, as when the guest thread ran
// the program. So the VU1 thread runs only while the GS has a thread of its
// own.
#include "vu.h"

#include "hw/gs.h"
#include "log.h"
#include "ps2/ops_vu0.h"
#include "ring.h"

#include <algorithm>
#include <cfenv>
#include <cstring>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <process.h>
#include <windows.h>
#else
#include <pthread.h>
#endif

namespace ps2::vu {

bool g_vu1Threaded = false;

namespace {

// Writes `mask` names of quadword `qw` of VU1's data memory (bit 0: x).
void writeDataQw(uint32_t qw, uint32_t mask, const uint8_t* w) {
    uint8_t* p = vu1Mem + (qw & 0x3FF) * 16;
    if (mask == 0xF) {
        std::memcpy(p, w, 16);
        return;
    }
    for (uint32_t c = 0; c < 4; ++c)
        if ((mask >> c) & 1) std::memcpy(p + 4 * c, w + 4 * c, 4);
}

void writeMicroBytes(uint32_t at, const uint8_t* bytes, size_t n) {
    for (size_t i = 0; i < n; ++i) vu1Micro[(at + i) & 0x3FFF] = bytes[i];
}

class Vu1Thread {
public:
    enum Kind : uint32_t {
        Data,    // arg: the first quadword | the components written << 16; payload: the quadwords
        Micro,   // arg: the first byte | bytes << 32; payload: the bytes, in whole quadwords
        Run,     // arg: the program's number; payload: start | TOP << 32, ITOP | rounding mode << 32
    };

    ~Vu1Thread() {   // at exit, if it still runs: drops what is queued
        if (running_) stop(false);
    }

    WordRing& ring() { return ring_; }
    uint64_t nextProgram() { return ++programs_; }

    // Starts the thread with a large stack: recompiled micro programs are big
    // functions, run on the guest thread's stack otherwise. False if it cannot.
    bool start() {
        constexpr size_t kStack = size_t(64) << 20;
#if defined(_WIN32)
        handle_ = reinterpret_cast<HANDLE>(_beginthreadex(
            nullptr, static_cast<unsigned>(kStack),
            [](void* p) -> unsigned {
                static_cast<Vu1Thread*>(p)->work();
                return 0;
            },
            this, STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr));
        running_ = handle_ != nullptr;
#else
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setstacksize(&attr, kStack);
        running_ = pthread_create(&thread_, &attr, [](void* p) -> void* {
            static_cast<Vu1Thread*>(p)->work();
            return nullptr;
        }, this) == 0;
        pthread_attr_destroy(&attr);
#endif
        return running_;
    }

    // Ends the thread. `finish`: once it has carried out everything queued;
    // otherwise (at exit) what is queued is dropped, and so is what the GS
    // thread still waits for.
    void stop(bool finish) {
        if (finish) ring_.waitIdle();
        ring_.close();
        if (!finish) gs::path1Close();
#if defined(_WIN32)
        WaitForSingleObject(handle_, INFINITE);
        CloseHandle(handle_);
        handle_ = nullptr;
#else
        pthread_join(thread_, nullptr);
#endif
        running_ = false;
        ring_.reopen();
    }

private:
    void work() {
        int rounding = std::fegetround();
        WordRing::Entry e;
        while (ring_.next(e)) {
            switch (e.kind) {
            case Data: {
                const auto qw = static_cast<uint32_t>(e.arg & 0x3FF), mask = static_cast<uint32_t>(e.arg >> 16) & 0xF;
                for (uint64_t i = 0; i < e.qwords; ++i)
                    writeDataQw(qw + static_cast<uint32_t>(i), mask, reinterpret_cast<const uint8_t*>(ring_.qword(e, i)));
                break;
            }
            case Micro: {
                const auto at = static_cast<uint32_t>(e.arg);
                const auto bytes = static_cast<size_t>(e.arg >> 32);
                for (uint64_t i = 0; i < e.qwords; ++i) {
                    const size_t from = 16 * i;
                    writeMicroBytes(at + static_cast<uint32_t>(from), reinterpret_cast<const uint8_t*>(ring_.qword(e, i)),
                                    std::min<size_t>(16, bytes - from));
                }
                microChanged(1);
                break;
            }
            case Run: {
                const uint64_t* q = ring_.qword(e, 0);
                const auto mode = static_cast<int>(q[1] >> 32);
                if (mode != rounding) std::fesetround(rounding = mode);
                runVu1(static_cast<uint32_t>(q[0]), static_cast<uint32_t>(q[0] >> 32), static_cast<uint32_t>(q[1]), true);
                gs::path1End(e.arg);
                break;
            }
            default: break;
            }
            ring_.done(e);
        }
    }

    WordRing ring_{uint64_t(1) << 21};   // 16 MB
    uint64_t programs_ = 0;              // programs queued (guest thread)
    bool running_ = false;
#if defined(_WIN32)
    HANDLE handle_ = nullptr;
#else
    pthread_t thread_{};
#endif
};

// Constructed on first use, so destroyed before the GS thread and its rings
// (namespace-scope objects of gs.cpp, constructed before main).
Vu1Thread& vu1Thread() {
    static Vu1Thread t;
    return t;
}

bool g_wanted = false;   // setOwnThread
bool g_gsThread = false;   // the GS has a thread of its own

void apply() {
    const bool on = g_wanted && g_gsThread;
    if (on == g_vu1Threaded) return;
    Vu1Thread& t = vu1Thread();
    if (!on) {
        t.stop(true);
        g_vu1Threaded = false;
        return;
    }
    g_vu1Threaded = t.start();
    if (!g_vu1Threaded) PS2_WARN("vu", "could not start the VU1 thread; VU1 runs on the guest thread");
}

} // namespace

void setOwnThread(bool on) {
    g_wanted = on;
    g_gsThread = gs::ownThread();
    apply();
}

void updateOwnThread(bool gsThread) {
    g_gsThread = gsThread;
    apply();
}

void syncThread() { vu1Thread().ring().waitIdle(); }

void startVu1(uint32_t startByte, uint32_t top, uint32_t itop) {
    if (!g_vu1Threaded) return runVu1(startByte, top, itop, false);
    Vu1Thread& t = vu1Thread();
    const uint64_t program = t.nextProgram();
    const uint64_t q[2] = {startByte | static_cast<uint64_t>(top) << 32,
                           itop | static_cast<uint64_t>(static_cast<uint32_t>(std::fegetround())) << 32};
    t.ring().put(Vu1Thread::Run, program, q, 1);
    gs::path1Run(program);
}

void writeVu1Data(uint32_t qw, uint32_t mask, const uint32_t* words, size_t qwords) {
    if (!g_vu1Threaded) {
        for (size_t i = 0; i < qwords; ++i) writeDataQw(qw + static_cast<uint32_t>(i), mask, reinterpret_cast<const uint8_t*>(words + 4 * i));
        return;
    }
    WordRing& ring = vu1Thread().ring();
    while (qwords) {   // in parts the ring takes
        const size_t n = std::min<size_t>(qwords, ring.maxQwords());
        ring.put(Vu1Thread::Data, (qw & 0x3FF) | static_cast<uint64_t>(mask & 0xF) << 16, words, n);
        qw += static_cast<uint32_t>(n);
        words += 4 * n;
        qwords -= n;
    }
}

void writeVu1Micro(uint32_t at, const uint8_t* bytes, size_t n) {
    if (!g_vu1Threaded) {
        writeMicroBytes(at, bytes, n);
        microChanged(1);
        return;
    }
    // In whole quadwords, at most an MPG's 2 KB at a time.
    uint64_t q[2 * 128];
    while (n) {
        const size_t part = std::min<size_t>(n, sizeof q);
        std::memset(q, 0, sizeof q);
        std::memcpy(q, bytes, part);
        vu1Thread().ring().put(Vu1Thread::Micro, at | static_cast<uint64_t>(part) << 32, q, (part + 15) / 16);
        at += static_cast<uint32_t>(part);
        bytes += part;
        n -= part;
    }
}

} // namespace ps2::vu
