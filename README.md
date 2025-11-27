# AIOS — Minimal Educational OS (Phase 0)

AIOS walks readers from power-on to a toy 64-bit kernel with guardrail-quality documentation. Phase 0 focuses on setting up a reproducible Ubuntu 24.04 workspace, building a "hello UEFI" probe, and proving that QEMU + OVMF boot straight into our binary.

## Quick Start
1. Clone this repo on Ubuntu 24.04 with sudo privileges and virtualization enabled.
2. If `make`, `gnu-efi`, or `uuid-dev` aren’t installed yet (common on stock Ubuntu Desktop VMs), run `sudo apt-get update && sudo apt-get install -y make gnu-efi uuid-dev ovmf`.
3. Run `make deps` once to install build, image, and virtualization tooling.
4. Run `make image` to compile `bootloader/hello-efi` and stage `/EFI/BOOT/BOOTX64.EFI` inside `images/aios-efi.img`.
5. Run `make run` to launch QEMU with the system disk plus OVMF firmware. You should see "Hello from AIOS – EFI test" in the firmware console; press any key to exit.

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
├── kernel/
├── scripts/
│   └── setup_env.sh
├── docs/
│   ├── 00_intro.md
│   ├── 01_architecture.md
│   ├── 02_environment.md
│   └── 03_bootloader.md
└── images/
```

- `bootloader/hello-efi`: holds the sample UEFI application compiled via `gnu-efi` headers/libraries.
- `scripts/setup_env.sh`: installs dependencies, builds the EFI binary, produces a FAT32 disk image, and runs QEMU/OVMF for verification.
- `docs/02_environment.md`: detailed walkthrough of the setup commands, tooling rationale, and verification steps. (Phase 1+ docs are placeholders for now.)

## Next Steps
- Flesh out the docs placeholders (`00_intro`, `01_architecture`, `03_bootloader`) as subsequent milestones land.
- Replace the hello-world EFI stub with the real AIOS loader once Phase 1 requirements exist.
- Track design decisions in `docs/` so the code continues to validate the prose—not vice versa.
