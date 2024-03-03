#ifndef _RISCV64_SBI_H_
#define _RISCV64_SBI_H_

#include <shampoos/types.h>
/*
 *  See https://github.com/riscv-non-isa/riscv-sbi-doc/blob/master/riscv-sbi.adoc
 *  I rewrite it all according to this document
 */
struct sbiret {
	long error;
	long value;
};

enum SBI_ERROR_TYPES{
	SBI_SUCCESS					=   0,
	SBI_ERR_FAILED				=   -1,
	SBI_ERR_NOT_SUPPORTED		=   -2,
	SBI_ERR_INVALID_PARAM		=   -3,
	SBI_ERR_DENIED				=   -4,
	SBI_ERR_INVALID_ADDRESS		=   -5,
	SBI_ERR_ALREADY_AVAILABLE	=   -6,
	SBI_ERR_ALREADY_STARTED		=   -7,
	SBI_ERR_ALREADY_STOPPED		=   -8,
	SBI_ERR_NO_SHMEM			=   -9,
};
enum SBI_EXT_ID{
#ifdef _SBI_LEGANCY_
	/*
	* For the v0.1 version SBI, some funcitons are defined with different ext id  
	*/
	SBI_LEGANCY_EXT_SET_TIMER				=   0x00,
	SBI_LEGANCY_EXT_CONSOLE_PUTCHAR			=   0x01,
	SBI_LEGANCY_EXT_CONSOLE_GETCHAR			=   0x02,
	SBI_LEGANCY_EXT_CLEAR_IPI				=   0x03,
	SBI_LEGANCY_EXT_SEND_IPI				=   0x04,
	SBI_LEGANCY_EXT_REMOTE_FENCE_I			=   0x05,
	SBI_LEGANCY_EXT_REMOTE_SFENCE_VMA		=   0x06,
	SBI_LEGANCY_EXT_REMOTE_SFENCE_VMA_ASID	=   0x07,
	SBI_LEGANCY_EXT_SHUTDOWN				=   0x08,
#endif
    SBI_BASE_EXT    =   0x10,
    SBI_CPPC_EXT    =   0x43505043,
    SBI_DBCN_EXT    =   0x4442434E,
    SBI_HSM_EXT     =   0x48534D,
    SBI_IPI_EXT     =   0x735049,
    SBI_NACL_EXT    =   0x4E41434C,
    SBI_PMU_EXT     =   0x504D55,
    SBI_RFNC_EXT    =   0x52464E43,
    SBI_STA_EXT     =   0x535441,
    SBI_SRST_EXT    =   0x53525354,
    SBI_SUSP_EXT    =   0x53555350,
    SBI_TIME_EXT    =   0x54494D45,


    SBI_EXT_EXPERIMENTAL_START  =   0x08000000,
	SBI_EXT_EXPERIMENTAL_END    =   0x08FFFFFF,
    
    SBI_EXT_VENDOR_START        =   0x09000000,
    SBI_EXT_VENDOR_END          =   0x09FFFFFF,

    SBI_EXT_FIRMWARE_START  =   0x0A000000,
    SBI_EXT_FIRMWARE_END    =   0x0AFFFFFF,
};

/*Base Externsion SBI Version 0.2, EID 0x10*/
enum SBI_BASE_EXT_FID{
    SBI_EXT_BASE_GET_SPEC_VERSION   =   0,
	SBI_EXT_BASE_GET_IMPL_ID        =   1,
	SBI_EXT_BASE_GET_IMPL_VERSION   =   2,
	SBI_EXT_BASE_PROBE_EXT          =   3,
	SBI_EXT_BASE_GET_MVENDORID      =   4,
	SBI_EXT_BASE_GET_MARCHID        =   5,
	SBI_EXT_BASE_GET_MIMPID         =   6,
};
/*FID #0*/
struct sbiret sbi_get_spec_version(void);
/*FID #1*/
struct sbiret sbi_get_impl_id(void);
/*FID #2*/
struct sbiret sbi_get_impl_version(void);
/*FID #3*/
struct sbiret sbi_probe_extension(long extension_id);
/*FID #4*/
struct sbiret sbi_get_mvendorid(void);
/*FID #5*/
struct sbiret sbi_get_marchid(void);
/*FID #6*/
struct sbiret sbi_get_mimpid(void);


