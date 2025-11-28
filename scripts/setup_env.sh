#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"
EFI_BUILD_DIR="$BUILD_DIR/loader"
KERNEL_BUILD_DIR="$BUILD_DIR/kernel"
ESP_STAGING="$BUILD_DIR/esp"
IMAGE_DIR="$PROJECT_ROOT/images"
IMAGE_PATH="$IMAGE_DIR/aios-efi.img"
IMAGE_SIZE="${IMAGE_SIZE:-64M}"
DATA_IMAGE="$IMAGE_DIR/aios-data.img"
DATA_IMAGE_SIZE="${DATA_IMAGE_SIZE:-32M}"
EFI_BINARY="$ESP_STAGING/EFI/BOOT/BOOTX64.EFI" # UEFI removable-media fallback. Spec §3.5.1.
KERNEL_BINARY="$ESP_STAGING/AIOS/KERNEL.ELF"
KERNEL_ELF="$KERNEL_BUILD_DIR/kernel.elf"

OVMF_CODE_DEFAULT="/usr/share/OVMF/OVMF_CODE.fd"
OVMF_VARS_TEMPLATE_DEFAULT="/usr/share/OVMF/OVMF_VARS.fd" # TianoCore/OVMF template vars.
OVMF_CODE="${OVMF_CODE:-$OVMF_CODE_DEFAULT}"
OVMF_VARS_TEMPLATE="${OVMF_VARS_TEMPLATE:-$OVMF_VARS_TEMPLATE_DEFAULT}"
OVMF_VARS="$BUILD_DIR/OVMF_VARS.fd"

OVMF_CODE_CANDIDATES=(
    /usr/share/OVMF/OVMF_CODE.fd
    /usr/share/OVMF/OVMF_CODE_4M.fd
    /usr/share/OVMF/OVMF_CODE.secboot.fd
    /usr/share/ovmf/OVMF_CODE.fd
    /usr/share/edk2/ovmf/OVMF_CODE.fd
)
OVMF_VARS_CANDIDATES=(
    /usr/share/OVMF/OVMF_VARS.fd
    /usr/share/OVMF/OVMF_VARS_4M.fd
    /usr/share/OVMF/OVMF_VARS.secboot.fd
    /usr/share/ovmf/OVMF_VARS.fd
    /usr/share/edk2/ovmf/OVMF_VARS.fd
)

GNU_EFI_LDS_CANDIDATES=(
    /usr/lib/gnu-efi/elf_x86_64_efi.lds
    /usr/lib/elf_x86_64_efi.lds
    /usr/lib/x86_64-linux-gnu/gnu-efi/elf_x86_64_efi.lds
)
GNU_EFI_CRT0_CANDIDATES=(
    /usr/lib/gnu-efi/x86_64/crt0-efi-amd64.o
    /usr/lib/crt0-efi-x86_64.o
    /usr/lib/x86_64-linux-gnu/gnu-efi/crt0-efi-amd64.o
)

log() {
    printf '[setup_env] %s\n' "$1"
}

find_artifact() {
    local label="$1"
    local env_hint="$2"
    shift 2
    for candidate in "$@"; do
        if [[ -f "$candidate" ]]; then
            echo "$candidate"
            return 0
        fi
    done
    cat >&2 <<EOF
Missing $label. Install the appropriate package or set $env_hint to point at the
correct file.
EOF
    return 1
}

need_root_packages() {
    if ! command -v sudo >/dev/null 2>&1; then
        echo "sudo is required to install packages" >&2
        exit 1
    fi
}

install_deps() {
    log "Installing Ubuntu 24.04 build dependencies via apt"
    need_root_packages
    sudo apt-get update
    # Noninteractive mode prevents debconf prompts from stalling CI/agent runs.
    sudo DEBIAN_FRONTEND=noninteractive apt-get install -y \
        build-essential \
        clang \
        lld \
        gnu-efi \
        uuid-dev \
        qemu-system-x86 \
        ovmf \
        mtools \
        dosfstools \
        make \
        python3 \
        curl
    log "Dependencies installed"
}

ensure_dirs() {
    mkdir -p "$EFI_BUILD_DIR" "$KERNEL_BUILD_DIR" "$ESP_STAGING/EFI/BOOT" "$ESP_STAGING/AIOS" "$IMAGE_DIR"
}

