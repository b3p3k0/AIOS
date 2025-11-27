#include <efi.h>
#include <efilib.h>
#include <elf.h>

#include "aios/bootinfo.h"

#define KERNEL_PATH L"\\AIOS\\KERNEL.ELF"
#define ALIGN_DOWN(value, align) ((value) & ~((align) - 1))
#define ALIGN_UP(value, align)   (((value) + ((align) - 1)) & ~((align) - 1))

struct loaded_kernel {
    EFI_PHYSICAL_ADDRESS base;
    UINT64 size;
    EFI_PHYSICAL_ADDRESS entry;
};

struct memory_map {
    EFI_MEMORY_DESCRIPTOR *buffer;
    EFI_PHYSICAL_ADDRESS phys_addr;
    UINTN size;
    UINTN pages;
    UINTN key;
    UINTN descriptor_size;
    UINT32 descriptor_version;
};

static EFI_STATUS open_root(EFI_HANDLE image_handle, EFI_FILE_PROTOCOL **root);
static EFI_STATUS read_kernel_file(EFI_FILE_PROTOCOL *root, VOID **buffer, UINTN *size);
static EFI_STATUS load_kernel_image(VOID *buffer, UINTN size, struct loaded_kernel *out);
static EFI_STATUS prepare_memory_map(struct memory_map *map);
static EFI_STATUS exit_boot_services_with_map(EFI_HANDLE image_handle, struct memory_map *map);
static EFI_PHYSICAL_ADDRESS find_rsdp(void);
static EFI_STATUS query_framebuffer(struct aios_framebuffer *fb);
static VOID free_memory_map(struct memory_map *map);

EFI_STATUS EFIAPI efi_main(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE *system_table) {
    InitializeLib(image_handle, system_table);

    Print(L"AIOS loader — Phase 1 prototype\r\n");

    EFI_FILE_PROTOCOL *root = NULL;
    EFI_STATUS status = open_root(image_handle, &root);
    if (EFI_ERROR(status)) {
        Print(L"Failed to open filesystem: %r\r\n", status);
        return status;
    }

    VOID *kernel_file = NULL;
    UINTN kernel_size = 0;
    status = read_kernel_file(root, &kernel_file, &kernel_size);
    if (EFI_ERROR(status)) {
        Print(L"Unable to read %s: %r\r\n", KERNEL_PATH, status);
        return status;
    }

    struct loaded_kernel kernel = {0};
    status = load_kernel_image(kernel_file, kernel_size, &kernel);
    uefi_call_wrapper(BS->FreePool, 1, kernel_file);
    if (EFI_ERROR(status)) {
        Print(L"ELF load failed: %r\r\n", status);
        return status;
    }

    struct aios_boot_info boot = {
        .magic = AIOS_BOOTINFO_MAGIC,
        .version = AIOS_BOOTINFO_VERSION,
        .kernel_base = kernel.base,
        .kernel_size = kernel.size,
        .entry_point = kernel.entry,
        .rsdp_address = find_rsdp(),
    };

    query_framebuffer(&boot.framebuffer);

    struct memory_map map = {0};
    while (TRUE) {
        status = prepare_memory_map(&map);
        if (EFI_ERROR(status)) {
            Print(L"GetMemoryMap failed: %r\r\n", status);
            free_memory_map(&map);
            return status;
        }
        boot.memory_map.buffer = map.phys_addr;
        boot.memory_map.size = map.size;
        boot.memory_map.descriptor_size = map.descriptor_size;
        boot.memory_map.descriptor_version = map.descriptor_version;

        status = exit_boot_services_with_map(image_handle, &map);
        if (status == EFI_SUCCESS) {
            break;
        }
        if (status != EFI_INVALID_PARAMETER) {
            Print(L"ExitBootServices error: %r\r\n", status);
            free_memory_map(&map);
            return status;
        }
        /* Memory map changed between calls. Retry. */
    }

    free_memory_map(&map);

    typedef void (*kernel_entry_t)(struct aios_boot_info *);
    kernel_entry_t entry = (kernel_entry_t)(UINTN)kernel.entry;
    entry(&boot);

    return EFI_SUCCESS;
}

