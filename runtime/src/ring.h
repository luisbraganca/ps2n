// A ring of 64-bit words that carries entries from one host thread to
// another: a header (a kind, a payload size in quadwords and a 64-bit
// argument), then the payload. Entries are whole quadwords, so a quadword
// never wraps round the end of the ring.
//
// One thread writes and one reads. The writer waits while the ring is full,
// and when asked until the reader has carried out everything queued: an entry
// counts as done when the reader says so (done()), not when it starts on it.
// The reader waits while the ring is empty. Both spin briefly before they
// sleep. close() ends both sides' waits for good, dropping what is left (at
// exit).
#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>
#include <thread>

#if defined(_M_X64) || defined(__x86_64__)
#include <immintrin.h>
#endif

namespace ps2 {

class WordRing {
public:
    // `words`: a power of two. An entry's payload is at most maxQwords().
    explicit WordRing(uint64_t words) : words_(words) {}

    uint64_t maxQwords() const { return words_ / 32; }

    // ---- the writer ----
    // Queues an entry once there is room for it. False if the ring is closed:
    // the entry is dropped.
    bool put(uint32_t kind, uint64_t arg, const void* payload, uint64_t qwords) {
        if (!ring_) ring_.reset(new uint64_t[words_]);
        const uint64_t at = head_.load(std::memory_order_relaxed), words = 2 + 2 * qwords;
        if (at + words > words_ && !waitDone(at + words - words_)) return false;   // room the reader has freed
        const uint64_t mask = words_ - 1;
        ring_[at & mask] = kind | qwords << 32;
        ring_[(at + 1) & mask] = arg;
        const uint64_t start = (at + 2) & mask, first = std::min<uint64_t>(2 * qwords, words_ - start);
        const auto* bytes = static_cast<const uint8_t*>(payload);
        if (first) std::memcpy(&ring_[start], bytes, first * 8);
        if (2 * qwords > first) std::memcpy(&ring_[0], bytes + first * 8, (2 * qwords - first) * 8);
        head_.store(at + words);
        if (readerWaits_.load()) {
            wake_.fetch_add(1);
            wake_.notify_one();
        }
        return true;
    }
    // Whether the reader has carried out everything queued.
    bool idle() const { return tail_.load() == head_.load(std::memory_order_relaxed); }
    // Waits until it has. False if the ring was closed first.
    bool waitIdle() { return waitDone(head_.load(std::memory_order_relaxed)); }

    // ---- the reader ----
    struct Entry {
        uint32_t kind = 0;
        uint64_t arg = 0;
        uint64_t qwords = 0;
        uint64_t at = 0;   // where it starts in the ring
    };
    // The next entry, once there is one. False once the ring is closed.
    bool next(Entry& e) {
        const uint64_t at = tail_.load(std::memory_order_relaxed);
        while (head_.load() == at || closed_.load()) {
            if (closed_.load()) return false;
            if (spinUntil([&] { return head_.load() != at || closed_.load(); })) continue;
            const uint32_t seen = wake_.load();
            readerWaits_.store(true);
            if (head_.load() == at && !closed_.load()) wake_.wait(seen);
            readerWaits_.store(false);
        }
        const uint64_t mask = words_ - 1, h = ring_[at & mask];
        e.kind = static_cast<uint32_t>(h);
        e.qwords = h >> 32;
        e.arg = ring_[(at + 1) & mask];
        e.at = at;
        return true;
    }
    // Quadword `i` of an entry's payload: two words, which never wrap.
    const uint64_t* qword(const Entry& e, uint64_t i) const { return &ring_[(e.at + 2 + 2 * i) & (words_ - 1)]; }
    // The entry has been carried out: its room is free, and a writer waiting for it goes on.
    void done(const Entry& e) {
        const uint64_t end = e.at + 2 + 2 * e.qwords;
        tail_.store(end);
        if (end >= writerWaitsFor_.load()) {
            done_.fetch_add(1);
            done_.notify_one();
        }
    }

    // ---- either side ----
    // Ends both sides' waits for good: put() drops entries and next() returns false.
    void close() {
        closed_.store(true);
        wake_.fetch_add(1);
        wake_.notify_all();
        done_.fetch_add(1);
        done_.notify_all();
    }
    // While neither side uses it: opens a closed ring again, empty.
    void reopen() {
        tail_.store(head_.load());
        closed_.store(false);
    }

private:
    // Spins briefly (tens of microseconds) before a thread sleeps: the other side is usually close.
    template <typename F>
    static bool spinUntil(F ready) {
        for (int i = 0; i < 1024; ++i) {
            if (ready()) return true;
#if defined(_M_X64) || defined(__x86_64__)
            _mm_pause();
#else
            std::this_thread::yield();
#endif
        }
        return false;
    }

    // The writer: until the reader has carried out everything before word `pos`.
    bool waitDone(uint64_t pos) {
        while (tail_.load() < pos) {
            if (closed_.load()) return false;
            if (spinUntil([&] { return tail_.load() >= pos || closed_.load(); })) continue;
            const uint32_t seen = done_.load();
            writerWaitsFor_.store(pos);
            if (tail_.load() < pos && !closed_.load()) done_.wait(seen);
            writerWaitsFor_.store(UINT64_MAX);
        }
        return true;
    }

    const uint64_t words_;
    std::unique_ptr<uint64_t[]> ring_;
    std::atomic<uint64_t> head_{0};                      // words queued
    std::atomic<uint64_t> tail_{0};                      // words carried out
    std::atomic<uint32_t> wake_{0};                      // moves to wake the reader
    std::atomic<uint32_t> done_{0};                      // moves to wake the writer
    std::atomic<bool> readerWaits_{false};
    std::atomic<uint64_t> writerWaitsFor_{UINT64_MAX};   // the writer waits for tail_ to reach it
    std::atomic<bool> closed_{false};
};

} // namespace ps2
