// SynthyBoy - MPC overlay, GPU helper: the real IHelperTransport (Linux): a memfd for the shared block, a SOCK_SEQPACKET socket pair, the helper started with posix_spawn.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Never `fork` without exec: the plugin lives in a multi-threaded real-time process, posix_spawn (vfork semantics in glibc) only suspends the calling thread (the overlay thread)
// until the exec. The helper gets: descriptor 3 = its end of the socket, 0 = /dev/null, 1 and 2 = /tmp/synthyboy-overlay-gpu.log (append); every other descriptor of the MPC
// application is closed in the child (addclosefrom_np), the signal mask is empty and the signal handlers are the defaults. The display descriptor and the memfd travel over the
// socket with SCM_RIGHTS in the `hello`: the helper gets the SAME open file description as the application's descriptor, so it has the application's (master) rights on plane 38.
// The helper's scheduling and affinity are those of the spawning thread (SCHED_OTHER, nice 0, the isolated audio cores are not in its mask).
#pragma once

#if defined(__linux__)
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>

#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include "gpu_overlay.h"

extern char** environ;

namespace synthyboy_mpc { namespace overlay { namespace gpu {

class LinuxHelperTransport final : public IHelperTransport
{
public:
    /** `helperPath`: the executable (next to the plugin). `logPath`: where its stdout / stderr go. */
    LinuxHelperTransport (std::string helperPath, std::string logPath = "/tmp/synthyboy-overlay-gpu.log") : path_ (std::move (helperPath)), log_ (std::move (logPath)) {}
    ~LinuxHelperTransport() override
    {
        kill();
        close();
    }

    bool start (int displayFd, uint32_t crtc, uint32_t plane, uint32_t flags, std::string& error) override
    {
        close();
        if (path_.empty() || ::access (path_.c_str(), X_OK) != 0)
        {
            error = "the helper executable is missing or not executable: " + path_;
            return false;
        }
        return startImpl (displayFd, crtc, plane, flags, error);
    }

    SharedBlock* block() noexcept override { return block_; }
    int pid() const noexcept override { return pid_; }

    bool send (const Msg& m) override
    {
        if (sock_ < 0)
            return false;
        const ssize_t n = ::send (sock_, &m, sizeof m, MSG_NOSIGNAL | MSG_DONTWAIT);
        return n == static_cast<ssize_t> (sizeof m);
    }

    int receive (Msg& m) override
    {
        if (sock_ < 0)
            return -1;
        const ssize_t n = ::recv (sock_, &m, sizeof m, MSG_DONTWAIT);
        if (n == static_cast<ssize_t> (sizeof m))
            return 1;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
            return 0;
        return -1;   // 0 = the peer closed, anything else = an error
    }

    bool alive() override
    {
        if (pid_ <= 0)
            return false;
        int st = 0;
        const pid_t r = ::waitpid (pid_, &st, WNOHANG);
        if (r == pid_)
        {
            reaped_ = true;
            return false;
        }
        if (r < 0 && errno == ECHILD)   // somebody else reaped it (a SIGCHLD handler of the application) or the signal is ignored: ask the kernel
            return ::kill (pid_, 0) == 0;
        return true;
    }

    void kill() noexcept override
    {
        if (pid_ <= 0)
            return;
        if (! reaped_)
        {
            ::kill (pid_, SIGKILL);
            for (int i = 0; i < 200; ++i)   // up to 2 s
            {
                int st = 0;
                const pid_t r = ::waitpid (pid_, &st, WNOHANG);
                if (r == pid_ || (r < 0 && errno == ECHILD && ::kill (pid_, 0) != 0))
                    break;
                sleepMs (10);
            }
        }
        reaped_ = true;
        pid_ = 0;
    }

    bool waitExit (int ms) noexcept override
    {
        for (int waited = 0; waited <= ms; waited += 5)
        {
            if (! alive())
            {
                pid_ = 0;
                return true;
            }
            sleepMs (5);
        }
        return false;
    }

    void close() noexcept override
    {
        if (sock_ >= 0)
            ::close (sock_);
        sock_ = -1;
        if (block_)
            ::munmap (block_, sizeof (SharedBlock));
        block_ = nullptr;
        if (pid_ > 0 && ! reaped_)
            kill();
        pid_ = 0;
        reaped_ = false;
    }

private:
    static void sleepMs (int ms) noexcept
    {
        timespec ts { 0, static_cast<long> (ms) * 1000000L };
        nanosleep (&ts, nullptr);
    }

