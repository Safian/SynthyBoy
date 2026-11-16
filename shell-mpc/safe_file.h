// SynthyBoy - MPC shell: the ONE way the plugin (and its GPU helper) reads a small switch / debug / status-input file and writes a small status / log file in /tmp.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The plugin runs inside the MPC application and /tmp is writable by every process of the box, so a file the plugin looks at may be anything: a FIFO nobody writes (an open that blocks for ever),
// a symlink to a device or to another file, a directory, a sparse file of 2 GB with no newline. The rules of this header, for every file of that kind:
//   READ   open (O_RDONLY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC): never blocks, never follows a symlink in the last component; fstat must say a regular file (a FIFO, a device, a directory, a socket is
//          ignored as if the file were not there); at most `cap - 1` bytes are read into the caller's fixed buffer (the rest of the file is never looked at: no allocation grows with the file).
//   WRITE  open (O_WRONLY | O_CREAT | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC, mode 0644 unless the caller says other), fstat must say a regular file, then ftruncate to 0 (never O_TRUNC: opening a FIFO or a device
//          with O_TRUNC would act on it first), at most `kMaxWriteBytes` are written. writeReplacing () writes a new file under a name of its own (O_EXCL) and renames it over the old one:
//          a symlink at the target name is replaced, not followed.
// Nothing here allocates and nothing throws; every function is for threads that may block for a moment (the display thread, the overlay thread), never the audio thread.
#pragma once

#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace synthyboy_mpc { namespace safefile {

/** The size every switch file is read with (a number, a word, a line of numbers: no switch is longer). */
constexpr size_t kSwitchBytes = 256;
/** The most one write puts into a file (a status text, a log line block, a dump of the instance's parameters). */
constexpr size_t kMaxWriteBytes = 4u * 1024u * 1024u;

/** Opens `path` for reading under the rules above; the descriptor, or -1 (absent, not a regular file, not readable). */
inline int openRegularForRead (const char* path) noexcept
{
    if (path == nullptr || *path == '\0')
        return -1;
    const int fd = ::open (path, O_RDONLY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0)
        return -1;
    struct stat st;
    if (::fstat (fd, &st) != 0 || ! S_ISREG (st.st_mode))
    {
        ::close (fd);
        return -1;
    }
    return fd;
}

/** Reads at most `cap - 1` bytes of the file into `buf` and NUL terminates them. The number of bytes read (0 for an empty file), or -1 when the file is absent or not a regular file (buf then holds ""). */
inline long readSmall (const char* path, char* buf, size_t cap) noexcept
{
    if (buf == nullptr || cap == 0)
        return -1;
    buf[0] = '\0';
    const int fd = openRegularForRead (path);
    if (fd < 0)
        return -1;
    size_t got = 0;
    while (got + 1 < cap)
    {
        const ssize_t n = ::read (fd, buf + got, cap - 1 - got);
        if (n > 0)
            got += static_cast<size_t> (n);
        else if (n < 0 && errno == EINTR)
            continue;
        else
            break;
    }
    ::close (fd);
    buf[got] = '\0';
    return static_cast<long> (got);
}

/** The first token (a run of characters above ' ' and below 127) of the file, at most `cap - 1` of them, NUL terminated; false when the file is absent, not a regular file or has no token. */
inline bool readWord (const char* path, char* out, size_t cap) noexcept
{
    char raw[kSwitchBytes];
    if (out == nullptr || cap == 0)
        return false;
    out[0] = '\0';
    if (readSmall (path, raw, sizeof raw) <= 0)
        return false;
    size_t i = 0;
    while (raw[i] != '\0' && (static_cast<unsigned char> (raw[i]) <= ' ' || static_cast<unsigned char> (raw[i]) >= 127))
        ++i;
    size_t n = 0;
    while (raw[i] != '\0' && static_cast<unsigned char> (raw[i]) > ' ' && static_cast<unsigned char> (raw[i]) < 127 && n + 1 < cap)
        out[n++] = raw[i++];
    out[n] = '\0';
    return n > 0;
}