/*
 * CPPC Externsion SBI Version 2.0, EID 0x43505043
 * As mentioned in ACPI, I think it might also used in ACPI include files
 */
enum SBI_CPPC_EXT_FID{
    SBI_EXT_CPPC_PROBE  =   0,
    SBI_EXT_CPPC_READ   =   1,
    SBI_EXT_CPPC_READ_HI=   2,
    SBI_EXT_CPPC_WRITE  =   3,
};
/*FID #0*/
struct sbiret sbi_cppc_probe(u_int32_t cppc_reg_id);
/*FID #1*/
struct sbiret sbi_cppc_read(u_int32_t cppc_reg_id);
/*FID #2*/
struct sbiret sbi_cppc_read_hi(u_int32_t cppc_reg_id);
/*FID #3*/
struct sbiret sbi_cppc_write(u_int32_t cppc_reg_id, u_int64_t val);

/*
 * DBCN Externsion SBI Version 2.0, EID 0x4442434E
 */
enum SBI_DBCN_EXT_FID{
    SBI_EXT_DBCN_CONSOLE_WRITE      =   0,
    SBI_EXT_DBCN_CONSOLE_READ       =   1,
    SBI_EXT_DBCN_CONSOLE_WRITE_BYTE =   2,
};
/*FID #0*/
struct sbiret sbi_debug_console_write(unsigned long num_bytes,
                                      unsigned long base_addr_lo,
                                      unsigned long base_addr_hi);
/*FID #1*/
struct sbiret sbi_debug_console_read(unsigned long num_bytes,
                                      unsigned long base_addr_lo,
                                      unsigned long base_addr_hi);
/*FID #2*/
struct sbiret sbi_debug_console_write_byte(u_int8_t byte);

/*
 * HSM Externsion SBI Version 0.2, EID 0x48534D
 */
enum SBI_HART_STATES
{
    SBI_HART_STARTED            =   0,
    SBI_HART_STOPPED            =   1,
    SBI_HART_START_PENDING      =   2,
    SBI_HART_STOP_PENDING       =   3,
    SBI_HART_SUSPENDED          =   4,
    SBI_HART_SUSPEND_PENDING    =   5,
    SBI_HART_RESUME_PENDING     =   6,
};
enum SBI_HART_EXT_FID{
    SBI_EXT_HSM_START   =   0,
	SBI_EXT_HSM_STOP    =   1,
	SBI_EXT_HSM_STATUS  =   2,
	SBI_EXT_HSM_SUSPEND =   3,
};
/*FID #0*/
struct sbiret sbi_hart_start(unsigned long hartid,
                             unsigned long start_addr,
                             unsigned long opaque);
/*FID #1*/
struct sbiret sbi_hart_stop(void);
/*FID #2*/
struct sbiret sbi_hart_get_status(unsigned long hartid);
/*FID #3*/
struct sbiret sbi_hart_suspend(u_int32_t suspend_type,
                               unsigned long resume_addr,
                               unsigned long opaque);

/*
 * IPI Externsion SBI Version 0.2, EID 0x735049
 */
/*FID #0*/
#ifndef _SBI_LEGANCY_
enum SBI_IPI_EXT_FID{
    SBI_EXT_IPI_SEND    =   0,
};
struct sbiret sbi_send_ipi(unsigned long hart_mask,
                           unsigned long hart_mask_base);
#endif
/*
 * NACL Externsion SBI Version 2.0, EID 0x4E41434C
 * There might more definations for this externsion,but it's a future work
 */
