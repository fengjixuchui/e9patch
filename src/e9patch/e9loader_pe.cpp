/*
 * e9loader_pe.cpp
 * Copyright (C) 2021 National University of Singapore
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * NOTE: As a special exception, this file is under the MIT license.  The
 *       rest of the E9Patch/E9Tool source code is under the GPLv3 license.
 */

#include <cstdarg>
#include <cstdint>
#include <cstdlib>
#include <wchar.h>

#include "e9loader.cpp"

#define CONTAINING_RECORD(addr,type,field)              \
    ((type *)((uint8_t *)(addr) - (uint8_t *)(&((type *)0)->field)))

typedef struct _UNICODE_STRING
{
    uint16_t Length;
    uint16_t MaximumLength;
    wchar_t *Buffer;
} UNICODE_STRING, *PUNICODE_STRING;

typedef struct _LIST_ENTRY
{
    struct _LIST_ENTRY *Flink;
    struct _LIST_ENTRY *Blink;
} LIST_ENTRY, *PLIST_ENTRY;

typedef struct _PEB_LDR_DATA
{
    uint8_t    Reserved1[8];
    void      *Reserved2[3];
    LIST_ENTRY InMemoryOrderModuleList;
} PEB_LDR_DATA, *PPEB_LDR_DATA;

typedef struct _LDR_DATA_TABLE_ENTRY
{
    void          *Reserved1[2];
    LIST_ENTRY     InMemoryOrderLinks;
    void          *Reserved2[2];
    void          *DllBase;
    void          *Reserved3[2];
    UNICODE_STRING FullDllName;
    uint8_t        Reserved4[8];
    void          *Reserved5[3];
    union
    {
        uint64_t   CheckSum;
        void      *Reserved6;
    };
    uint64_t       TimeDateStamp;
} LDR_DATA_TABLE_ENTRY, *PLDR_DATA_TABLE_ENTRY;

typedef struct _PEB
{
    uint8_t       Reserved1[2];
    uint8_t       BeingDebugged;
    uint8_t       Reserved2[1];
    void         *Reserved3[1];
    void         *ImageBaseAddress;
    PPEB_LDR_DATA Ldr;
    void         *ProcessParameters;
    void         *SubSystemData;
    void         *ProcessHeap;
    uint8_t       Reserved4[88];
    void         *Reserved5[52];
    void         *PostProcessInitRoutine;
    uint8_t       Reserved6[128];
    void         *Reserved7[1];
    uint64_t      SessionId;
} PEB, *PPEB;

typedef struct _IMAGE_FILE_HEADER
{
    uint16_t Machine;
    uint16_t NumberOfSections;
    uint32_t TimeDateStamp;
    uint32_t PointerToSymbolTable;
    uint32_t NumberOfSymbols;
    uint16_t SizeOfOptionalHeader;
    uint16_t Characteristics;
} IMAGE_FILE_HEADER, *PIMAGE_FILE_HEADER;

typedef struct _IMAGE_DATA_DIRECTORY
{
    uint32_t VirtualAddress;
    uint32_t Size;
} IMAGE_DATA_DIRECTORY, *PIMAGE_DATA_DIRECTORY;

typedef struct _IMAGE_OPTIONAL_HEADER64
{
    uint16_t Magic;
    uint8_t  MajorLinkerVersion;
    uint8_t  MinorLinkerVersion;
    uint32_t SizeOfCode;
    uint32_t SizeOfInitializedData;
    uint32_t SizeOfUninitializedData;
    uint32_t AddressOfEntryPoint;
    uint32_t BaseOfCode;
    uint64_t ImageBase;
    uint32_t SectionAlignment;
    uint32_t FileAlignment;
    uint16_t MajorOperatingSystemVersion;
    uint16_t MinorOperatingSystemVersion;
    uint16_t MajorImageVersion;
    uint16_t MinorImageVersion;
    uint16_t MajorSubsystemVersion;
    uint16_t MinorSubsystemVersion;
    uint32_t Win32VersionValue;
    uint32_t SizeOfImage;
    uint32_t SizeOfHeaders;
    uint32_t CheckSum;
    uint16_t Subsystem;
    uint16_t DllCharacteristics;
    uint64_t SizeOfStackReserve;
    uint64_t SizeOfStackCommit;
    uint64_t SizeOfHeapReserve;
    uint64_t SizeOfHeapCommit;
    uint32_t LoaderFlags;
    uint32_t NumberOfRvaAndSizes;
    IMAGE_DATA_DIRECTORY DataDirectory[];
} IMAGE_OPTIONAL_HEADER64, *PIMAGE_OPTIONAL_HEADER64;

