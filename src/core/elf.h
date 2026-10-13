#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

// ELF64 standard types
typedef uint64_t Elf64_Addr;
typedef uint64_t Elf64_Off;
typedef uint16_t Elf64_Half;
typedef uint32_t Elf64_Word;
typedef int32_t  Elf64_Sword;
typedef uint64_t Elf64_Xword;
typedef int64_t  Elf64_Sxword;

#pragma pack(push, 1)

struct Elf64_Ehdr {
    unsigned char e_ident[16];
    Elf64_Half    e_type;
    Elf64_Half    e_machine;
    Elf64_Word    e_version;
    Elf64_Addr    e_entry;
    Elf64_Off     e_phoff;
    Elf64_Off     e_shoff;
    Elf64_Word    e_flags;
    Elf64_Half    e_ehsize;
    Elf64_Half    e_phentsize;
    Elf64_Half    e_phnum;
    Elf64_Half    e_shentsize;
    Elf64_Half    e_shnum;
    Elf64_Half    e_shstrndx;
};

struct Elf64_Phdr {
    Elf64_Word    p_type;
    Elf64_Word    p_flags;
    Elf64_Off     p_offset;
    Elf64_Addr    p_vaddr;
    Elf64_Addr    p_paddr;
    Elf64_Xword   p_filesz;
    Elf64_Xword   p_memsz;
    Elf64_Xword   p_align;
};

struct Elf64_Dyn {
    Elf64_Sxword  d_tag;
    union {
        Elf64_Xword d_val;
        Elf64_Addr  d_ptr;
    } d_un;
};

struct Elf64_Rela {
    Elf64_Addr    r_offset;
    Elf64_Xword   r_info;
    Elf64_Sxword  r_addend;
};

struct Elf64_Sym {
    Elf64_Word    st_name;
    unsigned char st_info;
    unsigned char st_other;
    Elf64_Half    st_shndx;
    Elf64_Addr    st_value;
    Elf64_Xword   st_size;
};

#pragma pack(pop)

// ELF Segment Types
constexpr Elf64_Word PT_NULL             = 0;
constexpr Elf64_Word PT_LOAD             = 1;
constexpr Elf64_Word PT_DYNAMIC          = 2;
constexpr Elf64_Word PT_INTERP           = 3;
constexpr Elf64_Word PT_NOTE             = 4;
constexpr Elf64_Word PT_SHLIB            = 5;
constexpr Elf64_Word PT_PHDR             = 6;
constexpr Elf64_Word PT_TLS              = 7;
constexpr Elf64_Word PT_SCE_RELA         = 0x60000000;
constexpr Elf64_Word PT_SCE_DYNLIBDATA   = 0x61000000;
constexpr Elf64_Word PT_SCE_PROT         = 0x61000001;
constexpr Elf64_Word PT_SCE_RELRO        = 0x61000010;
constexpr Elf64_Word PT_SCE_COMMENT      = 0x6fffff00;
constexpr Elf64_Word PT_SCE_LIBVERSION   = 0x6fffff01;

// Segment Flags
constexpr Elf64_Word PF_X = 0x1;
constexpr Elf64_Word PF_W = 0x2;
constexpr Elf64_Word PF_R = 0x4;