enum SBI_NACL_EXT_FID{
    SBI_EXT_NACL_PROBE_FEATURE  =   0,
    SBI_EXT_NACL_SET_SHMEM      =   1,
    SBI_EXT_NACL_SYNC_CSR       =   2,
    SBI_EXT_NACL_SYNC_HFENCE    =   3,
    SBI_EXT_NACL_SYNC_SRET      =   4,
};
/*FID #0*/
struct sbiret sbi_nacl_probe_feature(u_int32_t feature_id);
/*FID #1*/
struct sbiret sbi_nacl_set_shmem(unsigned long shmem_phys_lo,
                                 unsigned long shmem_phys_hi,
                                 unsigned long flags);
/*FID #2*/
struct sbiret sbi_nacl_sync_csr(unsigned long csr_num);
/*FID #3*/
struct sbiret sbi_nacl_sync_hfence(unsigned long entry_index);
/*FID #4*/
struct sbiret sbi_nacl_sync_sret(void);

/*
 * Legacy Externsion SBI Version 0.1
 */
#ifdef _SBI_LEGANCY_
/*EID #0x00*/
//long sbi_set_timer(u_int64_t stime_value);
/*EID #0x01*/
long sbi_console_putchar(int ch);
/*EID #0x02*/
long sbi_console_getchar(void);
/*EID #0x03*/
long sbi_clear_ipi(void);
/*EID #0x04*/
//long sbi_send_ipi(const unsigned long *hart_mask);
/*EID #0x05*/
//long sbi_remote_fence_i(const unsigned long *hart_mask);
/*EID #0x06*/
//long sbi_remote_sfence_vma(const unsigned long *hart_mask,
//                           unsigned long start,
//                           unsigned long size);
/*EID #0x07*/
//long sbi_remote_sfence_vma_asid(const unsigned long *hart_mask,
//                                unsigned long start,
//                                unsigned long size,
//                                unsigned long asid);
/*EID #0x08*/
void sbi_shutdown(void);
#endif
/*
 * PMU Externsion SBI Version 0.3, EID 0x504D55
 */
