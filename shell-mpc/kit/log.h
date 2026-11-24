// mpckit - a developer log for plugins that run inside a host you cannot attach a debugger to (the Akai MPC application). Header only, C++17, POSIX, no dependency on anything else.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
//   MPCKIT_LOG ("startup", info, "engine built in %.1f ms", ms);      // any thread, the audio thread included
//   MPCKIT_SCOPE ("startup", "engine construct");                     // logs the duration of the enclosing scope at its end (level info)
//   MPCKIT_LOG_START ("myplugin");                                    // once, from a thread that may allocate (the plugin's constructor): /tmp/myplugin-dev.log, /tmp/myplugin-log.conf
//
// COMPILE TIME: one macro, MPCKIT_DEV_LOG (the shell's CMake option SYNTHYBOY_MPC_DEV_LOG maps to it). 0 (the default of this header when nothing defines it): every macro above expands to
// `do {} while (0)` and their arguments are NOT evaluated (not even compiled: they are dropped by the preprocessor), so a release binary holds no log string, no thread, no file, no symbol of this header.
// 1: the log exists but every category is OFF until the control file switches it on.
//
// RUN TIME (dev builds): the control file /tmp/<plugin>-log.conf, lines `category=level` (levels: off error warn info debug trace; `all=` is the default of every category that has no line of its own;
// `#` starts a comment), e.g.  all=info   or   startup=debug / q=trace. The writer thread reads it at start and about once a second (never the audio thread): create, edit or delete the file while the
// host runs. No file: everything off.
//
// REAL TIME: MPCKIT_LOG on any thread is: one relaxed load per registered category, a formatting of the line into a fixed record (vsnprintf into 176 bytes, no allocation) and a lock free publish into a
// bounded multi producer ring (D. Vyukov's queue: a compare and swap on the head, no lock, no system call except the monotonic clock). A full ring drops the record and counts it (the writer logs the
// count). Nothing allocates, locks or does I/O on the calling thread. The thread tag is the low bits of pthread_self () (no thread local storage: its first access in a dlopen'ed library may allocate).
//
// OUTPUT: a background thread drains the ring into /tmp/<plugin>-dev.log, "<ms since start> <level> <thread> <category> <text>" per line. The file is capped (kMaxFileBytes, 2 MB): when the next block
// would exceed it the file is renamed to <file>.1 (the old one is replaced) and a new one begins, so the disk use is at most twice the cap. The file is opened O_NOFOLLOW | O_NONBLOCK and used only
// when it is a regular file (a FIFO or a symlink left in /tmp is ignored). Nothing is written while no category is on.
#pragma once

#ifndef MPCKIT_DEV_LOG
#define MPCKIT_DEV_LOG 0
#endif

#if MPCKIT_DEV_LOG

#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

