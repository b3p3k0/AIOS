# AIOS — Minimal Educational OS (Phase 1)

AIOS walks readers from power-on to a toy 64-bit kernel with guardrail-quality documentation. Phase 1 evolves the project from a "hello UEFI" probe into a real chain-loader that loads an AIOS kernel ELF, passes along boot info, exits boot services, and jumps into our freestanding kernel stub.

## Quick Start
1. Clone this repo on Ubuntu 24.04 with sudo privileges and virtualization enabled.
2. If `make`, `gnu-efi`, or `uuid-dev` aren’t installed yet (common on stock Ubuntu Desktop VMs), run `sudo apt-get update && sudo apt-get install -y make gnu-efi uuid-dev ovmf`.
3. Run `make deps` once to install build, image, and virtualization tooling.
4. Run `make image` to compile the UEFI loader + ELF kernel, then stage `/EFI/BOOT/BOOTX64.EFI` and `/AIOS/KERNEL.ELF` inside `images/aios-efi.img`.
5. Run `make run` to launch QEMU with the system disk plus OVMF firmware. You should see serial output like `AIOS kernel stub online` in your terminal (we wire QEMU `-serial stdio`). On hosts without `/dev/kvm` (e.g., nested VirtualBox), the script auto-falls back to TCG and uses a generic `qemu64` CPU model.

If `make run` complains about missing `OVMF_CODE.fd`, either install `ovmf` as above or point the script at your firmware path:
```bash
export OVMF_CODE=/usr/share/OVMF/OVMF_CODE_4M.fd
export OVMF_VARS_TEMPLATE=/usr/share/OVMF/OVMF_VARS_4M.fd
make run
```

Each Makefile target wraps `scripts/setup_env.sh`, so you can also call the script directly (`./scripts/setup_env.sh help`).

## Repository Layout
```
AIOS/
├── README.md
├── Makefile
├── bootloader/
│   └── hello-efi/
│       └── main.c
├── include/
│   └── aios/bootinfo.h
├── kernel/
│   ├── link.ld
│   ├── main.c
│   ├── serial.c
│   ├── serial.h
│   ├── util.c
│   └── util.h
├── scripts/
│   └── setup_env.sh
├── docs/
│   ├── 00_intro.md
│   ├── 01_architecture.md
│   ├── 02_environment.md
│   └── 03_bootloader.md
└── images/
```

- `bootloader/hello-efi`: AIOS loader that reads `/AIOS/KERNEL.ELF`, parses ELF headers, collects boot info, exits boot services, and jumps into the kernel.
- `kernel/`: freestanding ELF kernel stub that prints boot context over the emulated serial port, proving the loader handoff works.
- `include/aios/bootinfo.h`: shared contract for loader ⇄ kernel handoff data.
- `scripts/setup_env.sh`: installs dependencies, builds loader+kernel, produces the FAT32 disk image, and runs QEMU/OVMF for verification.
- `docs/02_environment.md`: detailed walkthrough of the setup commands, tooling rationale, and verification steps. (New lessons live in `drafts/` until the curriculum pass.)

## Next Steps
- Flesh out the docs placeholders (`00_intro`, `01_architecture`, `03_bootloader`) with Phase 1 content using the notes in `drafts/`.
- Teach the loader to support additional kernel conveniences (e.g., paging bootstrap, checksum validation) as we grow the curriculum.
- Track design decisions in `docs/` so the code continues to validate the prose—not vice versa.