static EFI_STATUS open_root(EFI_HANDLE image_handle, EFI_FILE_PROTOCOL **root) {
    EFI_LOADED_IMAGE_PROTOCOL *loaded_image = NULL;
    EFI_STATUS status = uefi_call_wrapper(BS->HandleProtocol, 3, image_handle, &gEfiLoadedImageProtocolGuid, (VOID **)&loaded_image);
    if (EFI_ERROR(status)) {
        return status;
    }

    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs = NULL;
    status = uefi_call_wrapper(BS->HandleProtocol, 3, loaded_image->DeviceHandle, &gEfiSimpleFileSystemProtocolGuid, (VOID **)&fs);
    if (EFI_ERROR(status)) {
        return status;
    }

    return uefi_call_wrapper(fs->OpenVolume, 2, fs, root);
}

static EFI_STATUS read_kernel_file(EFI_FILE_PROTOCOL *root, VOID **buffer, UINTN *size) {
    EFI_FILE_PROTOCOL *file = NULL;
    EFI_STATUS status = uefi_call_wrapper(root->Open, 5, root, &file, KERNEL_PATH, EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(status)) {
        return status;
    }

    UINTN info_size = 0;
    status = uefi_call_wrapper(file->GetInfo, 4, file, &gEfiFileInfoGuid, &info_size, NULL);
    if (status != EFI_BUFFER_TOO_SMALL) {
        uefi_call_wrapper(file->Close, 1, file);
        return status;
    }

    EFI_FILE_INFO *info = NULL;
    status = uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData, info_size, (VOID **)&info);
    if (EFI_ERROR(status)) {
        uefi_call_wrapper(file->Close, 1, file);
        return status;
    }

    status = uefi_call_wrapper(file->GetInfo, 4, file, &gEfiFileInfoGuid, &info_size, info);
    if (EFI_ERROR(status)) {
        uefi_call_wrapper(BS->FreePool, 1, info);
        uefi_call_wrapper(file->Close, 1, file);
        return status;
    }

    *size = info->FileSize;
    status = uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData, *size, buffer);
    if (EFI_ERROR(status)) {
        uefi_call_wrapper(BS->FreePool, 1, info);
        uefi_call_wrapper(file->Close, 1, file);
        return status;
    }

    UINTN read_size = *size;
    status = uefi_call_wrapper(file->Read, 3, file, &read_size, *buffer);
    uefi_call_wrapper(BS->FreePool, 1, info);
    uefi_call_wrapper(file->Close, 1, file);
    if (EFI_ERROR(status) || read_size != *size) {
        uefi_call_wrapper(BS->FreePool, 1, *buffer);
        *buffer = NULL;
        return EFI_BAD_BUFFER_SIZE;
    }
    return EFI_SUCCESS;
}

static BOOLEAN is_valid_elf(const Elf64_Ehdr *ehdr) {
    return ehdr->e_ident[EI_MAG0] == ELFMAG0 &&
           ehdr->e_ident[EI_MAG1] == ELFMAG1 &&
           ehdr->e_ident[EI_MAG2] == ELFMAG2 &&
           ehdr->e_ident[EI_MAG3] == ELFMAG3 &&
           ehdr->e_ident[EI_CLASS] == ELFCLASS64 &&
           ehdr->e_type == ET_EXEC;
}

static EFI_STATUS load_kernel_image(VOID *buffer, UINTN size, struct loaded_kernel *out) {
    if (size < sizeof(Elf64_Ehdr)) {
        return EFI_LOAD_ERROR;
    }

    Elf64_Ehdr *ehdr = (Elf64_Ehdr *)buffer;
    if (!is_valid_elf(ehdr)) {
        return EFI_UNSUPPORTED;
    }

    Elf64_Phdr *phdrs = (Elf64_Phdr *)((UINT8 *)buffer + ehdr->e_phoff);
    UINT64 first = ~0ULL;
    UINT64 last = 0;
    for (UINT16 i = 0; i < ehdr->e_phnum; ++i) {
        Elf64_Phdr *ph = &phdrs[i];
        if (ph->p_type != PT_LOAD || ph->p_memsz == 0) {
            continue;
        }
        if (ph->p_paddr < first) {
            first = ph->p_paddr;
        }
        UINT64 segment_end = ph->p_paddr + ph->p_memsz;
        if (segment_end > last) {
            last = segment_end;
        }
    }

    if (first == ~0ULL) {
        return EFI_LOAD_ERROR;
    }

    UINT64 aligned_base = ALIGN_DOWN(first, EFI_PAGE_SIZE);
    UINT64 aligned_size = ALIGN_UP(last - aligned_base, EFI_PAGE_SIZE);
    UINTN pages = aligned_size / EFI_PAGE_SIZE;

    EFI_PHYSICAL_ADDRESS dest = aligned_base;
    EFI_STATUS status = uefi_call_wrapper(BS->AllocatePages, 4, AllocateAddress, EfiLoaderData, pages, &dest);
    if (EFI_ERROR(status)) {
        return status;
    }

    for (UINT16 i = 0; i < ehdr->e_phnum; ++i) {
        Elf64_Phdr *ph = &phdrs[i];
        if (ph->p_type != PT_LOAD || ph->p_memsz == 0) {
            continue;
        }
        UINT8 *src = (UINT8 *)buffer + ph->p_offset;
        UINT8 *dst = (UINT8 *)(UINTN)ph->p_paddr;
        CopyMem(dst, src, ph->p_filesz);
        if (ph->p_memsz > ph->p_filesz) {
            SetMem(dst + ph->p_filesz, ph->p_memsz - ph->p_filesz, 0);
        }
    }

    out->base = aligned_base;
    out->size = aligned_size;
    out->entry = ehdr->e_entry;
    return EFI_SUCCESS;
}