    bool startImpl (int displayFd, uint32_t crtc, uint32_t plane, uint32_t flags, std::string& error)
    {
        // the shared block
        int mfd = memfdCreate();
        if (mfd < 0)
        {
            error = std::string ("memfd_create: ") + std::strerror (errno);
            return false;
        }
        if (::ftruncate (mfd, static_cast<off_t> (sizeof (SharedBlock))) != 0)
        {
            error = std::string ("ftruncate: ") + std::strerror (errno);
            ::close (mfd);
            return false;
        }
        void* p = ::mmap (nullptr, sizeof (SharedBlock), PROT_READ | PROT_WRITE, MAP_SHARED, mfd, 0);
        if (p == MAP_FAILED)
        {
            error = std::string ("mmap: ") + std::strerror (errno);
            ::close (mfd);
            return false;
        }
        block_ = static_cast<SharedBlock*> (p);
        initBlock (block_);
        // the socket pair (both ends close on exec: the helper's end is moved to descriptor 3 by a file action)
        int sv[2];
        if (::socketpair (AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, sv) != 0)
        {
            error = std::string ("socketpair: ") + std::strerror (errno);
            ::close (mfd);
            close();
            return false;
        }
        posix_spawn_file_actions_t fa;
        posix_spawnattr_t at;
        posix_spawn_file_actions_init (&fa);
        posix_spawnattr_init (&at);
        posix_spawn_file_actions_addopen (&fa, 0, "/dev/null", O_RDONLY, 0);
        posix_spawn_file_actions_addopen (&fa, 1, log_.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
        posix_spawn_file_actions_adddup2 (&fa, 1, 2);
        // the socket end: onto descriptor 3 (dup2 clears the close-on-exec flag of the copy); a source that already is 3 would keep the flag, so move it first
        int child = sv[1];
        if (child == 3)
        {
            child = ::fcntl (child, F_DUPFD_CLOEXEC, 10);
            ::close (3);   // no longer ours: the copy is
            sv[1] = child;
        }
        posix_spawn_file_actions_adddup2 (&fa, child, 3);
        posix_spawn_file_actions_addclosefrom_np (&fa, 4);
        sigset_t none, all;
        sigemptyset (&none);
        sigfillset (&all);
        posix_spawnattr_setsigmask (&at, &none);
        posix_spawnattr_setsigdefault (&at, &all);
        posix_spawnattr_setflags (&at, POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF);
        char* argv[] = { const_cast<char*> (path_.c_str()), nullptr };
        pid_t pid = 0;
        const int rc = posix_spawn (&pid, path_.c_str(), &fa, &at, argv, environ);
        posix_spawn_file_actions_destroy (&fa);
        posix_spawnattr_destroy (&at);
        ::close (sv[1]);
        if (rc != 0)
        {
            error = std::string ("posix_spawn: ") + std::strerror (rc);
            ::close (sv[0]);
            ::close (mfd);
            close();
            return false;
        }
        pid_ = pid;
        reaped_ = false;
        sock_ = sv[0];
        // hello + descriptors
        const Msg hello = makeMsg (kMsgHello, kProtocolVersion, crtc, plane, flags);
        const int fds[2] = { displayFd, mfd };
        const bool sent = sendWithFds (hello, fds, 2);
        ::close (mfd);   // the helper has its own copy; the mapping stays
        if (! sent)
        {
            error = std::string ("hello: ") + std::strerror (errno);
            kill();
            close();
            return false;
        }
        return true;
    }

    static int memfdCreate() noexcept
    {
#if defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 27))
        return ::memfd_create ("synthyboy-overlay-gpu", MFD_CLOEXEC);
#else
        errno = ENOSYS;
        return -1;
#endif
    }

    bool sendWithFds (const Msg& m, const int* fds, int n)
    {
        iovec io;
        io.iov_base = const_cast<Msg*> (&m);
        io.iov_len = sizeof m;
        alignas (cmsghdr) char ctl[CMSG_SPACE (sizeof (int) * 4)];
        std::memset (ctl, 0, sizeof ctl);
        msghdr mh;
        std::memset (&mh, 0, sizeof mh);
        mh.msg_iov = &io;
        mh.msg_iovlen = 1;
        mh.msg_control = ctl;
        mh.msg_controllen = CMSG_SPACE (sizeof (int) * static_cast<size_t> (n));
        cmsghdr* c = CMSG_FIRSTHDR (&mh);
        c->cmsg_level = SOL_SOCKET;
        c->cmsg_type = SCM_RIGHTS;
        c->cmsg_len = CMSG_LEN (sizeof (int) * static_cast<size_t> (n));
        std::memcpy (CMSG_DATA (c), fds, sizeof (int) * static_cast<size_t> (n));
        return ::sendmsg (sock_, &mh, MSG_NOSIGNAL) == static_cast<ssize_t> (sizeof m);
    }

    std::string path_, log_;
    int sock_ = -1;
    pid_t pid_ = 0;
    bool reaped_ = false;
    SharedBlock* block_ = nullptr;
};

}}} // namespace synthyboy_mpc::overlay::gpu
#endif // __linux__
