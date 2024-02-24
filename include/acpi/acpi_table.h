/*
 *	It's a realization of ACPI Specification for version 6.5
 *	And I rewrite it , which is not a copy of linux acpi files
 */
#ifndef _ACPI_TABLE_H_
#define _ACPI_TABLE_H_
#include <shampoos/types.h>
#include <acpi/acpi_defs.h>

/*
 * I have to tell this line of pack is necessary.
 * If you understand the layout of a struct, you must know why. 
 * If you don't, just try to learn it.
 * Otherwise, there might left some hole in the members of a struct.
 * And because of the unknown alignment of different compliers and platform you use, the size of the hole is unknown
 * So you have to use the pack(1) to eliminate the possible hole and keep the members close. 
 */
#pragma pack(1)
/*
 * For only acpi version 1.0 RSDP table 
 */
#define ACPI_TABLE_RSDP(version) (struct acpi_table_rsdp_##version)
struct acpi_table_rsdp_1{
	char	signature[8];	/* “RSD PTR ” (Notice that this signature must contain a trailing blank character.) */
	u8		checksum;		/* This is the checksum of the fields defined in the ACPI 1.0 specification. 
								This includes only the first 20 bytes of this table, bytes 0 to 19, 
								including the checksum field. 
								These bytes must sum to zero. */
	char	oemid[6];		/* An OEM-supplied string that identifies the OEM. */
	u8		revision;		/* The revision of this structure. 
								Larger revision numbers are backward compatible to lower revision numbers. 
								The ACPI version 1.0 revision number of this table is zero. 
								The ACPI version 1.0 RSDP Structure only includes the first 20 bytes of this table, bytes 0 to 19. 
								It does not include the Length field and beyond. The current value for this field is 2. */
	u32		rsdt_address;	/* 32 bit physical address of the RSDT. */
};
/*
 * For acpi version 2.0 RSDP table 
 */
struct acpi_table_rsdp_2{
	struct	acpi_table_rsdp_1;	
	u32		length;				/* The length of the table, in bytes, including the header, starting from offset 0. 
									This field is used to record the size of the entire table. 
									This field is not available in the ACPI version 1.0 RSDP Structure. */
	u64		xsdt_address;		/* 64 bit physical address of the XSDT. */
	u8		extended_checksum;	/* This is a checksum of the entire table, including both checksum fields. */
	u8		reserved[3];		/* Reserved field */
};

/*
 *	Generic address structure, which is used to express register addresses
 */
struct acpi_gas{
	u8		address_space_id;			/* The address space where the data structure or register exists. */
#define ACPI_GAS_ID_System_Memory_Space	0x00	/* System Memory space */
#define ACPI_GAS_ID_System_IO_Space		0x01	/* System I/O space */
#define ACPI_GAS_ID_PCI_conf_Space		0x02	/* PCI Configuration space */
#define ACPI_GAS_ID_Embedded_Ctrler		0x03	/* Embedded Controller */
#define ACPI_GAS_ID_SMBus				0x04	/* SMBus */
#define ACPI_GAS_ID_System_CMOS			0x05	/* SystemCMOS */
#define ACPI_GAS_ID_Pci_Bar_Target		0x06	/* PciBarTarget */
#define ACPI_GAS_ID_IPMI				0x07	/* IPMI */
#define ACPI_GAS_ID_General_Purposel_IO	0x08	/* General PurposeIO */
#define ACPI_GAS_ID_Generic_Serial_Bus	0x09	/* GenericSerialBus */
#define ACPI_GAS_ID_Platform_Comm_Chan	0x0A	/* Platform Communications Channel (PCC) */
#define ACPI_GAS_ID_Platform_Rt_Mech	0x0B	/* Platform Runtime Mechanism (PRM) */
	/* 0x0C to 0x7E Reserved */
#define ACPI_GAS_ID_Functional_Fixed_HW	0x7F	/* Functional Fixed Hardware */
	/* 0x80 to 0xFF OEM Defined */
	u8		register_bit_width;			/* The size in bits of the given register. 
											When addressing a data structure, this field must be zero. */
	u8		register_bit_offset;		/* The bit offset of the given register at the given address. 
											When addressing a data structure, this field must be zero. */
	u8		access_size;				/* Specifies access size. */
#define ACPI_GAS_ACCESS_SIZE_Undefined	0
#define ACPI_GAS_ACCESS_SIZE_Byte		1
#define ACPI_GAS_ACCESS_SIZE_Word		2
#define ACPI_GAS_ACCESS_SIZE_Dword		3
#define ACPI_GAS_ACCESS_SIZE_Qword		4
	u64		address;					/* The 64-bit address of the data structure or register in the given address space (relative to the processor). */
};
/*
 *	For PCI Bar Target, which address_space_id is ACPI_GAS_ID_Pci_Bar_Target(0x06),the address must be the following:
 *	[63:56] PCI Segment
 *	[55:48] PCI Bus
 *	[47:43] PCI Device
 *	[42:40] PCI Function
 *	[39:37] BAR Index
 *	[36:0 ]	Offset from BAR in DWORDs
 */
/*
 *	ACPI table header
 */
