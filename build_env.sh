#!/bin/bash

set -e

with_tuna_mirror() {
    local CODENAME
    CODENAME=$(lsb_release -cs)
    
    local TEMP_SOURCES="/tmp/temp_sources_$$.list"
    
    cat > "$TEMP_SOURCES" << EOF
deb https://mirrors.tuna.tsinghua.edu.cn/ubuntu/ $CODENAME main restricted universe multiverse
deb https://mirrors.tuna.tsinghua.edu.cn/ubuntu/ $CODENAME-updates main restricted universe multiverse
deb https://mirrors.tuna.tsinghua.edu.cn/ubuntu/ $CODENAME-backports main restricted universe multiverse
deb https://mirrors.tuna.tsinghua.edu.cn/ubuntu/ $CODENAME-security main restricted universe multiverse
EOF
    
    sudo apt -o Dir::Etc::sourcelist="$TEMP_SOURCES" -o Dir::Etc::sourceparts="-" -o APT::Get::List-Cleanup="0" "$@"
    
    rm -f "$TEMP_SOURCES"
}

download_and_extract_musl() {
    local url="$1"
    local filename=$(basename "$url")
    local tmp_file="/tmp/$filename"
    local target_dir="/opt/musl-cross"
    
    echo "handling $filename..."
    
    local arch_name=$(basename "$filename" .tgz)
    local check_tool="${arch_name%-cross}-gcc"
    
    if [ -f "$target_dir/bin/$check_tool" ]; then
        echo "✓ $check_tool have exist, skip download and unzip"
        return 0
    fi
    
    if [ -f "$tmp_file" ]; then
        echo "✓ Finding exist file: $tmp_file"
        echo "Using downloaded file to unzip..."
    else
        echo "Downloading $filename..."
        wget -q --show-progress "$url" -O "$tmp_file"
    fi
    
    if ! tar -tzf "$tmp_file" >/dev/null 2>&1; then
        echo "✗ File is bad, reloading..."
        rm -f "$tmp_file"
        wget -q --show-progress "$url" -O "$tmp_file"
    fi
    
    echo "Unzip to $target_dir..."
    sudo tar -xzf "$tmp_file" -C "$target_dir" --strip-components=1
    
    rm -f "$tmp_file"
    
    echo "✓ $filename has finished"
}

compile_and_install_qemu() {
    local qemu_version="10.1.2"
    local qemu_url="https://download.qemu.org/qemu-${qemu_version}.tar.xz"
    local qemu_filename="qemu-${qemu_version}.tar.xz"
    local tmp_file="/tmp/$qemu_filename"
    local build_dir="/tmp/qemu-${qemu_version}-build"
    
    echo "start installing QEMU ${qemu_version}..."
    
    if command -v qemu-system-x86_64 >/dev/null 2>&1 && \
       command -v qemu-system-aarch64 >/dev/null 2>&1 && \
       command -v qemu-system-riscv64 >/dev/null 2>&1; then
        echo "✓ QEMU has installed, skip"
        return 0
    fi
    
    if [ -f "$tmp_file" ]; then
        echo "✓ Find exist QEMU source file zip: $tmp_file"
    else
        echo "Downloading QEMU source code..."
        wget -q --show-progress "$qemu_url" -O "$tmp_file"
    fi
    
    if ! tar -tf "$tmp_file" >/dev/null 2>&1; then
        echo "✗ File is bad, reloading..."
        rm -f "$tmp_file"
        wget -q --show-progress "$qemu_url" -O "$tmp_file"
    fi
    
    sudo rm -rf "$build_dir"
    mkdir -p "$build_dir"
    
    echo "Unzipping QEMU source code..."
    tar -xf "$tmp_file" -C "/tmp"
    local src_dir="/tmp/qemu-${qemu_version}"
    
    cd "$src_dir"
    
    echo "Configuring QEMU..."
    ./configure \
        --target-list=x86_64-softmmu,aarch64-softmmu,riscv64-softmmu \
        --prefix=/usr/local
    
    echo "Compling QEMU(Which need a moment)..."
    make -j$(nproc)
    
    echo "Installing QEMU..."
    sudo make install
    
    echo "Cleaning tmp file..."
    cd /
    sudo rm -rf "$src_dir" "$build_dir" "$tmp_file"
    
    echo "✓ QEMU ${qemu_version} has installed"
}