#include <fcntl.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace mpckit
{
enum class Level : int { off = 0, error = 1, warn = 2, info = 3, debug = 4, trace = 5 };

namespace devlog
{
constexpr size_t kRingSlots = 1024;        // records between two drains of the writer (it drains every 50 ms)
constexpr size_t kTextBytes = 176;         // longer lines are cut
constexpr int kMaxCategories = 32;         // categories with a line of their own in the control file
constexpr size_t kCategoryName = 24;
constexpr size_t kMaxFileBytes = 2u * 1024u * 1024u;

inline const char* levelName (Level l) noexcept
{
    switch (l)
    {
        case Level::error: return "ERR";
        case Level::warn:  return "WRN";
        case Level::info:  return "INF";
        case Level::debug: return "DBG";
        case Level::trace: return "TRC";
        default:           return "---";
    }
}

inline uint32_t hashOf (const char* s) noexcept
{
    uint32_t h = 2166136261u;
    for (; *s != '\0'; ++s)
        h = (h ^ static_cast<unsigned char> (*s)) * 16777619u;
    return h == 0u ? 1u : h;   // 0 marks a free entry
}

struct Settings
{
    std::string logPath, confPath;
    size_t capBytes = kMaxFileBytes;
    bool thread = true;          // false: no writer thread (tests drain by hand with Logger::drainNow)
};

class Logger
{
public:
    Logger() : origin_ (std::chrono::steady_clock::now())
    {
        for (size_t i = 0; i < kRingSlots; ++i)
            ring_[i].seq.store (i, std::memory_order_relaxed);
        for (auto& c : cat_)
        {
            c.hash.store (0u, std::memory_order_relaxed);
            c.level.store (0, std::memory_order_relaxed);
        }
    }
    ~Logger() { stop(); }
    Logger (const Logger&) = delete;
    Logger& operator= (const Logger&) = delete;

    /** Non real time (allocates, starts the writer thread). The second call does nothing until stop (). */
    void start (const Settings& s)
    {
        std::lock_guard<std::mutex> g (lifeMutex_);
        if (running_)
            return;
        settings_ = s;
        bytes_ = 0;
        fd_ = -1;
        stopFlag_ = false;
        readConfig();
        running_ = true;
        if (s.thread)
            thread_ = std::thread ([this] { writerLoop(); });
    }

    void stop() noexcept
    {
        std::lock_guard<std::mutex> g (lifeMutex_);
        if (! running_)
            return;
        {
            std::lock_guard<std::mutex> w (wakeMutex_);
            stopFlag_ = true;
        }
        wake_.notify_all();
        if (thread_.joinable())
            thread_.join();
        drainNow();
        if (fd_ >= 0)
            ::close (fd_);
        fd_ = -1;
        running_ = false;
    }

    /** True when a record of `category` at `level` is wanted. Any thread: relaxed atomic loads of a table that only the writer thread fills. */
    bool enabled (const char* category, Level level) const noexcept
    {
        const int want = static_cast<int> (level);
        const uint32_t h = hashOf (category);
        for (int i = 0; i < kMaxCategories; ++i)
        {
            const uint32_t eh = cat_[i].hash.load (std::memory_order_acquire);
            if (eh == 0u)
                break;
            if (eh == h)
                return want <= cat_[i].level.load (std::memory_order_relaxed);
        }
        return want <= allLevel_.load (std::memory_order_relaxed);
    }

    /** Any thread: formats and publishes one record. No allocation, no lock, no I/O. */
    void write (const char* category, Level level, const char* fmt, va_list ap) noexcept __attribute__ ((format (printf, 4, 0)))
    {
        size_t pos = 0;
        Slot* s = claim (pos);
        if (s == nullptr)
        {
            dropped_.fetch_add (1, std::memory_order_relaxed);
            return;
        }
        s->ns = std::chrono::duration_cast<std::chrono::nanoseconds> (std::chrono::steady_clock::now() - origin_).count();
        s->level = static_cast<uint8_t> (level);
        s->thread = static_cast<uint16_t> ((uintptr_t) (pthread_self()) >> 4)   /* pthread_t is a pointer on macOS and an integer number on glibc: the C cast takes both */;
        std::strncpy (s->category, category, kCategoryName - 1);
        s->category[kCategoryName - 1] = '\0';
        std::vsnprintf (s->text, kTextBytes, fmt, ap);
        s->seq.store (pos + 1, std::memory_order_release);   // published
    }

    /** The same with the arguments of a printf: any thread, no allocation, no lock, no I/O. */
    void writef (const char* category, Level level, const char* fmt, ...) noexcept __attribute__ ((format (printf, 4, 5)))
    {
        va_list ap;
        va_start (ap, fmt);
        write (category, level, fmt, ap);
        va_end (ap);
    }

    /** Milliseconds since this logger was made (the clock of the records). */
    double elapsedMs() const noexcept
    {
        return std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - origin_).count();
    }

    unsigned long droppedTotal() const noexcept { return droppedTotal_.load (std::memory_order_relaxed) + dropped_.load (std::memory_order_relaxed); }
    unsigned long long bytesWritten() const noexcept { return bytesTotal_.load (std::memory_order_relaxed); }

    /** The writer's work, also callable by hand when the thread is off (tests): drains the ring into the file and looks at the control file. Not for the audio thread. */
    void drainNow() noexcept
    {
        std::lock_guard<std::mutex> g (drainMutex_);
        try { drainLocked(); } catch (...) {}
    }
    /** Reads the control file now (the writer does it about once a second). */
    void refreshConfig() noexcept
    {
        std::lock_guard<std::mutex> g (drainMutex_);
        readConfig();
    }

private:
    struct Slot
    {
        std::atomic<size_t> seq { 0 };
        long long ns = 0;
        uint8_t level = 0;
        uint16_t thread = 0;
        char category[kCategoryName] = {};
        char text[kTextBytes] = {};
    };
    struct Category
    {
        std::atomic<uint32_t> hash { 0u };
        std::atomic<int> level { 0 };
    };

    Slot* claim (size_t& pos) noexcept
    {
        size_t head = head_.load (std::memory_order_relaxed);
        for (;;)
        {
            Slot& slot = ring_[head % kRingSlots];
            const size_t seq = slot.seq.load (std::memory_order_acquire);
            if (seq == head)
            {
                if (head_.compare_exchange_weak (head, head + 1, std::memory_order_relaxed))
                {
                    pos = head;
                    return &slot;
                }
            }
            else if (seq < head)
                return nullptr;   // the slot of the previous lap is not free yet: the ring is full
            else
                head = head_.load (std::memory_order_relaxed);
        }
    }

    static bool parseLevel (const char* s, size_t n, int& out) noexcept
    {
        struct Name { const char* text; int value; };
        static const Name kNames[] = { { "off", 0 }, { "error", 1 }, { "warn", 2 }, { "info", 3 }, { "debug", 4 }, { "trace", 5 } };
        for (const Name& nm : kNames)
            if (std::strlen (nm.text) == n && std::strncmp (nm.text, s, n) == 0)
            {
                out = nm.value;
                return true;
            }
        return false;
    }

    /** Reads the control file and applies it: the named categories get their level, every other category (and every one that was named before and no longer is) follows `all`. The new values are
        stored one by one, never through a state below the final one: a reader on another thread sees the old or the new level of a category. */
    void readConfig() noexcept
    {
        char buf[1024];
        size_t got = 0;
        if (! settings_.confPath.empty())
        {
            const int fd = ::open (settings_.confPath.c_str(), O_RDONLY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC);
            if (fd >= 0)
            {
                struct stat st;
                if (::fstat (fd, &st) == 0 && S_ISREG (st.st_mode))
                {
                    while (got + 1 < sizeof buf)
                    {
                        const ssize_t r = ::read (fd, buf + got, sizeof buf - 1 - got);
                        if (r > 0) got += static_cast<size_t> (r);
                        else if (r < 0 && errno == EINTR) continue;
                        else break;
                    }
                }
                ::close (fd);
            }
        }
        buf[got] = '\0';
        struct Named { char name[kCategoryName]; int level; };
        Named named[kMaxCategories];
        int count = 0, allValue = 0;
        const char* p = buf;
        while (*p != '\0')
        {
            const char* eol = std::strchr (p, '\n');
            const size_t len = eol != nullptr ? static_cast<size_t> (eol - p) : std::strlen (p);
            const char* eq = static_cast<const char*> (std::memchr (p, '=', len));
            if (p[0] != '#' && eq != nullptr)
            {
                size_t a = 0, b = static_cast<size_t> (eq - p);
                while (a < b && (p[a] == ' ' || p[a] == '\t')) ++a;
                while (b > a && (p[b - 1] == ' ' || p[b - 1] == '\t')) --b;
                size_t c = static_cast<size_t> (eq - p) + 1, d = len;
                while (c < d && (p[c] == ' ' || p[c] == '\t')) ++c;
                while (d > c && (p[d - 1] == ' ' || p[d - 1] == '\t' || p[d - 1] == '\r')) --d;
                int level = 0;
                if (b > a && parseLevel (p + c, d - c, level))
                {
                    if (b - a == 3 && std::strncmp (p + a, "all", 3) == 0)
                        allValue = level;
                    else if (count < kMaxCategories)
                    {
                        std::memset (named[count].name, 0, kCategoryName);
                        std::memcpy (named[count].name, p + a, b - a < kCategoryName - 1 ? b - a : kCategoryName - 1);
                        named[count].level = level;
                        ++count;
                    }
                }
            }
            if (eol == nullptr)
                break;
            p = eol + 1;
        }
        // the categories already in the table: the named level, or `all`
        for (auto& c : cat_)
        {
            const uint32_t h = c.hash.load (std::memory_order_relaxed);
            if (h == 0u)
                break;
            int level = allValue;
            for (int i = 0; i < count; ++i)
                if (hashOf (named[i].name) == h)
                    level = named[i].level;
            c.level.store (level, std::memory_order_relaxed);
        }
        // the named ones that are not in the table yet: appended (the level first, then the hash that publishes the entry)
        for (int i = 0; i < count; ++i)
        {
            const uint32_t h = hashOf (named[i].name);
            for (auto& c : cat_)
            {
                const uint32_t eh = c.hash.load (std::memory_order_relaxed);
                if (eh == h)
                    break;
                if (eh == 0u)
                {
                    c.level.store (named[i].level, std::memory_order_relaxed);
                    c.hash.store (h, std::memory_order_release);
                    break;
                }
            }
        }
        allLevel_.store (allValue, std::memory_order_relaxed);
    }

    bool openFile() noexcept
    {
        if (fd_ >= 0)
            return true;
        if (settings_.logPath.empty())
            return false;
        const int fd = ::open (settings_.logPath.c_str(), O_WRONLY | O_CREAT | O_APPEND | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC, 0644);
        if (fd < 0)
            return false;
        struct stat st;
        if (::fstat (fd, &st) != 0 || ! S_ISREG (st.st_mode))
        {
            ::close (fd);
            return false;
        }
        fd_ = fd;
        bytes_ = static_cast<size_t> (st.st_size);
        return true;
    }

    void appendBlock (const char* data, size_t n) noexcept
    {
        if (n == 0 || ! openFile())
            return;
        if (bytes_ + n > settings_.capBytes)
        {
            ::close (fd_);
            fd_ = -1;
            const std::string old = settings_.logPath + ".1";
            ::rename (settings_.logPath.c_str(), old.c_str());   // the previous rotation is replaced: at most two files
            if (! openFile())
                return;
        }
        size_t done = 0;
        while (done < n)
        {
            const ssize_t r = ::write (fd_, data + done, n - done);
            if (r > 0) done += static_cast<size_t> (r);
            else if (r < 0 && errno == EINTR) continue;
            else break;
        }
        bytes_ += done;
        bytesTotal_.fetch_add (done, std::memory_order_relaxed);
    }

    void drainLocked()
    {
        char block[16 * 1024];
        size_t n = 0;
        for (;;)
        {
            Slot& s = ring_[tail_ % kRingSlots];
            if (s.seq.load (std::memory_order_acquire) != tail_ + 1)
                break;
            const long long ms10 = s.ns / 100000;   // 0.1 ms resolution
            const int len = std::snprintf (block + n, sizeof block - n, "%lld.%01lld %s %04x %s %s\n", ms10 / 10, ms10 % 10, levelName (static_cast<Level> (s.level)), static_cast<unsigned> (s.thread), s.category, s.text);
            if (len < 0 || static_cast<size_t> (len) >= sizeof block - n)
            {
                appendBlock (block, n);   // the block is full: write it out and take this record again
                n = 0;
                if (static_cast<size_t> (len) >= sizeof block)
                {
                    s.seq.store (tail_ + kRingSlots, std::memory_order_release);
                    ++tail_;
                }
                continue;
            }
            n += static_cast<size_t> (len);
            s.seq.store (tail_ + kRingSlots, std::memory_order_release);   // free for the next lap
            ++tail_;
        }
        const unsigned long lost = dropped_.exchange (0, std::memory_order_relaxed);
        if (lost > 0)
        {
            droppedTotal_.fetch_add (lost, std::memory_order_relaxed);
            const int len = std::snprintf (block + n, sizeof block - n, "-- %lu records dropped: the ring was full --\n", lost);
            if (len > 0 && static_cast<size_t> (len) < sizeof block - n)
                n += static_cast<size_t> (len);
        }
        appendBlock (block, n);
    }

    void writerLoop() noexcept
    {
        int sinceConfig = 0;
        for (;;)
        {
            {
                std::unique_lock<std::mutex> lk (wakeMutex_);
                wake_.wait_for (lk, std::chrono::milliseconds (50), [this] { return stopFlag_; });
                if (stopFlag_)
                    return;
            }
            std::lock_guard<std::mutex> g (drainMutex_);
            try { drainLocked(); } catch (...) {}
            if (++sinceConfig >= 20)   // 20 x 50 ms: about once a second
            {
                sinceConfig = 0;
                readConfig();
            }
        }
    }

    const std::chrono::steady_clock::time_point origin_;
    Slot ring_[kRingSlots];
    std::atomic<size_t> head_ { 0 };
    size_t tail_ = 0;                                  // the writer's (under drainMutex_)
    std::atomic<unsigned long> dropped_ { 0 }, droppedTotal_ { 0 };
    std::atomic<unsigned long long> bytesTotal_ { 0 };
    Category cat_[kMaxCategories];
    std::atomic<int> allLevel_ { 0 };
    Settings settings_;
    int fd_ = -1;
    size_t bytes_ = 0;
    bool running_ = false, stopFlag_ = false;
    std::mutex lifeMutex_, wakeMutex_, drainMutex_;
    std::condition_variable wake_;
    std::thread thread_;
};

inline Logger& logger() noexcept
{
    static Logger instance;
    return instance;
}

inline void start (const char* plugin)
{
    Settings s;
    s.logPath = std::string ("/tmp/") + plugin + "-dev.log";
    s.confPath = std::string ("/tmp/") + plugin + "-log.conf";
    logger().start (s);
}

inline void emit (const char* category, Level level, const char* fmt, ...) noexcept __attribute__ ((format (printf, 3, 4)));
inline void emit (const char* category, Level level, const char* fmt, ...) noexcept
{
    va_list ap;
    va_start (ap, fmt);
    logger().write (category, level, fmt, ap);
    va_end (ap);
}

/** Logs "<name>: <ms> ms" at level info when the enclosing scope ends (and the category is on at info). */
class Scope
{
public:
    Scope (const char* category, const char* name) noexcept : category_ (category), name_ (name), t0_ (std::chrono::steady_clock::now()) {}
    ~Scope()
    {
        if (logger().enabled (category_, Level::info))
            emit (category_, Level::info, "%s: %.2f ms", name_, std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0_).count());
    }
    Scope (const Scope&) = delete;
    Scope& operator= (const Scope&) = delete;

private:
    const char* category_;
    const char* name_;
    std::chrono::steady_clock::time_point t0_;
};
} // namespace devlog
} // namespace mpckit

#define MPCKIT_LOG(category, level, ...) \
    do { if (::mpckit::devlog::logger().enabled (category, ::mpckit::Level::level)) ::mpckit::devlog::emit (category, ::mpckit::Level::level, __VA_ARGS__); } while (0)
#define MPCKIT_SCOPE_JOIN2(a, b) a##b
#define MPCKIT_SCOPE_JOIN(a, b) MPCKIT_SCOPE_JOIN2 (a, b)
#define MPCKIT_SCOPE(category, name) ::mpckit::devlog::Scope MPCKIT_SCOPE_JOIN (mpckitScope_, __LINE__) (category, name)
#define MPCKIT_LOG_START(plugin) ::mpckit::devlog::start (plugin)

#else // MPCKIT_DEV_LOG == 0: nothing of the log exists in the binary

#define MPCKIT_LOG(category, level, ...) do {} while (0)
#define MPCKIT_SCOPE(category, name) do {} while (0)
#define MPCKIT_LOG_START(plugin) do {} while (0)

#endif