struct acpi_table_head{
	char 	signature[4];		/* The ASCII string representation of the table identifier. */
	u32		length;				/* The length of the table, in bytes, including the header, starting from offset 0. 
									This field is used to record the size of the entire table. */
	u8		revision;			/* The revision of the structure corresponding to the signature field for this table. 
									Larger revision numbers are backward compatible to lower revision numbers with the same signature.*/
	u8		checksum;			/* The entire table, including the checksum field, must add to zero to be considered valid. */
	char 	OEMID[6];			/* An OEM-supplied string that identifies the OEM. */
	char	OEM_table_ID[8];	/* An OEM-supplied string that the OEM uses to identify the particular data table. 
									This field is particularly useful when defining a definition block to distinguish definition block functions. 
									The OEM assigns each dissimilar table a new OEM Table ID. */
	u32		OEM_revision;		/* An OEM-supplied revision number. Larger numbers are assumed to be newer revisions. */
	u32		Creator_ID;			/* Vendor ID of utility that created the table. 
									For tables containing Definition Blocks, this is the ID for the ASL Compiler. */
	u32		Creator_revision;	/*Revision of utility that created the table. 
									For tables containing Definition Blocks, this is the revision for the ASL Compiler.*/
};
/*Root System Description Tables*/
struct acpi_table_rsdt{
	struct	acpi_table_head;
	u32*	entry;
};
#define ACPI_RSDT_ENTRY_SIZE	(4)

/*Extended System Description Table XSDT*/
struct acpi_table_xsdt{
	struct 	acpi_table_head;
	u64*	entry;
};
#define ACPI_XSDT_ENTRY_SIZE	(8)

/*Fixed ACPI Description Table(FADT)*/
struct acpi_table_fadt{
	struct	acpi_table_head;	/* The signature is "FACP",
									and the revision is 6 , and it's the major version,the minor version is at offset 131*/
	u32		FIRMWARE_CTRL;		/* Physical memory address of the FACS, where OSPM and Firmware exchange control information. 
									If the HARDWARE_REDUCED_ACPI flag is set, and both this field and the X_FIRMWARE_CTRL field are zero, there is no FACS available.*/
	u32		DSDT;				/* Physical memory address of the DSDT. */
	u8		reserved1;
	u8		Preferred_PM_profile;	/* OSPM can use this field to set default power management policy parameters during OS installation*/
#define Pref_PM_Profile_Unspecified			0
#define Pref_PM_Profile_Desktop				1
#define Pref_PM_Profile_Mobile				2
#define Pref_PM_Profile_Workstation			3
#define Pref_PM_Profile_Enterprise_Server	4
#define Pref_PM_Profile_SOHO_Serve			5
#define Pref_PM_Profile_Appliance_PC		6
#define Pref_PM_Profile_Performance_Server	7
#define Pref_PM_Profile_Tablet				8
	/* >8 Reserved */
	u16		SCI_INT;			/*System vector the SCI interrupt is wired to in 8259 mode. 
									On systems that do not contain the 8259, this field contains the Global System interrupt number of the SCI interrupt. 
									OSPM is required to treat the ACPI SCI interrupt as a shareable, level, active low interrupt.*/
	u32		SMI_CMD;			/**/
	u8		ACPI_ENABLE;			/**/
	u8		ACPI_DISABLE;
	u8		S4BIOS_REQ;
	u8		PSTATE_CNT;
	u32		PM1a_EVT_BLK;
	u32		PM1b_EVT_BLK;
	u32		PM1a_CNT_BLK;
	u32		PM1b_CNT_BLK;
	u32		PM2_CNT_BLK;
	u32		PM_TMR_BLK;
	u32		GPE0_BLK;
	u32		GPE1_BLK;
	u8		PM1_EVT_LEN;
	u8		PM1_CNT_LEN;
	u8		PM2_CNT_LEN;
	u8		PM_TMR_LEN;
	u8		GPE0_BLK_LEN;
	u8		GPE1_BLK_LEN;
	u8		GPE1_BASE;
	u8		CST_CNT;
	u16		P_LVL2_LAT;
	u16		P_LVL3_LAT;
	u16		FLUSH_SIZE;
	u16		FLUSH_STRIDE;
	u8		DUTY_OFFSET;
	u8		DUTY_WIDTH;
	u8		DAY_ALRM;
	u8		MON_ALRM;
	u8		CENTURY;
	u16		IAPC_BOOT_ARCH;
	u8		reserved2;
	u32		Flags;
	struct	acpi_gas	RESET_REG;
	u8		RESET_VALUE;
	u16		ARM_BOOT_ARCH;
	u8		FADT_Minor_Version;
	u64		X_FIRMWARE_CTRL;
	u64		X_DSDT;
	struct 	acpi_gas		X_PM1a_EVT_BLK;
	struct	acpi_gas		X_PM1b_EVT_BLK;
	struct	acpi_gas		X_PM1a_CNT_BLK;
	struct	acpi_gas		X_PM1b_CNT_BLK;
	struct 	acpi_gas		X_PM2_CNT_BLK;
	struct	acpi_gas		X_PM_TMR_BLK;
	struct	acpi_gas		X_GPE0_BLK;
	struct	acpi_gas		X_GPE1_BLK;
	struct	acpi_gas		SLEEP_CONTROL_REG;
	struct	acpi_gas		SLEEP_STATUS_REG;
	u64		Hypervisor_Vendor_Id;
};


#endif