static EFI_STATUS prepare_memory_map(struct memory_map *map) {
    if (map->buffer != NULL && map->pages != 0) {
        uefi_call_wrapper(BS->FreePages, 2, map->phys_addr, map->pages);
        map->buffer = NULL;
        map->pages = 0;
    }

    map->size = 0;
    EFI_STATUS status = uefi_call_wrapper(BS->GetMemoryMap, 5, &map->size, map->buffer, &map->key, &map->descriptor_size, &map->descriptor_version);
    if (status != EFI_BUFFER_TOO_SMALL) {
        return status;
    }

    map->size += map->descriptor_size * 2;
    map->pages = EFI_SIZE_TO_PAGES(map->size);
    status = uefi_call_wrapper(BS->AllocatePages, 4, AllocateAnyPages, EfiLoaderData, map->pages, &map->phys_addr);
    if (EFI_ERROR(status)) {
        return status;
    }

    map->buffer = (EFI_MEMORY_DESCRIPTOR *)(UINTN)map->phys_addr;
    return uefi_call_wrapper(BS->GetMemoryMap, 5, &map->size, map->buffer, &map->key, &map->descriptor_size, &map->descriptor_version);
}

static EFI_STATUS exit_boot_services_with_map(EFI_HANDLE image_handle, struct memory_map *map) {
    return uefi_call_wrapper(BS->ExitBootServices, 2, image_handle, map->key);
}

static EFI_PHYSICAL_ADDRESS find_rsdp(void) {
    static EFI_GUID acpi2 = ACPI_20_TABLE_GUID;
    static EFI_GUID acpi1 = ACPI_TABLE_GUID;

    for (UINTN i = 0; i < ST->NumberOfTableEntries; ++i) {
        EFI_CONFIGURATION_TABLE *table = &ST->ConfigurationTable[i];
        if (CompareGuid(&table->VendorGuid, &acpi2) || CompareGuid(&table->VendorGuid, &acpi1)) {
            return (EFI_PHYSICAL_ADDRESS)(UINTN)table->VendorTable;
        }
    }
    return 0;
}

static EFI_STATUS query_framebuffer(struct aios_framebuffer *fb) {
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = NULL;
    EFI_STATUS status = uefi_call_wrapper(BS->LocateProtocol, 3, &gEfiGraphicsOutputProtocolGuid, NULL, (VOID **)&gop);
    if (EFI_ERROR(status)) {
        fb->base = 0;
        fb->width = fb->height = fb->pixels_per_scanline = fb->bpp = 0;
        return status;
    }

    fb->base = gop->Mode->FrameBufferBase;
    fb->width = gop->Mode->Info->HorizontalResolution;
    fb->height = gop->Mode->Info->VerticalResolution;
    fb->pixels_per_scanline = gop->Mode->Info->PixelsPerScanLine;
    fb->bpp = 32; /* GOP always exposes linear framebuffer for us */
    return EFI_SUCCESS;
}

static VOID free_memory_map(struct memory_map *map) {
    if (map->buffer && map->pages) {
        uefi_call_wrapper(BS->FreePages, 2, map->phys_addr, map->pages);
        map->buffer = NULL;
        map->pages = 0;
    }
}
