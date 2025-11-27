#include <efi.h>
#include <efilib.h>

/*
 * UEFI applications must expose efi_main with the signature specified in the
 * UEFI Specification v2.10, Section 3.1.2. The firmware locates BOOTX64.EFI in
 * /EFI/BOOT and transfers control here. [UEFI Spec v2.10 §3.1.2 —
 * https://uefi.org/specifications]
 */
EFI_STATUS EFIAPI efi_main(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE *system_table) {
    InitializeLib(image_handle, system_table);

    Print(L"\r\nAIOS minimal EFI probe\r\n");
    Print(L"Hello from AIOS - EFI test!\r\n");
    Print(L"Firmware handed us to /EFI/BOOT/BOOTX64.EFI, so wiring works.\r\n");

    /*
     * Stall until the user presses a key so QEMU/OVMF output stays visible
     * long enough for screenshots/logging during lessons.
     */
    UINTN index = 0;
    EFI_EVENT events[1] = { ST->ConIn->WaitForKey };
    Print(L"Press any key to exit...\r\n");
    uefi_call_wrapper(BS->WaitForEvent, 3, 1, events, &index);

    return EFI_SUCCESS;
}
