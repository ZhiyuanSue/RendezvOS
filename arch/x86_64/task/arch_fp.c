#include <arch/x86_64/thread_arch.h>
#include <arch/x86_64/sys_ctrl_def.h>
#include <arch/x86_64/sys_ctrl.h>
#include <arch/x86_64/cpuinfo.h>
#include <common/bit.h>
#include <common/types.h>
#include <modules/log/log.h>

/**
 * For XSAVE, first please learn about the CPUID.0xD.
 *
 * EDX : EAX returns which bits can be open in XCR0.
 * EBX returns when the XCR0 have set some bits, how much space we need.
 * ECX returns when all the XCR0 support bits are enabled, how much space we
 * need.
 *
 * And RFBM, it is used in xsave/xrstor to decide which part we want to
 * save/restore. But please remember, although we use the same EDX : EAX,
 * the meaning between cpuid's EDX : EAX with RFBM EDX : EAX is different.
 *
 * so we should first use cpuid to detect which bits are supported.
 * Then enable the XCR0, then another cpuid to read the EBX to get the space.
 * Then when save/restore, using the RFBM decide which part save/restore.
 *
 * For simplest case, the RFBM is set to the same with the XCR0 enabled bits.
 * We also do it this way: one static fp_cfg holds use_xsave + xcr0, and that
 * xcr0 bitset is reused as RFBM on every save/restore.
 *
 * The EDX : EAX is like this:
 * Bit 00：x87 state
 * Bit 01：SSE state
 * Bit 02：AVX state
 * Bits 05-07：AVX-512 states
 */

/*
 * X87|SSE only, it use FXSAVE and need 512B size space.
 * AVX (no AVX-512) use XSAVE, size from CPUID.0xD
 */
#define ARCH_FP_FXSAVE_SIZE 512

/* XCR0 / RFBM components we may enable. */
#define ARCH_FP_XCR0_ALLOWED (XCR0_X87 | XCR0_SSE | XCR0_AVX)
/* 3 AVX-512 relative bits are denied */
#define ARCH_FP_XCR0_DENIED (XCR0_OPMASK | XCR0_ZMM_HI256 | XCR0_HI16_ZMM)

struct arch_fp_cfg {
        bool use_xsave;
        bool have_detected;
        u64 xcr0; /* also RFBM when use_xsave */
};

/* Boot-time policy; unchanged afterwards. fp_cfg.xcr0 also used as RFBM. */
static struct arch_fp_cfg fp_cfg = {false, false, 0};

/* Defaults are FXSAVE; XSAVE path overwrites from CPUID.0xD EBX after XCR0. */
static size_t fp_payload_size = ARCH_FP_FXSAVE_SIZE;
static size_t fp_buf_size = ARCH_FP_FXSAVE_SIZE + ARCH_FP_ALIGN;

size_t arch_fp_payload_size(void)
{
        return fp_payload_size;
}

size_t arch_fp_state_size(void)
{
        return fp_buf_size;
}

void arch_fp_cpu_init(void)
{
        u32 eax, ebx, ecx, edx;
        u64 xcr, supported;
        bool first = !fp_cfg.have_detected;

        /* x86-64 long mode already implies SSE/SSE2/FXSR. It's ABI defined */
        set_cr0_bit(CR0_MP);
        set_cr0_bit(CR0_NE);
        set_cr4_bit(CR4_OSFXSR);
        set_cr4_bit(CR4_OSXMMEXCPT);
        set_mxcsr(MXCSR_DEFAULT);

        /* detect once → fp_cfg; APs skip to enable. Boot is BSP then APs. */
        if (!first)
                goto arch_fp_cpu_enable;
        if (cpu_info.feature_1 & X86_CPUID_FEATURE_ECX_XSAVE) {
                fp_cfg.use_xsave = true;
                fp_cfg.xcr0 = XCR0_X87 | XCR0_SSE;
                if ((cpu_info.feature_1 & X86_CPUID_FEATURE_ECX_AVX)
                    && cpu_info.MaxBasicCPUID >= X86_CPUID_XSAVE) {
                        cpuid(X86_CPUID_XSAVE, 0, &eax, &ebx, &ecx, &edx);
                        supported = ((u64)edx << 32) | eax;
                        if (supported & XCR0_AVX)
                                fp_cfg.xcr0 |= XCR0_AVX;
                }
                fp_cfg.xcr0 &= ARCH_FP_XCR0_ALLOWED;
        }

arch_fp_cpu_enable:
        /* every CPU: XCR0 is per-core. Size only on first. */
        if (fp_cfg.use_xsave) {
                set_cr4_bit(CR4_OSXSAVE);
                xcr = get_xcr(0);
                xcr = clear_mask_u64(
                        xcr, ARCH_FP_XCR0_ALLOWED | ARCH_FP_XCR0_DENIED);
                xcr = set_mask_u64(xcr, fp_cfg.xcr0);
                set_xcr(0, xcr);
                if (first && cpu_info.MaxBasicCPUID >= X86_CPUID_XSAVE) {
                        cpuid(X86_CPUID_XSAVE, 0, &eax, &ebx, &ecx, &edx);
                        if (ebx >= ARCH_FP_FXSAVE_SIZE) {
                                fp_payload_size = ebx;
                                fp_buf_size = fp_payload_size + ARCH_FP_ALIGN;
                        }
                }
        }

        if (first) {
                fp_cfg.have_detected = true;
                pr_info("[arch_fp]: %s xcr0/rfbm=0x%llx payload=%llu buf=%llu\n",
                        fp_cfg.use_xsave ? "XSAVE" : "FXSAVE",
                        fp_cfg.xcr0,
                        (u64)fp_payload_size,
                        (u64)fp_buf_size);
        }
}

void arch_fp_save_to(void* buf)
{
        if (!buf)
                return;
        if (fp_cfg.use_xsave) {
                u32 lo = (u32)fp_cfg.xcr0;
                u32 hi = (u32)(fp_cfg.xcr0 >> 32);
                __asm__ __volatile__("xsave64 (%0)"
                                     :
                                     : "r"(buf), "a"(lo), "d"(hi)
                                     : "memory");
        } else {
                __asm__ __volatile__("fxsave64 (%0)" : : "r"(buf) : "memory");
        }
}

void arch_fp_load_from(const void* buf)
{
        if (!buf)
                return;
        if (fp_cfg.use_xsave) {
                u32 lo = (u32)fp_cfg.xcr0;
                u32 hi = (u32)(fp_cfg.xcr0 >> 32);
                __asm__ __volatile__("xrstor64 (%0)"
                                     :
                                     : "r"(buf), "a"(lo), "d"(hi)
                                     : "memory");
        } else {
                __asm__ __volatile__("fxrstor64 (%0)" : : "r"(buf) : "memory");
        }
}

void arch_fp_init_area(u8* area)
{
        if (!area)
                return;
        /* Legacy header layout shared by FXSAVE and XSAVE. */
        *(u16*)(area + ARCH_X86_FXSAVE_FCW_OFF) = FCW_DEFAULT;
        *(u32*)(area + ARCH_X86_FXSAVE_MXCSR_OFF) = MXCSR_DEFAULT;
}