// PS4 Dynamic Tags
constexpr Elf64_Sxword DT_SCE_FINGERPRINT         = 0x61000007;
constexpr Elf64_Sxword DT_SCE_ORIGINAL_FILENAME   = 0x61000009;
constexpr Elf64_Sxword DT_SCE_MODULE_INFO         = 0x6100000d;
constexpr Elf64_Sxword DT_SCE_NEEDED_MODULE       = 0x6100000f;
constexpr Elf64_Sxword DT_SCE_MODULE_ATTR         = 0x61000011;
constexpr Elf64_Sxword DT_SCE_EXPORT_LIB          = 0x61000013;
constexpr Elf64_Sxword DT_SCE_IMPORT_LIB          = 0x61000015;
constexpr Elf64_Sxword DT_SCE_EXPORT_LIB_ATTR     = 0x61000017;
constexpr Elf64_Sxword DT_SCE_IMPORT_LIB_ATTR     = 0x61000019;
constexpr Elf64_Sxword DT_SCE_STUB_MODULE_NAME    = 0x6100001d;
constexpr Elf64_Sxword DT_SCE_STUB_MODULE_VERSION = 0x6100001f;
constexpr Elf64_Sxword DT_SCE_STUB_LIBRARY_NAME   = 0x61000021;
constexpr Elf64_Sxword DT_SCE_STUB_LIBRARY_VERSION= 0x61000023;
constexpr Elf64_Sxword DT_SCE_HASH                = 0x61000025;
constexpr Elf64_Sxword DT_SCE_PLTGOT              = 0x61000027;
constexpr Elf64_Sxword DT_SCE_JMPREL              = 0x61000029;
constexpr Elf64_Sxword DT_SCE_PLTREL              = 0x6100002b;
constexpr Elf64_Sxword DT_SCE_PLTRELSZ            = 0x6100002d;
constexpr Elf64_Sxword DT_SCE_RELA                = 0x6100002f;
constexpr Elf64_Sxword DT_SCE_RELASZ              = 0x61000031;
constexpr Elf64_Sxword DT_SCE_RELAENT             = 0x61000033;
constexpr Elf64_Sxword DT_SCE_STRTAB              = 0x61000035;
constexpr Elf64_Sxword DT_SCE_STRSZ               = 0x61000037;
constexpr Elf64_Sxword DT_SCE_SYMTAB              = 0x61000039;
constexpr Elf64_Sxword DT_SCE_SYMENT              = 0x6100003b;
constexpr Elf64_Sxword DT_SCE_HASHSZ              = 0x6100003d;
constexpr Elf64_Sxword DT_SCE_SYMTABSZ            = 0x6100003f;

// Relocation Types (AMD64)
constexpr Elf64_Word R_X86_64_NONE      = 0;
constexpr Elf64_Word R_X86_64_64        = 1;
constexpr Elf64_Word R_X86_64_GLOB_DAT  = 6;
constexpr Elf64_Word R_X86_64_JUMP_SLOT = 7;
constexpr Elf64_Word R_X86_64_RELATIVE  = 8;
constexpr Elf64_Word R_X86_64_DTPMOD64  = 16;
constexpr Elf64_Word R_X86_64_DTPOFF64  = 17;
constexpr Elf64_Word R_X86_64_TPOFF64   = 18;

inline Elf64_Word ELF64_R_SYM(Elf64_Xword info) { return static_cast<Elf64_Word>(info >> 32); }
inline Elf64_Word ELF64_R_TYPE(Elf64_Xword info) { return static_cast<Elf64_Word>(info & 0xffffffff); }

struct LoadedModule {
    uint8_t* base_address = nullptr;
    size_t   total_size = 0;
    uint64_t entry_point = 0;

    std::vector<Elf64_Phdr> program_headers;

    // Dynamic linker pointers
    Elf64_Dyn*  dynamic_table = nullptr;
    size_t      dynamic_count = 0;
    const char* strtab = nullptr;
    size_t      strsz = 0;
    Elf64_Sym*  symtab = nullptr;
    size_t      symcount = 0;
    Elf64_Rela* jmprel = nullptr;
    size_t      jmprelsz = 0;
    Elf64_Rela* rela = nullptr;
    size_t      relasz = 0;
    uint64_t*   pltgot = nullptr;

    // TLS info
    uint64_t tls_vaddr = 0;
    uint64_t tls_filesz = 0;
    uint64_t tls_memsz = 0;
    uint64_t tls_align = 0;
};

bool LoadOrbisElf(const std::string& filepath, LoadedModule& out_module);
void FreeOrbisElf(LoadedModule& module);