#define IMAGE_DIRECTORY_ENTRY_EXPORT    0

typedef struct _IMAGE_EXPORT_DIRECTORY
{
    uint32_t Characteristics;
    uint32_t TimeDateStamp;
    uint16_t MajorVersion;
    uint16_t MinorVersion;
    uint32_t Name;
    uint32_t Base;
    uint32_t NumberOfFunctions;
    uint32_t NumberOfNames;
    uint32_t AddressOfFunctions;
    uint32_t AddressOfNames;
    uint32_t AddressOfNameOrdinals;
} IMAGE_EXPORT_DIRECTORY, *PIMAGE_EXPORT_DIRECTORY;

typedef void *(*load_library_t)(const char *lib);
typedef void *(*get_proc_address_t)(void *module, const char *name);
typedef int32_t *(*get_last_error_t)(void);
typedef int (*attach_console_t)(int32_t pid);
typedef void *(*get_std_handle_t)(int32_t handle);
typedef int8_t (*write_console_t)(void *handle, void *buf, int32_t len,
    int32_t *out, void *unused);
typedef void *(*create_file_t)(const wchar_t *name, int32_t access,
    int32_t mode, void *sec, int32_t disp, int32_t flags, void *temp);
typedef void *(*create_file_mapping_t)(void *file, void *sec, int32_t prot,
    int32_t min, int32_t max, const char *name);
typedef void *(*map_view_of_file_ex_t)(void *mapping, int32_t access,
    int32_t hi, int32_t lo, size_t size, void *base);
typedef int (*close_handle_t)(void *handle);

#define DLL_PROCESS_ATTACH      1
#define STD_ERROR_HANDLE        (-12)
#define INVALID_HANDLE_VALUE    ((void *)(-1))

static inline void e9panic(void)
{
    asm volatile (
        "mov $0x7, %ecx\n"  // FAST_FAIL_FATAL_APP_EXIT
        "int $0x29"         // __fast_fail
    );
    while (true)
        asm volatile ("ud2");
    __builtin_unreachable();
}

/*
 * Write an error message and exit.
 */