install_qemu_dependencies() {
    echo "Installing QEMU dependencies..."
    
    with_tuna_mirror install -y \
        pkg-config \
        libglib2.0-dev \
        libpixman-1-dev \
        libslirp-dev \
        ninja-build \
        meson \
        flex \
        bison \
        libaio-dev \
        libcap-ng-dev \
        libattr1-dev \
        librbd-dev \
        libgnutls28-dev \
        libssh-dev \
        liburing-dev
}

echo "Starting configure environment..."

with_tuna_mirror update

with_tuna_mirror install -y \
    git \
    wget \
    python3-pip \
    curl \
    build-essential \
    cmake \
    tar \
    xz-utils \
    gcc-aarch64-linux-gnu \
    gcc-riscv64-linux-gnu \
    gcc-x86-64-linux-gnu \
    binutils-aarch64-linux-gnu \
    binutils-riscv64-linux-gnu \
    binutils-x86-64-linux-gnu \
    libc6-dev-arm64-cross \
    libc6-dev-riscv64-cross \
    cloc \
    clang-format

install_qemu_dependencies

echo "create /opt/musl-cross dir..."
sudo mkdir -p /opt/musl-cross

MUSL_COMPILERS=(
    "https://musl.cc/x86_64-linux-musl-cross.tgz"
    "https://musl.cc/aarch64-linux-musl-cross.tgz"
)

for compiler_url in "${MUSL_COMPILERS[@]}"; do
    download_and_extract_musl "$compiler_url"
done

echo "Chenge mod..."
sudo chmod -R 755 /opt/musl-cross

echo "Configure env virable..."
CURRENT_SHELL=$(basename "$SHELL")

case "$CURRENT_SHELL" in
    "bash") SHELL_RC="$HOME/.bashrc" ;;
    "zsh") SHELL_RC="$HOME/.zshrc" ;;
    "fish") SHELL_RC="$HOME/.config/fish/config.fish" ;;
    *) SHELL_RC="$HOME/.bashrc" ;;
esac

echo "Detected shell $CURRENT_SHELL, configure file: $SHELL_RC"

if grep -q "MUSL_CROSS_COMPILER" "$SHELL_RC"; then
    echo "Find exist musl cross complier, Updating..."
    sed -i '/MUSL_CROSS_COMPILER/d' "$SHELL_RC"
fi

if [ "$CURRENT_SHELL" = "fish" ]; then
    cat >> "$SHELL_RC" << 'EOF'

# MUSL Cross Compiler configuration
set -gx MUSL_CROSS_COMPILER /opt/musl-cross
set -gx PATH $MUSL_CROSS_COMPILER/bin $PATH
EOF
else
    cat >> "$SHELL_RC" << 'EOF'

# MUSL Cross Compiler configuration
export MUSL_CROSS_COMPILER=/opt/musl-cross
export PATH="$MUSL_CROSS_COMPILER/bin:$PATH"
EOF
fi

echo "Taking env variable effect immediately..."
export MUSL_CROSS_COMPILER=/opt/musl-cross
export PATH="/opt/musl-cross/bin:$PATH"

echo "extract installation..."
if [ -d "/opt/musl-cross/bin" ]; then
    echo "✓ musl cross complier has installed to /opt/musl-cross"
    
    TOOLS=("x86_64-linux-musl-gcc" "aarch64-linux-musl-gcc")
    for tool in "${TOOLS[@]}"; do
        if [ -f "/opt/musl-cross/bin/$tool" ]; then
            echo "✓ $tool is ready"
        else
            echo "✗ $tool not find"
        fi
    done
    
    echo ""
    echo "Finish installation!"
    echo "Env variable has been installed $SHELL_RC"
    echo "Reopen the terminal or using 'source $SHELL_RC' let the configuration work"
    echo ""
    echo "Usable cross complier:"
    ls /opt/musl-cross/bin/*-musl*-gcc 2>/dev/null | xargs -n1 basename
else
    echo "✗ the installation might have problem, please check /opt/musl-cross dir"
    exit 1
fi

compile_and_install_qemu

echo "Finish configure env virable!"