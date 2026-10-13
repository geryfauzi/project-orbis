#include "elf.h"
#include <windows.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <cstring>
#include <map>

struct SelfEntry {
    uint64_t flags;
    uint64_t file_offset;
    uint64_t file_size;
    uint64_t memory_size;
};

bool LoadOrbisElf(const std::string& filepath, LoadedModule& out_module) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "[ELF] Failed to open file: " << filepath << std::endl;
        return false;
    }

    // Read FSELF / ELF header magic
    uint32_t magic = 0;
    file.read(reinterpret_cast<char*>(&magic), 4);
    if (magic != 0x1D3D154F && magic != 0x464c457f) {
        std::cerr << "[ELF] Unsupported binary format magic: 0x" << std::hex << magic << std::dec << std::endl;
        return false;
    }

    uint64_t elf_header_offset = 0;
    std::map<uint32_t, uint64_t> fself_seg_offsets;
    std::map<uint32_t, uint64_t> fself_seg_sizes;

    if (magic == 0x1D3D154F) {
        // FSELF header
        elf_header_offset = 0x160;

        file.seekg(0x18, std::ios::beg);
        uint16_t num_entries = 0;
        file.read(reinterpret_cast<char*>(&num_entries), sizeof(num_entries));

        file.seekg(0x20, std::ios::beg);
        for (uint16_t i = 0; i < num_entries; i++) {
            SelfEntry entry;
            file.read(reinterpret_cast<char*>(&entry), sizeof(entry));

            uint32_t seg_idx = static_cast<uint32_t>((entry.flags >> 20) & 0xfff);
            uint32_t seg_type = static_cast<uint32_t>((entry.flags >> 8) & 0xf);

            // Type 8 represents actual segment data
            if (seg_type == 8) {
                fself_seg_offsets[seg_idx] = entry.file_offset;
                fself_seg_sizes[seg_idx] = entry.file_size;
            }
        }
    }

    // Read ELF Header
    file.seekg(elf_header_offset, std::ios::beg);
    Elf64_Ehdr ehdr;
    file.read(reinterpret_cast<char*>(&ehdr), sizeof(ehdr));

    if (memcmp(ehdr.e_ident, "\x7f\x45\x4c\x46", 4) != 0) {
        std::cerr << "[ELF] Invalid ELF identification bytes!" << std::endl;
        return false;
    }

    out_module.entry_point = ehdr.e_entry;

    // Read Program Headers
    file.seekg(elf_header_offset + ehdr.e_phoff, std::ios::beg);
    out_module.program_headers.resize(ehdr.e_phnum);
    file.read(reinterpret_cast<char*>(out_module.program_headers.data()), ehdr.e_phnum * sizeof(Elf64_Phdr));

    // Calculate virtual memory size for loadable segments
    uint64_t max_vaddr = 0;
    for (const auto& ph : out_module.program_headers) {
        if (ph.p_type == PT_LOAD || ph.p_type == PT_SCE_RELRO) {
            uint64_t end = ph.p_vaddr + ph.p_memsz;
            if (end > max_vaddr) max_vaddr = end;
        }
    }

    // Page align to 64KB
    size_t total_alloc = (max_vaddr + 0xffff) & ~0xffff;
    out_module.total_size = total_alloc;

    // Allocate guest virtual address space
    uint8_t* base = reinterpret_cast<uint8_t*>(VirtualAlloc(
        nullptr,
        total_alloc,
        MEM_RESERVE | MEM_COMMIT,
        PAGE_EXECUTE_READWRITE
    ));

    if (!base) {
        std::cerr << "[ELF] VirtualAlloc failed (size: 0x" << std::hex << total_alloc << std::dec 
                  << ", err: " << GetLastError() << ")" << std::endl;
        return false;
    }

    out_module.base_address = base;
    std::memset(base, 0, total_alloc);

    // Map segments into virtual memory
    for (size_t i = 0; i < out_module.program_headers.size(); i++) {
        const auto& ph = out_module.program_headers[i];
        if (ph.p_type == PT_LOAD || ph.p_type == PT_SCE_RELRO) {
            uint64_t file_off = ph.p_offset;
            uint64_t file_sz = ph.p_filesz;

            // Check if mapped in FSELF entries
            if (fself_seg_offsets.find(static_cast<uint32_t>(i)) != fself_seg_offsets.end()) {
                file_off = fself_seg_offsets[static_cast<uint32_t>(i)];
                file_sz = fself_seg_sizes[static_cast<uint32_t>(i)];
            }

            uint8_t* dest = base + ph.p_vaddr;
            if (file_sz > 0) {
                file.seekg(file_off, std::ios::beg);
                file.read(reinterpret_cast<char*>(dest), file_sz);
            }
        } else if (ph.p_type == PT_TLS) {
            out_module.tls_vaddr = ph.p_vaddr;
            out_module.tls_filesz = ph.p_filesz;
            out_module.tls_memsz = ph.p_memsz;
            out_module.tls_align = ph.p_align;
        }
    }

    // Read PT_SCE_DYNLIBDATA (Segment 8)
    int dynlib_idx = -1;
    for (size_t i = 0; i < out_module.program_headers.size(); i++) {
        if (out_module.program_headers[i].p_type == PT_SCE_DYNLIBDATA) {
            dynlib_idx = static_cast<int>(i);
            break;
        }
    }

    if (dynlib_idx < 0) {
        std::cerr << "[ELF] PT_SCE_DYNLIBDATA segment not found!" << std::endl;
        return false;
    }

    uint64_t dynlib_off = fself_seg_offsets.count(dynlib_idx) ? fself_seg_offsets[dynlib_idx] : out_module.program_headers[dynlib_idx].p_offset;
    uint64_t dynlib_sz = fself_seg_sizes.count(dynlib_idx) ? fself_seg_sizes[dynlib_idx] : out_module.program_headers[dynlib_idx].p_filesz;

    std::vector<uint8_t> dynlib_buf(dynlib_sz);
    file.seekg(dynlib_off, std::ios::beg);
    file.read(reinterpret_cast<char*>(dynlib_buf.data()), dynlib_sz);

    // Read PT_DYNAMIC (Segment 4)
    int dyn_idx = -1;
    for (size_t i = 0; i < out_module.program_headers.size(); i++) {
        if (out_module.program_headers[i].p_type == PT_DYNAMIC) {
            dyn_idx = static_cast<int>(i);
            break;
        }
    }

    if (dyn_idx < 0) {
        std::cerr << "[ELF] PT_DYNAMIC segment not found!" << std::endl;
        return false;
    }

    // Calculate dynamic offset relative to dynlibdata in file
    uint64_t dyn_raw_offset = out_module.program_headers[dyn_idx].p_offset;
    uint64_t dynlib_raw_offset = out_module.program_headers[dynlib_idx].p_offset;
    uint64_t dyn_file_off = dynlib_off + (dyn_raw_offset - dynlib_raw_offset);
    uint64_t dyn_sz = out_module.program_headers[dyn_idx].p_filesz;

    size_t dyn_count = dyn_sz / sizeof(Elf64_Dyn);
    std::vector<Elf64_Dyn> dyn_table(dyn_count);
    file.seekg(dyn_file_off, std::ios::beg);
    file.read(reinterpret_cast<char*>(dyn_table.data()), dyn_sz);

    // Parse dynamic tags to set pointers
    uint64_t strtab_off = 0;
    uint64_t symtab_off = 0;
    uint64_t jmprel_off = 0;
    uint64_t rela_off = 0;

    for (const auto& dyn : dyn_table) {
        switch (dyn.d_tag) {
            case DT_SCE_STRTAB:
                strtab_off = dyn.d_un.d_ptr;
                break;
            case DT_SCE_STRSZ:
                out_module.strsz = dyn.d_un.d_val;
                break;
            case DT_SCE_SYMTAB:
                symtab_off = dyn.d_un.d_ptr;
                break;
            case DT_SCE_SYMTABSZ:
                out_module.symcount = dyn.d_un.d_val / sizeof(Elf64_Sym);
                break;
            case DT_SCE_JMPREL:
                jmprel_off = dyn.d_un.d_ptr;
                break;
            case DT_SCE_PLTRELSZ:
                out_module.jmprelsz = dyn.d_un.d_val / sizeof(Elf64_Rela);
                break;
            case DT_SCE_RELA:
                rela_off = dyn.d_un.d_ptr;
                break;
            case DT_SCE_RELASZ:
                out_module.relasz = dyn.d_un.d_val / sizeof(Elf64_Rela);
                break;
            case DT_SCE_PLTGOT:
                out_module.pltgot = reinterpret_cast<uint64_t*>(base + dyn.d_un.d_ptr);
                break;
        }
    }

    // Allocate persistent memory for dynamic structures
    uint8_t* persistent_dynlib = new uint8_t[dynlib_sz];
    std::memcpy(persistent_dynlib, dynlib_buf.data(), dynlib_sz);

    out_module.strtab = reinterpret_cast<const char*>(persistent_dynlib + strtab_off);
    out_module.symtab = reinterpret_cast<Elf64_Sym*>(persistent_dynlib + symtab_off);
    out_module.jmprel = reinterpret_cast<Elf64_Rela*>(persistent_dynlib + jmprel_off);
    out_module.rela = reinterpret_cast<Elf64_Rela*>(persistent_dynlib + rela_off);

    return true;
}

void FreeOrbisElf(LoadedModule& module) {
    if (module.base_address) {
        VirtualFree(module.base_address, 0, MEM_RELEASE);
        module.base_address = nullptr;
    }
    if (module.strtab) {
        delete[] reinterpret_cast<const uint8_t*>(module.strtab - 0x18);
        module.strtab = nullptr;
    }
}