/** The first line (up to the first '\n' or '\r', at most `cap - 1` bytes) of the file; false when the file is absent or not a regular file. An empty first line gives true and "". */
inline bool readFirstLine (const char* path, char* out, size_t cap) noexcept
{
    if (out == nullptr || cap == 0)
        return false;
    if (readSmall (path, out, cap) < 0)
        return false;
    for (size_t i = 0; out[i] != '\0'; ++i)
        if (out[i] == '\n' || out[i] == '\r')
        {
            out[i] = '\0';
            break;
        }
    return true;
}

/** The first number of the file (strtod rules, finite values only); false when there is none. */
inline bool readNumber (const char* path, double& value) noexcept
{
    char w[64];
    if (! readWord (path, w, sizeof w))
        return false;
    char* end = nullptr;
    const double v = std::strtod (w, &end);
    if (end == w || ! (v - v == 0.0))   // not a number, or inf / nan
        return false;
    value = v;
    return true;
}

/** The first integer of the file; false when there is none. */
inline bool readInt (const char* path, long& value) noexcept
{
    char w[64];
    if (! readWord (path, w, sizeof w))
        return false;
    char* end = nullptr;
    errno = 0;
    const long v = std::strtol (w, &end, 10);
    if (end == w || errno == ERANGE)
        return false;
    value = v;
    return true;
}

/** The first two numbers of the file ("key value"-style files read their key with readWord first; this is for "q value"). */
inline bool readTwoNumbers (const char* path, double& a, double& b) noexcept
{
    char raw[kSwitchBytes];
    if (readSmall (path, raw, sizeof raw) <= 0)
        return false;
    char* end = nullptr;
    const double x = std::strtod (raw, &end);
    if (end == raw || ! (x - x == 0.0))
        return false;
    char* end2 = nullptr;
    const double y = std::strtod (end, &end2);
    if (end2 == end || ! (y - y == 0.0))
        return false;
    a = x;
    b = y;
    return true;
}

/** Opens `path` for reading like openRegularForRead, but a symlink in the last component IS followed (a user's own sample or preset file may be a link); the target must be a regular file. Never blocks. */
inline int openRegularFollowing (const char* path) noexcept
{
    if (path == nullptr || *path == '\0')
        return -1;
    const int fd = ::open (path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0)
        return -1;
    struct stat st;
    if (::fstat (fd, &st) != 0 || ! S_ISREG (st.st_mode))
    {
        ::close (fd);
        return -1;
    }
    return fd;
}

/** A user's file (a sample, a preset) read whole into `out` (anything with resize () and data (): std::vector<uint8_t>, std::string), for files that are meant to be big. The size comes from fstat of the opened regular file and must be in 1 .. `limit`
    (else false, nothing is allocated); the file is read in blocks of 1 MB and `cancel` (may be null) is looked at between the blocks: a set flag ends the read at once (effClose is waiting). A FIFO, a device or a directory is refused, the open never blocks. */
template <class Container>
inline bool readWholeBounded (const char* path, Container& out, size_t limit, const std::atomic<bool>* cancel = nullptr) noexcept
{
    const int fd = openRegularFollowing (path);
    if (fd < 0)
        return false;
    struct stat st;
    bool ok = ::fstat (fd, &st) == 0 && st.st_size > 0 && static_cast<unsigned long long> (st.st_size) <= static_cast<unsigned long long> (limit);
    const size_t total = ok ? static_cast<size_t> (st.st_size) : 0;
    try { if (ok) out.resize (total); }
    catch (...) { ok = false; }
    size_t got = 0;
    while (ok && got < total)
    {
        if (cancel != nullptr && cancel->load (std::memory_order_relaxed))
        {
            ok = false;
            break;
        }
        const size_t want = total - got < (1u << 20) ? total - got : (1u << 20);
        const ssize_t n = ::read (fd, reinterpret_cast<char*> (out.data()) + got, want);
        if (n > 0)
            got += static_cast<size_t> (n);
        else if (n < 0 && errno == EINTR)
            continue;
        else
            ok = false;   // shorter than fstat said (truncated while reading) or an error
    }
    ::close (fd);
    return ok;
}