build_loader() {
    ensure_dirs
    local src="$PROJECT_ROOT/bootloader/hello-efi/main.c"
    local obj="$EFI_BUILD_DIR/main.o"
    local so="$EFI_BUILD_DIR/hello.so"
    local lds="${EFI_LDS:-$(find_artifact 'elf_x86_64_efi.lds' 'EFI_LDS' "${GNU_EFI_LDS_CANDIDATES[@]}")}"
    local crt0="${EFI_CRT0:-$(find_artifact 'crt0-efi (x86_64)' 'EFI_CRT0' "${GNU_EFI_CRT0_CANDIDATES[@]}")}"
    local crt0_dir="$(dirname "$crt0")"

    local accel_cflag='-DACCEL_MODE="TCG"'
    if [[ -e /dev/kvm && -r /dev/kvm && -w /dev/kvm ]]; then
        accel_cflag='-DACCEL_MODE="KVM"'
    fi

    log "Compiling AIOS UEFI loader"
    # GNU-EFI exposes canonical typedefs/startup glue so stock GCC can target
    # PE32+ EFI binaries without a custom cross-toolchain. [GNU-EFI —
    # https://sourceforge.net/projects/gnu-efi/]
    x86_64-linux-gnu-gcc \
        -I/usr/include/efi \
        -I/usr/include/efi/x86_64 \
        -I"$PROJECT_ROOT/include" \
        "$accel_cflag" \
        -fPIC \
        -fshort-wchar \
        -mno-red-zone \
        -DEFI_FUNCTION_WRAPPER \
        -Wall -Wextra -Werror \
        -c "$src" -o "$obj"

    log "Linking PE/COFF EFI binary"
    ld \
        -nostdlib \
        -znocombreloc \
        -shared \
        -Bsymbolic \
        -L/usr/lib \
        -L"$crt0_dir" \
        -T "$lds" \
        "$obj" \
        "$crt0" \
        -lgnuefi -lefi \
        -o "$so"

    log "Stripping sections to create BOOTX64.EFI"
    objcopy \
        --target=efi-app-x86_64 \
        -j .text -j .sdata -j .data -j .dynamic -j .dynsym -j .rel -j .rela -j .reloc \
        "$so" "$EFI_BINARY"
}

build_kernel() {
    ensure_dirs
    log "Compiling AIOS kernel stub (ELF)"
    local cflags=(
        -ffreestanding
        -fno-stack-protector
        -fno-pie
        -mno-red-zone
        -m64
        -mcmodel=large
        -Wall
        -Wextra
        -Werror
        -nostdlib
        -I"$PROJECT_ROOT/include"
        -I"$PROJECT_ROOT"
        -I"$PROJECT_ROOT/kernel"
        -I"$PROJECT_ROOT/kernel/fs"
    )

    local objs=()
    for src in \
        "$PROJECT_ROOT/kernel/main.c" \
        "$PROJECT_ROOT/kernel/serial.c" \
        "$PROJECT_ROOT/kernel/util.c" \
        "$PROJECT_ROOT/kernel/mem.c" \
        "$PROJECT_ROOT/kernel/fs/blockdev.c" \
        "$PROJECT_ROOT/kernel/fs/fs.c" \
        "$PROJECT_ROOT/kernel/shell.c" \
        "$PROJECT_ROOT/kernel/virtio_blk.c"; do
        local obj="$KERNEL_BUILD_DIR/$(basename "${src%.*}").o"
        x86_64-linux-gnu-gcc "${cflags[@]}" -c "$src" -o "$obj"
        objs+=("$obj")
    done

    x86_64-linux-gnu-ld \
        -nostdlib \
        -z max-page-size=0x1000 \
        -T "$PROJECT_ROOT/kernel/link.ld" \
        -o "$KERNEL_ELF" \
        "${objs[@]}"
}