static NO_INLINE NO_RETURN void e9error_impl(void *stderr,
    write_console_t WriteConsole, const char *msg, ...)
{
    char buf[BUFSIZ], *str = buf;
    str = e9write_str(str, "e9patch loader error: ");
    va_list ap;
    va_start(ap, msg);
    str = e9write_format(str, msg, ap);
    va_end(ap);
    str = e9write_char(str, '\n');

    size_t len = str - buf;
    WriteConsole(stderr, buf, len, NULL, NULL);
    while (true)
        asm volatile ("ud2");
    __builtin_unreachable();
}
#define e9error(msg, ...)                                                \
    e9error_impl(stderr, WriteConsole, msg, ##__VA_ARGS__)

/*
 * Write a debug message.
 */
static NO_INLINE void e9debug_impl(void *stderr, write_console_t WriteConsole,
    const char *msg, ...)
{
    char buf[BUFSIZ], *str = buf;
    str = e9write_str(str, "e9patch loader debug: ");
    va_list ap;
    va_start(ap, msg);
    str = e9write_format(str, msg, ap);
    va_end(ap);
    str = e9write_char(str, '\n');

    size_t len = str - buf;
    WriteConsole(stderr, buf, len, NULL, NULL);
}
#define e9debug(msg, ...)                                               \
    e9debug_impl(stderr, WriteConsole, msg, ##__VA_ARGS__)

extern "C"
{
    void *e9loader(PEB *peb, const struct e9_config_s *config,
        int32_t reason);
}

asm (
    /*
     * E9Patch loader entry point.
     */
    ".globl _entry\n"
    ".type _entry,@function\n"
    "_entry:\n"
    // %r9 = pointer to config.

    "\tpushq %rcx\n"            // Save DllMain() args
    "\tpushq %rdx\n"
    "\tpushq %r8\n"

    "\tmov %gs:0x60,%rcx\n"     // Call e9loader()
    "\tmov %edx, %r8d\n"
    "\tmov %r9, %rdx\n"
    "\tcallq e9loader\n"
    // %rax = real entry point.

    "\tpop %r8\n"               // Restore DllMain() args
    "\tpop %rdx\n"
    "\tpop %rcx\n"

    "\tjmpq *%rax\n"
);

/*
 * to_lower()
 */
static wchar_t e9towlower(wchar_t c)
{
    if (c >= L'A' && c <= L'Z')
        return L'a' + (c - L'A');
    return c;
}

/*
 * wcscasecmp()
 */
static int e9wcscasecmp(const wchar_t *s1, const wchar_t *s2)
{
    for (; e9towlower(*s1) == e9towlower(*s2) && *s1 != L'\0'; s1++, s2++)
        ;
    return (int)e9towlower(*s2) - (int)e9towlower(*s1);
}

/*
 * strcmp()
 */
static int e9strcmp(const char *s1, const char *s2)
{
    for (; *s1 == *s2 && *s1 != '\0'; s1++, s2++)
        ;
    return (int)*s2 - (int)*s1;
}

/*
 * Main loader code.
 */
void *e9loader(PEB *peb, const struct e9_config_s *config, int32_t reason)
{
    // Step (0): Sanity checks & initialization:
    const uint8_t *loader_base = (const uint8_t *)config;
    const uint8_t *image_base  = loader_base - config->base;
    void *entry = (void *)(image_base + config->entry);
    if (reason != DLL_PROCESS_ATTACH)
        return entry;   // Enforce single execution

    // Step (1): Parse the PEB/LDR for kernel32.dll and our image path:
    PEB_LDR_DATA* ldr = peb->Ldr;
    LIST_ENTRY *curr = ldr->InMemoryOrderModuleList.Flink;
    const uint8_t *kernel32 = NULL;
    const wchar_t *self = NULL;

    while (curr != NULL && curr != &ldr->InMemoryOrderModuleList &&
            (kernel32 == NULL || self == NULL))
    {
        const LDR_DATA_TABLE_ENTRY* entry =
            CONTAINING_RECORD(curr, LDR_DATA_TABLE_ENTRY, InMemoryOrderLinks);
        const UNICODE_STRING *name = &entry->FullDllName;
        if (entry->DllBase == peb->ImageBaseAddress)
            self = name->Buffer;
        name++;     // BaseDllName immediately follows FullDllName
        if (e9wcscasecmp(name->Buffer, L"kernel32.dll") == 0)
            kernel32 = (const uint8_t *)entry->DllBase;
        curr = curr->Flink;
    }
    if (kernel32 == NULL || self == NULL)
        e9panic();

    // Step (2): Parse the kernel32.dll image for GetProcAddress:
    uint32_t pe_offset = *(const uint32_t *)(kernel32 + 0x3c);
    PIMAGE_FILE_HEADER hdr =
        (PIMAGE_FILE_HEADER)(kernel32 + pe_offset + sizeof(uint32_t));
    PIMAGE_OPTIONAL_HEADER64 ohdr =
        (PIMAGE_OPTIONAL_HEADER64)(hdr + 1);
    PIMAGE_EXPORT_DIRECTORY exports = (PIMAGE_EXPORT_DIRECTORY)
        (kernel32 +
            ohdr->DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress);
    const uint32_t *names = (uint32_t *)(kernel32 + exports->AddressOfNames);
    const uint32_t *funcs =
        (uint32_t *)(kernel32 + exports->AddressOfFunctions);
    load_library_t LoadLibrary = NULL;
    get_proc_address_t GetProcAddress = NULL;
    unsigned num_names = exports->NumberOfNames;
    for (unsigned i = 0; i < num_names &&
            (LoadLibrary == NULL || GetProcAddress == NULL); i++)
    {
        const char *name = (const char *)(kernel32 + names[i]);
        if (e9strcmp(name, "LoadLibraryA") == 0)
            LoadLibrary = (load_library_t)(kernel32 + funcs[i]);
        else if (e9strcmp(name, "GetProcAddress") == 0)
            GetProcAddress = (get_proc_address_t)(kernel32 + funcs[i]);
    }
    if (LoadLibrary == NULL || GetProcAddress == NULL)
        e9panic();

    // Step (3): Get critical functions necessary for output:
    void *handle = (void *)kernel32;
    attach_console_t AttachConsole =
        (attach_console_t)GetProcAddress(handle, "AttachConsole");
    get_std_handle_t GetStdHandle =
        (get_std_handle_t)GetProcAddress(handle, "GetStdHandle");
    if (AttachConsole == NULL || GetStdHandle == NULL)
        e9panic();
    (void)AttachConsole(-1);
    void *stderr = GetStdHandle(STD_ERROR_HANDLE);
    write_console_t WriteConsole =
        (write_console_t)GetProcAddress(handle, "WriteConsoleA");
    if (WriteConsole == NULL)
        e9panic();

    if (config->magic[0] != 'E' || config->magic[1] != '9' ||
            config->magic[2] != 'P' || config->magic[3] != 'A' ||
            config->magic[4] != 'T' || config->magic[5] != 'C' ||
            config->magic[6] != 'H' || config->magic[7] != '\0')
        e9error("missing \"E9PATCH\" magic number");

    // Step (4): Get functions necessary for loader:
    get_last_error_t GetLastError =
        (get_last_error_t)GetProcAddress(handle, "GetLastError");
    if (GetLastError == NULL)
        e9error("GetProcAddress(name=\"GetLastError\") failed");
    create_file_t CreateFile =
        (create_file_t)GetProcAddress(handle, "CreateFileW");
    if (CreateFile == NULL)
        e9error("GetProcAddress(name=\"%s\") failed (error=%d)", "CreateFileW",
            GetLastError());
    create_file_mapping_t CreateFileMapping =
        (create_file_mapping_t)GetProcAddress(handle, "CreateFileMappingA");
    if (CreateFileMapping == NULL)
        e9error("GetProcAddress(name=\"%s\") failed (error=%d)",
            "CreateFileMappingA", GetLastError());
    map_view_of_file_ex_t MapViewOfFileEx =
        (map_view_of_file_ex_t)GetProcAddress(handle, "MapViewOfFileEx");
    if (MapViewOfFileEx == NULL)
        e9error("GetProcAddress(name=\"%s\") failed (error=%d)",
            "MapViewOfFileEx", GetLastError());
    close_handle_t CloseHandle =
        (close_handle_t)GetProcAddress(handle, "CloseHandle");
    if (CloseHandle == NULL)
        e9error("GetProcAddress(name=\"%s\") failed (error=%d)",
            "CloseHandle", GetLastError());

    // Step (5): Load the trampoline code:
#define GENERIC_READ            0x80000000
#define GENERIC_EXECUTE         0x20000000
#define OPEN_EXISTING           3
#define FILE_ATTRIBUTE_NORMAL   0x00000080
    void *file = CreateFile(self, GENERIC_READ | GENERIC_EXECUTE, 0, NULL,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, INVALID_HANDLE_VALUE);
    if (file == INVALID_HANDLE_VALUE)
        e9error("CreateFile(path=\"%S\") failed (error=%d)", self,
            GetLastError());

#define PAGE_EXECUTE_READ       0x20
#define PAGE_READONLY           0x02
#define SEC_COMMIT              0x8000000
#define PAGE_EXECUTE_WRITECOPY  0x80
    void *mapping = CreateFileMapping(file, NULL,
        PAGE_EXECUTE_READ | SEC_COMMIT, 0, 0, NULL);
    if (mapping == NULL)
        e9error("CreateFileMapping(file=\"%S\") failed (error=%d)", self,
            GetLastError());

#define FILE_MAP_COPY           0x0001
#define FILE_MAP_READ           0x0004
#define FILE_MAP_EXECUTE        0x0020

    const struct e9_map_s *maps =
        (const struct e9_map_s *)(loader_base + config->maps[1]);
    for (uint32_t i = 0; i < config->num_maps[1]; i++)
    {
        off_t offset = (off_t)maps[i].offset * PAGE_SIZE;
        size_t len   = (size_t)maps[i].size * PAGE_SIZE;
        void *addr   = (void *)((intptr_t)maps[i].addr * PAGE_SIZE);
        int32_t prot = 0x0;
        if (maps[i].w)
            prot |= FILE_MAP_COPY;
        else if (maps[i].r)
            prot |= FILE_MAP_READ;
        prot |= (maps[i].x? FILE_MAP_EXECUTE: 0x0);

        e9debug("MapViewOfFileEx(addr=%p,size=%U,offset=+%U,prot=%c%c%c)",
            addr, len, offset,
            (maps[i].r? 'r': '-'), (maps[i].w? 'w': '-'),
            (maps[i].x? 'x': '-'));

        int32_t offset_lo = (int32_t)offset,
                offset_hi = (int32_t)(offset >> 32);
        void *result = MapViewOfFileEx(mapping, prot, offset_hi, offset_lo,
            len, addr);
        if (result == NULL)
            e9error("MapViewOfFileEx(addr=%p,size=%U,offset=+%U,prot=%c%c%c) "
                "failed (error=%d)", addr, len, offset,
                (maps[i].r? 'r': '-'), (maps[i].w? 'w': '-'),
                (maps[i].x? 'x': '-'), GetLastError());
    }
    if (!CloseHandle(file))
        e9error("failed to close %s handle (error=%d)", "file",
            GetLastError());
    if (!CloseHandle(mapping))
        e9error("failed to close %s handle (error=%d)", "mapping",
            GetLastError());

    // Step (6): Return the entry point:
    e9debug("entry=%p", entry);
    return entry;
}

