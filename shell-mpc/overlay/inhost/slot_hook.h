// SynthyBoy - MPC in-host drawing (experimental): the interposition. The host (/usr/bin/MPC, a PIE with full RELRO) calls libdrm's drmModeAtomicCommit through ONE jump slot of its GOT. The slot is found
// by name in the main program's own relocation table (dl_iterate_phdr, DT_JMPREL / DT_REL(A)), compared with what dlsym says libdrm's function is, and replaced by ours with the page made writable only for
// the one aligned store (an atomic store: a thread calling through the slot meanwhile gets the old or the new address, never a mixture) and put back at once. Nothing is patched unless every check holds: the
// symbol is bound exactly once, the slot holds the address of the function in a file named libdrm, the page is mapped read-only (r--p, the RELRO a full-RELRO program has) and the mapping can be changed.
// The memory and the protection calls are behind an interface (IProtect) so the logic is tested with a fake slot.
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#if defined(__linux__)
#include <dlfcn.h>
#include <elf.h>
#include <link.h>
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace synthyboy_mpc { namespace overlay { namespace inhost {

struct Protection
{
    bool found = false, r = false, w = false, x = false, shared = false;
    uintptr_t begin = 0, end = 0;
    std::string text() const
    {
        if (! found)
            return "unmapped";
        return std::string (r ? "r" : "-") + (w ? "w" : "-") + (x ? "x" : "-") + (shared ? "s" : "p");
    }
};

class IProtect
{
public:
    virtual ~IProtect() = default;
    virtual bool query (uintptr_t addr, Protection& out) = 0;
    /** Makes [begin, begin + len) writable (and readable). */
    virtual bool makeWritable (uintptr_t begin, size_t len, std::string& error) = 0;
    /** Puts the protection back to what `original` says. */
    virtual bool restore (uintptr_t begin, size_t len, const Protection& original, std::string& error) = 0;
};

/** The patched slot: install () and remove () are the only places the slot is written. */
class SlotPatch
{
public:
    /** Every check, then the change. `slot`: the address of the jump slot; `expected`: what it must hold now; `replacement`: ours. On false nothing was written (or it was written back) and `why` says what was found. */
    bool install (void** slot, void* expected, void* replacement, IProtect& mem, size_t pageSize, std::string& why)
    {
        if (slot_ != nullptr)
        {
            why = "already installed";
            return false;
        }
        if (slot == nullptr || (reinterpret_cast<uintptr_t> (slot) & (sizeof (void*) - 1)) != 0)
        {
            why = "the slot address is not aligned";
            return false;
        }
        void* now = __atomic_load_n (slot, __ATOMIC_ACQUIRE);
        if (now != expected)
        {
            char b[160];
            std::snprintf (b, sizeof b, "the slot holds %p, expected %p (another module hooked it, or another layout)", now, expected);
            why = b;
            return false;
        }
        Protection prot;
        if (! mem.query (reinterpret_cast<uintptr_t> (slot), prot) || ! prot.found)
        {
            why = "the slot's page is not in the memory map";
            return false;
        }
        if (! prot.r || prot.w || prot.x)
        {
            why = "the slot's page is mapped " + prot.text() + ", expected r--p (full RELRO)";
            return false;
        }
        const size_t pg = pageSize != 0 ? pageSize : 4096;
        const uintptr_t pageBegin = reinterpret_cast<uintptr_t> (slot) & ~(static_cast<uintptr_t> (pg) - 1);
        std::string err;
        if (! mem.makeWritable (pageBegin, pg, err))
        {
            why = "the page cannot be made writable: " + err;
            return false;
        }
        __atomic_store_n (slot, replacement, __ATOMIC_RELEASE);
        bool back = mem.restore (pageBegin, pg, prot, err);
        if (! back)
            back = mem.restore (pageBegin, pg, prot, err);   // once more
        if (! back)
        {
            __atomic_store_n (slot, expected, __ATOMIC_RELEASE);   // the page is still writable: undo the change, then the protection again
            mem.restore (pageBegin, pg, prot, err);
            why = "the protection could not be restored: " + err;
            return false;
        }
        slot_ = slot;
        original_ = expected;
        replacement_ = replacement;
        prot_ = prot;
        pageBegin_ = pageBegin;
        pageSize_ = pg;
        return true;
    }

    bool installed() const noexcept { return slot_ != nullptr; }
    void* slotAddress() const noexcept { return slot_; }
    void* original() const noexcept { return original_; }

    /** Puts the original address back. false: the slot no longer holds ours (another module hooked on top of us: it is left alone and our function stays in its chain), or the page could not be opened. */
    bool remove (IProtect& mem, std::string& why)
    {
        if (slot_ == nullptr)
            return true;
        if (__atomic_load_n (slot_, __ATOMIC_ACQUIRE) != replacement_)
        {
            why = "the slot no longer holds our function (hooked on top by another module)";
            return false;
        }
        std::string err;
        if (! mem.makeWritable (pageBegin_, pageSize_, err))
        {
            why = "the page cannot be made writable: " + err;
            return false;
        }
        __atomic_store_n (slot_, original_, __ATOMIC_RELEASE);
        if (! mem.restore (pageBegin_, pageSize_, prot_, err) && ! mem.restore (pageBegin_, pageSize_, prot_, err))
            why = "the protection could not be restored after the removal: " + err;
        slot_ = nullptr;
        return true;
    }

private:
    void** slot_ = nullptr;
    void* original_ = nullptr;
    void* replacement_ = nullptr;
    Protection prot_;
    uintptr_t pageBegin_ = 0;
    size_t pageSize_ = 0;
};

/** Install / remove by reference count: the first acquire () installs, the last release () removes. A refused install is remembered until the count has been 0 again. */
template <class Installer>
class HookRefs
{
public:
    explicit HookRefs (Installer& inst) : inst_ (inst) {}
    /** true: the hook is in place (for this and every other holder). */
    bool acquire()
    {
        if (count_++ == 0)
            installed_ = inst_.install();
        return installed_;
    }
    void release()
    {
        if (count_ == 0)
            return;
        if (--count_ == 0 && installed_)
        {
            inst_.remove();
            installed_ = false;
        }
    }
    int count() const noexcept { return count_; }
    bool installed() const noexcept { return installed_; }

private:
    Installer& inst_;
    int count_ = 0;
    bool installed_ = false;
};

#if defined(__linux__)
/** Protection from /proc/self/maps and mprotect. */
class LinuxProtect final : public IProtect
{
public:
    bool query (uintptr_t addr, Protection& out) override
    {
        FILE* f = std::fopen ("/proc/self/maps", "r");
        if (! f)
            return false;
        char line[512];
        bool found = false;
        while (std::fgets (line, sizeof line, f))
        {
            unsigned long b = 0, e = 0;
            char perms[8] = {};
            if (std::sscanf (line, "%lx-%lx %7s", &b, &e, perms) != 3)
                continue;
            if (addr >= b && addr < e)
            {
                out.found = true;
                out.begin = b;
                out.end = e;
                out.r = perms[0] == 'r';
                out.w = perms[1] == 'w';
                out.x = perms[2] == 'x';
                out.shared = perms[3] == 's';
                found = true;
                break;
            }
        }
        std::fclose (f);
        return found;
    }
    bool makeWritable (uintptr_t begin, size_t len, std::string& error) override
    {
        if (mprotect (reinterpret_cast<void*> (begin), len, PROT_READ | PROT_WRITE) != 0)
        {
            error = std::strerror (errno);
            return false;
        }
        return true;
    }
    bool restore (uintptr_t begin, size_t len, const Protection& original, std::string& error) override
    {
        const int prot = (original.r ? PROT_READ : 0) | (original.w ? PROT_WRITE : 0) | (original.x ? PROT_EXEC : 0);
        if (mprotect (reinterpret_cast<void*> (begin), len, prot) != 0)
        {
            error = std::strerror (errno);
            return false;
        }
        return true;
    }
};

/** The jump slots (GOT entries) of the MAIN program that are bound to the symbol `name`. */
struct SlotSearch
{
    void** slots[4] = {};
    int count = 0;
    std::string note;
};

#if defined(__LP64__)
using ElfRel = Elf64_Rela;
static constexpr bool kRelIsRela = true;
#define SYNTHYBOY_ELF_R_SYM(i) ELF64_R_SYM (i)
#define SYNTHYBOY_ELF_R_TYPE(i) ELF64_R_TYPE (i)
#else
using ElfRel = Elf32_Rel;
static constexpr bool kRelIsRela = false;
#define SYNTHYBOY_ELF_R_SYM(i) ELF32_R_SYM (i)
#define SYNTHYBOY_ELF_R_TYPE(i) ELF32_R_TYPE (i)
#endif

inline SlotSearch findMainProgramSlots (const char* name)
{
    SlotSearch res;
    struct Ctx { const char* name; SlotSearch* res; bool first; } ctx { name, &res, true };
    dl_iterate_phdr ([] (dl_phdr_info* info, size_t, void* data) -> int
    {
        Ctx& c = *static_cast<Ctx*> (data);
        if (! c.first)
            return 1;
        c.first = false;   // the first object is the main program
        const ElfW (Dyn)* dyn = nullptr;
        for (int i = 0; i < info->dlpi_phnum; ++i)
            if (info->dlpi_phdr[i].p_type == PT_DYNAMIC)
                dyn = reinterpret_cast<const ElfW (Dyn)*> (info->dlpi_addr + info->dlpi_phdr[i].p_vaddr);
        if (dyn == nullptr)
        {
            c.res->note = "the main program has no dynamic section";
            return 1;
        }
        uintptr_t jmprel = 0, symtab = 0, strtab = 0, rel = 0;
        size_t pltrelsz = 0, relsz = 0, syment = sizeof (ElfW (Sym));
        long pltrel = 0;
        for (const ElfW (Dyn)* d = dyn; d->d_tag != DT_NULL; ++d)
        {
            switch (d->d_tag)
            {
                case DT_JMPREL: jmprel = d->d_un.d_ptr; break;
                case DT_PLTRELSZ: pltrelsz = d->d_un.d_val; break;
                case DT_PLTREL: pltrel = static_cast<long> (d->d_un.d_val); break;
                case DT_SYMTAB: symtab = d->d_un.d_ptr; break;
                case DT_STRTAB: strtab = d->d_un.d_ptr; break;
                case DT_SYMENT: syment = d->d_un.d_val; break;
                default:
                    if (kRelIsRela ? d->d_tag == DT_RELA : d->d_tag == DT_REL) rel = d->d_un.d_ptr;
                    if (kRelIsRela ? d->d_tag == DT_RELASZ : d->d_tag == DT_RELSZ) relsz = d->d_un.d_val;
                    break;
            }
        }
        // the dynamic section holds addresses; glibc may have relocated them in place (a PIE: base added) or not
        const uintptr_t base = info->dlpi_addr;
        auto fix = [base] (uintptr_t p) { return p >= base && base != 0 ? p : p + base; };
        if (symtab == 0 || strtab == 0 || syment != sizeof (ElfW (Sym)))
        {
            c.res->note = "no symbol table in the main program";
            return 1;
        }
        symtab = fix (symtab);
        strtab = fix (strtab);
        auto scan = [&] (uintptr_t table, size_t bytes, bool isRela)
        {
            if (table == 0 || bytes == 0)
                return;
            table = fix (table);
            const size_t entry = isRela ? sizeof (Elf64_Rela) : sizeof (ElfRel);
            for (size_t off = 0; off + entry <= bytes; off += entry)
            {
                const ElfRel* r = reinterpret_cast<const ElfRel*> (table + off);
                const unsigned sym = SYNTHYBOY_ELF_R_SYM (r->r_info);
                if (sym == 0)
                    continue;
                const ElfW (Sym)* s = reinterpret_cast<const ElfW (Sym)*> (symtab + static_cast<size_t> (sym) * syment);
                if (std::strcmp (reinterpret_cast<const char*> (strtab + s->st_name), c.name) != 0)
                    continue;
                if (c.res->count < 4)
                    c.res->slots[c.res->count++] = reinterpret_cast<void**> (base + r->r_offset);
            }
        };
        (void) pltrel;
        scan (jmprel, pltrelsz, kRelIsRela);
        scan (rel, relsz, kRelIsRela);
        return 1;
    }, &ctx);
    return res;
}
#endif // __linux__

}}} // namespace synthyboy_mpc::overlay::inhost