create_image() {
    ensure_dirs
    if [[ ! -f "$EFI_BINARY" || ! -f "$KERNEL_ELF" ]]; then
        echo "Loader or kernel missing. Run '$0 build' first." >&2
        exit 1
    fi

    log "Creating FAT32 disk image at $IMAGE_PATH"
    rm -f "$IMAGE_PATH"
    truncate -s "$IMAGE_SIZE" "$IMAGE_PATH"
    mkfs.vfat -F 32 -n AIOS_EFI "$IMAGE_PATH"

    log "Copying artifacts into EFI system partition"
    mmd -i "$IMAGE_PATH" ::EFI ::EFI/BOOT ::AIOS
    mcopy -i "$IMAGE_PATH" "$EFI_BINARY" ::/EFI/BOOT/BOOTX64.EFI
    mcopy -i "$IMAGE_PATH" "$KERNEL_ELF" ::/AIOS/KERNEL.ELF

    local startup_nsh="$ESP_STAGING/startup.nsh"
    cat >"$startup_nsh" <<'EOF'
@echo -off
fs0:\EFI\BOOT\BOOTX64.EFI
EOF
    mcopy -i "$IMAGE_PATH" "$startup_nsh" ::/startup.nsh

    if [[ ! -f "$DATA_IMAGE" ]]; then
        log "Creating virtio data disk at $DATA_IMAGE"
        truncate -s "$DATA_IMAGE_SIZE" "$DATA_IMAGE"
    fi
}

prepare_ovmf_vars() {
    OVMF_CODE="$(find_artifact 'OVMF_CODE firmware' 'OVMF_CODE' "$OVMF_CODE" "${OVMF_CODE_CANDIDATES[@]}")"
    OVMF_VARS_TEMPLATE="$(find_artifact 'OVMF_VARS template' 'OVMF_VARS_TEMPLATE' "$OVMF_VARS_TEMPLATE" "${OVMF_VARS_CANDIDATES[@]}")"
    mkdir -p "$BUILD_DIR"
    if [[ ! -f "$OVMF_VARS" ]]; then
        cp "$OVMF_VARS_TEMPLATE" "$OVMF_VARS"
    fi
}

run_qemu() {
    if [[ ! -f "$IMAGE_PATH" ]]; then
        echo "Disk image missing at $IMAGE_PATH. Run '$0 image' first." >&2
        exit 1
    fi
    prepare_ovmf_vars

    local accel="tcg"
    local cpu="qemu64"
    if [[ -e /dev/kvm && -r /dev/kvm && -w /dev/kvm ]]; then
        accel="kvm"
        cpu="host"
    fi

    log "Launching QEMU with OVMF"
    # TianoCore's OVMF firmware emulates a UEFI board so we can validate the
    # firmware->bootloader contract in QEMU before hardware trials. [OVMF —
    # https://github.com/tianocore/tianocore.github.io/wiki/OVMF]
    qemu-system-x86_64 \
        -machine q35,accel=$accel \
        -cpu $cpu \
        -m 512 \
        -drive if=pflash,format=raw,readonly=on,file="$OVMF_CODE" \
        -drive if=pflash,format=raw,file="$OVMF_VARS" \
        -drive if=ide,format=raw,file="$IMAGE_PATH" \
        -drive if=none,format=raw,file="$DATA_IMAGE",id=aiosdata \
        -device virtio-blk-pci,drive=aiosdata,disable-modern=on \
        -serial stdio
}

clean_build() {
    log "Removing build artifacts"
    rm -rf "$BUILD_DIR"
    rm -f "$IMAGE_PATH"
}

usage() {
    cat <<'USAGE'
Usage: scripts/setup_env.sh <command>

Commands:
  deps     Install Ubuntu packages required for AIOS Phase 0.
  build    Compile the UEFI loader and ELF kernel.
  image    Build loader+kernel (if needed) and generate the FAT32 disk image.
  run      Launch QEMU/OVMF using the generated disk image.
  all      deps -> build -> image -> run (default when no command is given).
  clean    Remove build artifacts and disk images.
  help     Show this message.
USAGE
}

main() {
    local cmd="${1:-all}"
    case "$cmd" in
        deps)
            install_deps
            ;;
        build)
            build_loader
            build_kernel
            ;;
        image)
            build_loader
            build_kernel
            create_image
            ;;
        run)
            run_qemu
            ;;
        all)
            install_deps
            build_loader
            build_kernel
            create_image
            run_qemu
            ;;
        clean)
            clean_build
            ;;
        help|-h|--help)
            usage
            ;;
        *)
            echo "Unknown command: $cmd" >&2
            usage
            exit 1
            ;;
    esac
}

main "$@"
