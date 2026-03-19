# RendezvOS

The project was originally named in honor of my brother [YuJian](https://www.zhihu.com/people/xi-wo-wang-yi-15-46/posts), but later changed its name to RendezvOS, which was taken from the word rendezvous, used to express a romantic and expectant encounter.

## About YuJian

YuJian is my senior brother, a 2020 master's student in the Department of Computer Science and Technology at Tsinghua University. He has an incredible talent for mathematics and computer science, as well as an extremely hard and diligent attitude to study and work, which is my role model in life

RendezvOS is the standalone kernel tree. In another word, it's a core of a kernel. It contains the boot flow, memory management, task/thread management, IPC, and the architecture-specific code needed to bring the kernel up.

The repository root now acts as the integration layer for the Linux-compatible modules and the user-test payload. If you are working on the full system, prefer the root-level `Makefile`. If you want to exercise the kernel itself, use the commands in this document.

## Standalone RendezvOS workflow

RendezvOS can still be built and run by itself.

```bash
cd RendezvOS
make ARCH=x86_64 config
make ARCH=x86_64 run
```

This mode keeps its own `RendezvOS/build/` output directory and is useful for kernel-only experiments or core-side tests.

If you change the architecture config under `RendezvOS/script/config/config_##arch##.json`, rerun:

```bash
make ARCH=x86_64 config
```


That module should provide the syscall entry, IRQ registration, and the kernel init entry point used after `main`.

## Build environment

If you need the toolchain and emulator setup, run:

```bash
./build_env.sh
```

The script installs the required packages and toolchains for the kernel build.

## Debug and runtime flags

Use the usual `make run` flags in standalone `RendezvOS/` mode:

```bash
make ARCH=x86_64 run LOG=true
make ARCH=x86_64 run DBG=true
make ARCH=x86_64 run DUMP=true
make ARCH=x86_64 run SMP=1
make ARCH=x86_64 run MEM_SIZE=1G
```

Notes:

- `LOG=true` enables extra QEMU logging.
- `DBG=true` enables GDB-friendly boot parameters.
- `DUMP=true` enables disassembly output, then `make dump` writes it.
- `SMP` sets the CPU count.
- `MEM_SIZE` sets the guest memory size.

## Architecture configs

The architecture config files are read by `configure.py` and define the kernel build settings for each supported architecture.

Supported standalone RendezvOS configs currently include:

- `x86_64`
- `aarch64`
- `riscv64`
- `loongarch`

## Acknowledgements

Some code was reused from open-source projects:

- U-Boot: device-tree related code and definitions
- Linux: errno and other common kernel conventions