enum SBI_HW_EVENT_TYPE{
    SBI_HW_EVENT_GENERAL    =   0,
    SBI_HW_EVENT_CACHE      =   1,
    SBI_HW_EVENT_RAW        =   2,
    SBI_HW_EVENT_FIRMWARE   =   15,
};
enum SBI_HW_GENERAL_EVENT{
    SBI_PMU_HW_NO_EVENT,
    SBI_PMU_HW_CPU_CYCLES,
    SBI_PMU_HW_INSTRUCTIONS,
    SBI_PMU_HW_CACHE_REFERENCES,
    SBI_PMU_HW_CACHE_MISSES,
    SBI_PMU_HW_BRANCH_INSTRUCTIONS,
    SBI_PMU_HW_BRANCH_MISSES,
    SBI_PMU_HW_BUS_CYCLES,
    SBI_PMU_HW_STALLED_CYCLES_FRONTEND,
    SBI_PMU_HW_STALLED_CYCLES_BACKEND,
    SBI_PMU_HW_REF_CPU_CYCLES,
};
enum SBI_HW_CACHE_EVENT{
    SBI_PMU_HW_CACHE_L1D,
    SBI_PMU_HW_CACHE_L1I,
    SBI_PMU_HW_CACHE_LL,
    SBI_PMU_HW_CACHE_DTLB,
    SBI_PMU_HW_CACHE_ITLB,
    SBI_PMU_HW_CACHE_BPU,
    SBI_PMU_HW_CACHE_NODE,
};
enum SBI_HW_CACHE_OP_ID{
    SBI_PMU_HW_CACHE_OP_READ,
    SBI_PMU_HW_CACHE_OP_WRITE,
    SBI_PMU_HW_CACHE_OP_PREFETCH,
};
enum SBI_HW_CACHE_OP_RESULT_ID{
    SBI_PMU_HW_CACHE_RESULT_ACCESS,
    SBI_PMU_HW_CACHE_RESULT_MISS,
};
enum SBI_HW_RAW_EVENT{
    SBI_PMU_FW_MISALIGNED_LOAD,
    SBI_PMU_FW_MISALIGNED_STORE,
    SBI_PMU_FW_ACCESS_LOAD,
    SBI_PMU_FW_ACCESS_STORE,
    SBI_PMU_FW_ILLEGAL_INSN,
    SBI_PMU_FW_SET_TIMER,
    SBI_PMU_FW_IPI_SENT,
    SBI_PMU_FW_IPI_RECEIVED,
    SBI_PMU_FW_FENCE_I_SENT,
    SBI_PMU_FW_FENCE_I_RECEIVED,
    SBI_PMU_FW_SFENCE_VMA_SENT,
    SBI_PMU_FW_SFENCE_VMA_RECEIVED,
    SBI_PMU_FW_SFENCE_VMA_ASID_SENT,
    SBI_PMU_FW_SFENCE_VMA_ASID_RECEIVED,
    SBI_PMU_FW_HFENCE_GVMA_SENT,
    SBI_PMU_FW_HFENCE_GVMA_RECEIVED,
    SBI_PMU_FW_HFENCE_GVMA_VMID_SENT,
    SBI_PMU_FW_HFENCE_GVMA_VMID_RECEIVED,
    SBI_PMU_FW_HFENCE_VVMA_SENT,
    SBI_PMU_FW_HFENCE_VVMA_RECEIVED,
    SBI_PMU_FW_HFENCE_VVMA_ASID_SENT,
    SBI_PMU_FW_HFENCE_VVMA_ASID_RECEIVED,
    SBI_PMU_FW_PLATFORM=65535,
};
enum SBI_PMU_EXT_FID{
    SBI_EXT_PMU_NUM_COUNTER,
    SBI_EXT_PMU_COUNTER_GET_INFO,
    SBI_EXT_PMU_COUNTER_CONFIG_MATCHING,
    SBI_EXT_PMU_COUNTER_START,
    SBI_EXT_PMU_COUNTER_STOP,
    SBI_EXT_PMU_COUNTER_FW_READ,
    SBI_EXT_PMU_COUNTER_FW_READ_HI,
    SBI_EXT_PMU_SNAPSHOT_SET_SHMEM,
};
/*FID #0*/
struct sbiret sbi_pmu_num_counters();
/*FID #1*/
struct sbiret sbi_pmu_counter_get_info(unsigned long counter_idx);
/*FID #2*/
struct sbiret sbi_pmu_counter_config_matching(unsigned long counter_idx_base,
                                              unsigned long counter_idx_mask,
                                              unsigned long config_flags,
                                              unsigned long event_idx,
                                              u_int64_t event_data);
/*FID #3*/
struct sbiret sbi_pmu_counter_start(unsigned long counter_idx_base,
                                    unsigned long counter_idx_mask,
                                    unsigned long start_flags,
                                    u_int64_t initial_value);
/*FID #4*/
struct sbiret sbi_pmu_counter_stop(unsigned long counter_idx_base,
                                   unsigned long counter_idx_mask,
                                   unsigned long stop_flags);
/*FID #5*/
struct sbiret sbi_pmu_counter_fw_read(unsigned long counter_idx);
/*FID #6, used in Version 2.0*/
struct sbiret sbi_pmu_counter_fw_read_hi(unsigned long counter_idx);
/*FID #7, used in Version 2.0*/
struct sbiret sbi_pmu_snapshot_set_shmem(unsigned long shmem_phys_lo,
                                         unsigned long shmem_phys_hi,
                                         unsigned long flags);
/*
 * RFNC Externsion SBI Version 0.2, EID 0x52464E43
 */