/** Opens `path` for writing under the rules above, truncated to 0 (-1: not possible: the name is a symlink, a FIFO, a directory, a device or cannot be created). `append`: no truncation, writes go to the end. */
inline int openRegularForWrite (const char* path, bool append = false, mode_t mode = 0644) noexcept
{
    if (path == nullptr || *path == '\0')
        return -1;
    const int fd = ::open (path, O_WRONLY | O_CREAT | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC | (append ? O_APPEND : 0), mode);
    if (fd < 0)
        return -1;
    struct stat st;
    if (::fstat (fd, &st) != 0 || ! S_ISREG (st.st_mode) || (! append && ::ftruncate (fd, 0) != 0))
    {
        ::close (fd);
        return -1;
    }
    return fd;
}

/** Writes all of `data` (at most kMaxWriteBytes of it) to the descriptor; false when the write fails. */
inline bool writeAll (int fd, const char* data, size_t n) noexcept
{
    if (n > kMaxWriteBytes)
        n = kMaxWriteBytes;
    size_t done = 0;
    while (done < n)
    {
        const ssize_t w = ::write (fd, data + done, n - done);
        if (w > 0)
            done += static_cast<size_t> (w);
        else if (w < 0 && errno == EINTR)
            continue;
        else
            return false;
    }
    return true;
}

/** The file becomes exactly `data` (bounded to kMaxWriteBytes). */
inline bool writeSmall (const char* path, const char* data, size_t n, mode_t mode = 0644) noexcept
{
    const int fd = openRegularForWrite (path, false, mode);
    if (fd < 0)
        return false;
    const bool ok = writeAll (fd, data, n);
    ::close (fd);
    return ok;
}

/** `data` appended to the file (created when absent). */
inline bool appendSmall (const char* path, const char* data, size_t n, mode_t mode = 0644) noexcept
{
    const int fd = openRegularForWrite (path, true, mode);
    if (fd < 0)
        return false;
    const bool ok = writeAll (fd, data, n);
    ::close (fd);
    return ok;
}

/** The size of a regular file in bytes, -1 when absent or not a regular file (lstat: a symlink is not followed). */
inline long long regularFileSize (const char* path) noexcept
{
    struct stat st;
    if (path == nullptr || ::lstat (path, &st) != 0 || ! S_ISREG (st.st_mode))
        return -1;
    return static_cast<long long> (st.st_size);
}

/** Empties a regular file (no symlink followed: a symlink at the name is left alone). */
inline bool truncateRegular (const char* path) noexcept
{
    const int fd = ::open (path, O_WRONLY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0)
        return false;
    struct stat st;
    const bool ok = ::fstat (fd, &st) == 0 && S_ISREG (st.st_mode) && ::ftruncate (fd, 0) == 0;
    ::close (fd);
    return ok;
}

/** The file at `path` becomes `data` in one step: written under "<path>.tmp" (a stale name is removed first, created with O_EXCL | O_NOFOLLOW), then renamed over `path` (a symlink at `path` is replaced, not followed). */
inline bool writeReplacing (const char* path, const char* data, size_t n, mode_t mode = 0644) noexcept
{
    char tmp[1024];
    const int len = std::snprintf (tmp, sizeof tmp, "%s.tmp", path != nullptr ? path : "");
    if (path == nullptr || len <= 4 || static_cast<size_t> (len) >= sizeof tmp)
        return false;
    ::unlink (tmp);
    const int fd = ::open (tmp, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, mode);
    if (fd < 0)
        return false;
    const bool ok = writeAll (fd, data, n);
    ::close (fd);
    if (! ok || ::rename (tmp, path) != 0)
    {
        ::unlink (tmp);
        return false;
    }
    return true;
}

}} // namespace synthyboy_mpc::safefile
