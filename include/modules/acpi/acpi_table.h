/*
 *	It's a realization of ACPI Specification for version 6.5
 *	And I rewrite it , which is not a copy of linux acpi files
 */
#ifndef _ACPI_TABLE_H_
#define _ACPI_TABLE_H_
#include <common/types.h>
#include <modules/acpi/acpi_defs.h>

/*
 * I have to tell this line of pack is necessary.
 * If you understand the layout of a struct, you must know why.
 * If you don't, just try to learn it.
 * Otherwise, there might left some hole in the members of a struct.
 * And because of the unknown alignment of different compliers and platform you
 * use, the size of the hole is unknown So you have to use the pack(1) to
 * eliminate the possible hole and keep the members close.
 */
#pragma pack(1)
/*
 * For only acpi version 1.0 RSDP table
 */
#define ACPI_TABLE_RSDP(version) (struct acpi_table_rsdp_##version)

#define ACPI_TABLE_RSDP_1                                                      \
        char signature[8];                                                     \
        /* “RSD PTR ” (Notice that this signature must contain             \
                    a trailing blank character.) */                            \
        u8 checksum;                                                           \
        /* This is the checksum of the fields defined in the ACPI 1.0          \
                    specification. This includes only the first 20 bytes of    \
                    this table, bytes 0 to 19,                                 \
                    including the checksum field.                              \
                    These bytes must sum to zero. */                           \
        char oemid[6];                                                         \
        /* An OEM-supplied string that identifies the OEM. */                  \
        u8 revision;                                                           \
        /* The revision of this structure.                                     \
            Larger revision numbers are backward compatible to lower  revision \
           numbers. The ACPI version 1.0 revision number of	this table is      \
           zero. The ACPI  version 1.0 RSDP  Structure only includes the first \
           20 bytes of this table, bytes 0 to 19. It does not include the      \
           Length field and beyond.                                            \
        The current  value for		this field is 2. */                            \
        u32 rsdt_address; /* 32 bit physical address of the RSDT. */
struct acpi_table_rsdp {
        ACPI_TABLE_RSDP_1
};
/*
 * For acpi version 2.0 RSDP table
 */
struct acpi_table_rsdp_2 {
        ACPI_TABLE_RSDP_1
        u32 length; /* The length of the table, in bytes, including the header,
                        arting from offset 0. This field is used to record
                                                the size of the entire table.
                                                This field is not available in
                       the ACPI version 1.0 RSDP Structure. */
        u64 xsdt_address; /* 64 bit physical address of the XSDT. */
        u8 extended_checksum; /* This is a checksum of the entire table,
                                 including both checksum fields. */
        u8 reserved[3]; /* Reserved field */
};

/*
 *	Generic address structure, which is used to express register addresses
 */
struct acpi_gas {
        u8 address_space_id;
        /* The address space where the data structure or
                                                register exists. */
#define ACPI_GAS_ID_System_Memory_Space 0x00 /* System Memory space */
#define ACPI_GAS_ID_System_IO_Space     0x01 /* System I/O space */
#define ACPI_GAS_ID_PCI_conf_Space      0x02 /* PCI Configuration space */
#define ACPI_GAS_ID_Embedded_Ctrler     0x03 /* Embedded Controller */
#define ACPI_GAS_ID_SMBus               0x04 /* SMBus */
#define ACPI_GAS_ID_System_CMOS         0x05 /* SystemCMOS */
#define ACPI_GAS_ID_Pci_Bar_Target      0x06 /* PciBarTarget */
#define ACPI_GAS_ID_IPMI                0x07 /* IPMI */
#define ACPI_GAS_ID_General_Purposel_IO 0x08 /* General PurposeIO */
#define ACPI_GAS_ID_Generic_Serial_Bus  0x09 /* GenericSerialBus */
#define ACPI_GAS_ID_Platform_Comm_Chan  0x0A
        /* Platform Communications Channel (PCC) */
#define ACPI_GAS_ID_Platform_Rt_Mech             \
        0x0B /* Platform Runtime Mechanism (PRM) \
              */
        /* 0x0C to 0x7E Reserved */
#define ACPI_GAS_ID_Functional_Fixed_HW 0x7F /* Functional Fixed Hardware */
        /* 0x80 to 0xFF OEM Defined */
        u8 register_bit_width; /* The size in bits of the given register.
                                   When addressing a data structure,
                                   this field must be zero. */
        u8 register_bit_offset; /* The bit offset of the given register at the
                                   given address. When addressing a data
                                   structure, this field must be zero. */
        u8 access_size; /* Specifies access size. */
#define ACPI_GAS_ACCESS_SIZE_Undefined 0
#define ACPI_GAS_ACCESS_SIZE_Byte      1
#define ACPI_GAS_ACCESS_SIZE_Word      2
#define ACPI_GAS_ACCESS_SIZE_Dword     3
#define ACPI_GAS_ACCESS_SIZE_Qword     4
        u64 address; /* The 64-bit address of the data structure or register in
                        the given address space (relative to the processor). */
};
/*
 *	For PCI Bar Target, which address_space_id is
 *ACPI_GAS_ID_Pci_Bar_Target(0x06),the address must be the following: [63:56]
 *PCI Segment [55:48] PCI Bus [47:43] PCI Device [42:40] PCI Function [39:37]
 *BAR Index [36:0 ]	Offset from BAR in DWORDs
 */
/*
 *	ACPI table header
 */
#define ACPI_TABLE_HEAD                                                      \
        char signature[4];                                                   \
        /* The ASCII string representation of the table                      \
                identifier. */                                               \
        u32 length; /* The length of the table, in bytes, including the      \
                       header, starting from offset 0.    This field is used \
                       to record the size of the entire table. */            \
        u8 revision;                                                         \
        /* The revision of the structure corresponding to the                \
                signature field for this table.   Larger revision numbers    \
                are backward compatible to lower revision numbers with the   \
                same signature.*/                                            \
        u8 checksum; /* The entire table, including the checksum field,      \
                        must add to zero to be considered valid. */          \
        char OEMID[6];                                                       \
        /* An OEM-supplied string that identifies the OEM. */                \
        char OEM_table_ID[8];                                                \
        /* An OEM-supplied string that the OEM uses to                       \
                identify the particular data table. This field is            \
                particularly useful when defining a definition               \
                block to distinguish definition block functions.             \
                The OEM assigns each dissimilar table a new OEM              \
                Table ID. */                                                 \
        u32 OEM_revision;                                                    \
        /* An OEM-supplied revision number. Larger numbers are               \
                assumed to be newer revisions. */                            \
        u32 Creator_ID;                                                      \
        /* Vendor ID of utility that created the table.                      \
                For tables containing Definition Blocks,                     \
                    this is the                                              \
                ID for the ASL Compiler. */                                  \
        u32 Creator_revision
/*Revision of utility that created the table.                        \
        For tables containing Definition Blocks, this is             \
        the revision for the ASL Compiler.*/
/*Root System Description Tables*/
struct acpi_table_head {
        ACPI_TABLE_HEAD;
};
#define ACPI_HEAD_SIZE sizeof(acpi_table_head)
struct acpi_table_rsdt {
        ACPI_TABLE_HEAD;
        u32 *entry;
};
#define ACPI_RSDT_ENTRY_SIZE (4)

/*Extended System Description Table XSDT*/
struct acpi_table_xsdt {
        ACPI_TABLE_HEAD;
        u64 *entry;
};
#define ACPI_XSDT_ENTRY_SIZE (8)

#endif