enum SBI_RFNC_EXT_FID{
    SBI_EXT_RFNC_REMOTE_FENCE_I,
    SBI_EXT_RFNC_REMOTE_SFENCE_VMA,
    SBI_EXT_RFNC_REMOTE_SFENCE_VMA_ASID,
    SBI_EXT_RFNC_REMOTE_HFENCE_GVMA_VMID,
    SBI_EXT_RFNC_REMOTE_HFENCE_GVMA,
    SBI_EXT_RFNC_REMOTE_HFENCE_VVMA_ASID,
    SBI_EXT_RFNC_REMOTE_HFENCE_VVMA,
};
/*FID #0*/
struct sbiret sbi_remote_fence_i(unsigned long hart_mask,
                                 unsigned long hart_mask_base);
/*FID #1*/
struct sbiret sbi_remote_sfence_vma(unsigned long hart_mask,
                                    unsigned long hart_mask_base,
                                    unsigned long start_addr,
                                    unsigned long size);
/*FID #2*/
struct sbiret sbi_remote_sfence_vma_asid(unsigned long hart_mask,
                                         unsigned long hart_mask_base,
                                         unsigned long start_addr,
                                         unsigned long size,
                                         unsigned long asid);
/*FID #3*/
struct sbiret sbi_remote_hfence_gvma_vmid(unsigned long hart_mask,
                                          unsigned long hart_mask_base,
                                          unsigned long start_addr,
                                          unsigned long size,
                                          unsigned long vmid);
/*FID #4*/
struct sbiret sbi_remote_hfence_gvma(unsigned long hart_mask,
                                     unsigned long hart_mask_base,
                                     unsigned long start_addr,
                                     unsigned long size);
/*FID #5*/
struct sbiret sbi_remote_hfence_vvma_asid(unsigned long hart_mask,
                                          unsigned long hart_mask_base,
                                          unsigned long start_addr,
                                          unsigned long size,
                                          unsigned long asid);
/*FID #6*/
struct sbiret sbi_remote_hfence_vvma(unsigned long hart_mask,
                                     unsigned long hart_mask_base,
                                     unsigned long start_addr,
                                     unsigned long size);
/*
 * STA Externsion SBI Version 2.0, EID 0x535441
 */
enum SBI_STA_EXT_FID{
    SBI_EXT_STA_SET_SHMEM,
};
/*FID #0*/
struct sbiret sbi_steal_time_set_shmem(unsigned long shmem_phys_lo,
                                       unsigned long shmem_phys_hi,
                                       unsigned long flags);
/*
 * SRST Externsion SBI Version 0.3, EID 0x53525354
 */
enum SBI_SRST_EXT_FID{
    SBI_EXT_SRST_RESET,
};
enum SBI_SRST_EXT_RESET_TYPE{
    SBI_RESET_SHUTDOWN,
    SBI_RESET_COLD_REBOOT,
    SBI_RESET_WARM_REBOOT,
};
enum SBI_SRST_EXT_RESET_REASEON{
    SBI_RESET_NO_REASON,
    SBI_RESET_SYSTEM_FAILURE,
};
/*FID #0*/
struct sbiret sbi_system_reset(u_int32_t reset_type, u_int32_t reset_reason);


/*
 * SUSP Externsion SBI Version 2.0, EID 0x53555350
 */

enum SBI_SUSP_EXT_TYPE{
    SUSPEND_TO_RAM,
};
enum SBI_SUSP_EXT_FID{
    SBI_EXT_SUSP_SUSPEND,
};
/*FID #0*/
struct sbiret sbi_system_suspend(u_int32_t sleep_type,
                                 unsigned long resume_addr,
                                 unsigned long opaque);

/*
 * TIME Externsion SBI Version 0.2, EID 0x54494D45
 */
enum SBI_TIME_EXT_FID{
    SBI_EXT_TIME_SET,
};
struct sbiret sbi_set_timer(u_int64_t stime_value);


void sbi_init(void);
#endif