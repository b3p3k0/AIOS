# AIOS 02 — Environment + Boot Test

Phase 0 proves we can boot a custom UEFI application inside a virtual machine using only Ubuntu 24.04 packages. Once this works we have high confidence future bootloader/kernels will execute the same way, because UEFI firmware always looks for `/EFI/BOOT/BOOTX64.EFI` on a FAT-formatted ESP for x86_64 fallback boots. [UEFI Spec v2.10 §3.5.1](https://uefi.org/specifications)

## Host Requirements
- Ubuntu 24.04 with sudo privileges and at least 2 GB free disk space.
- Hardware virtualization enabled (Intel VT-x or AMD-V) so QEMU can use `-accel kvm` for speed; the script automatically falls back to TCG if KVM is unavailable.
- Network access for apt repositories the first time you run `make deps`.

## Why UEFI + FAT + QEMU/OVMF?
- **UEFI everywhere:** Modern PCs and cloud hypervisors ship UEFI firmware, so targeting it keeps AIOS aligned with real hardware instead of legacy BIOS pathways. The spec mandates that removable media boot by scanning `/EFI/BOOT/BOOTX64.EFI` on a FAT32 partition, which is exactly the layout we script. [UEFI Spec v2.10 §3.5.1](https://uefi.org/specifications)
- **OVMF (Open Virtual Machine Firmware):** TianoCore’s OVMF builds provide the same UEFI services as physical firmware but run inside QEMU, letting us iterate without hardware risks. [TianoCore OVMF Notes](https://github.com/tianocore/tianocore.github.io/wiki/OVMF)
- **GNU-EFI headers/libraries:** Ubuntu’s `gnu-efi` package exposes official EFI type definitions plus startup glue, so we can compile `efi_main` with stock `x86_64-linux-gnu-gcc` instead of maintaining a separate cross-compiler. [GNU-EFI Project](https://sourceforge.net/projects/gnu-efi/)
- **FAT32 disk image:** The ESP defined by UEFI must be FAT12/16/32; FAT32 works for our 64 MB disk and is straightforward to create with `mkfs.vfat` + `mtools`.

## Step-by-Step Usage
All commands run from the repo root (`AIOS/`).

1. **Install dependencies**
   ```bash
   make deps
   ```
   This wraps `scripts/setup_env.sh deps`, calling `apt-get` for toolchain packages (`build-essential`, `gnu-efi`, `qemu-system-x86`, `ovmf`, `mtools`, `dosfstools`, etc.).

2. **Build the hello-efi probe + disk image**
   ```bash
   make image
   ```
   - Compiles `bootloader/hello-efi/main.c` using GNU-EFI headers, producing `build/hello-efi/hello.so`.
   - Converts the shared object into the PE32+ binary `build/esp/EFI/BOOT/BOOTX64.EFI` with `objcopy`.
   - Creates `images/aios-efi.img`, formats it as FAT32, and copies the loader to `/EFI/BOOT/BOOTX64.EFI` using `mtools`.

3. **Boot inside QEMU/OVMF**
   ```bash
   make run
   ```
   The script launches:
   - `qemu-system-x86_64 -machine q35,accel=kvm:tcg -cpu host -m 512`
   - Adds two pflash drives for `OVMF_CODE.fd` and a writable copy of `OVMF_VARS.fd`.
   - Attaches `images/aios-efi.img` as a virtio disk so the firmware discovers the ESP and auto-loads `BOOTX64.EFI`.

4. **Verify output**
   - The OVMF splash should appear briefly, followed by the AIOS console text:
     ```
     AIOS minimal EFI probe
     Hello from AIOS - EFI test!
     Firmware handed us to /EFI/BOOT/BOOTX64.EFI, so wiring works.
     Press any key to exit...
     ```
   - Press any key to release control back to QEMU and quit.

5. **Clean up (optional)**
   ```bash
   make clean
   ```
   Removes `build/` and the FAT image so you can rerun the process from scratch.

## Design Notes
- `scripts/setup_env.sh` is intentionally verbose and linear; each function mirrors a lecture topic (dependencies, compiling EFI, packaging, virtualization) so students can read the script alongside the doc.
- Inline comments reference the same authoritative docs cited above, reinforcing the “docs first, code second” goal.
- The Makefile is a tiny UX wrapper; using plain GNU Make keeps friction low versus bespoke task runners.

## Troubleshooting
- **Missing OVMF firmware**: If `/usr/share/OVMF/OVMF_CODE.fd` is absent, (re)install `ovmf` via `sudo apt-get install ovmf`.
- **Permission errors during apt**: ensure your user is in the sudoers list; the script bails early if `sudo` is unavailable.
- **Blank QEMU window**: verify your CPU virtualization extension is enabled in firmware settings. Without it QEMU falls back to TCG (slower but functional), yet some hosts disable virtualization entirely.
- **Garbage console text**: make sure your terminal supports UTF-16 output; GNU-EFI’s `Print` handles conversion, but mixing host locales can still garble text. Running QEMU with `-serial stdio` keeps output ASCII-safe.

## File Reference
- `scripts/setup_env.sh`: orchestration script described here.
- `bootloader/hello-efi/main.c`: hello-world EFI app.
- `images/aios-efi.img`: generated FAT32 disk image (ignored until created).
