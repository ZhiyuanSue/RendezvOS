/*
 *	It's a realization of ACPI Specification for version 6.5
 *	And I rewrite it , which is not a copy of linux acpi files
 */
#ifndef _ACPI_DEFS_H_
#define _ACPI_DEFS_H_

/*define ACPI basic types*/
#define APCI_SIG_RSDP	"RAD PTR"

/* The signatures assigned in acpi specification */
#define ACPI_SIG_APIC	"APIC"  /* Multiple APIC Description Table */
#define ACPI_SIG_BERT	"BERT"  /* Boot Error Record Table*/
#define ACPI_SIG_BGRT	"BGRT"  /* Boot Graphics Resource Table */
#define ACPI_SIG_CCEL	"CCEL"  /* Virtual Firmware Confidential Computing Event Log Table */
#define ACPI_SIG_CPEP	"CPEP"  /* Corrected Platform Error Polling Table */
#define ACPI_SIG_DSDT	"DSDT"  /* Differentiated System Description Table */
#define ACPI_SIG_ECDT	"ECDT"  /* Embedded Controller Boot Resources Table */
#define ACPI_SIG_EINJ	"EINJ"  /* Error Injection Table */
#define ACPI_SIG_ERST	"ERST"  /* Error Record Serialization Table */
#define ACPI_SIG_FACP	"FACP"  /* Fixed ACPI Description Table (FADT) */
#define ACPI_SIG_FACS	"FACS"  /* Firmware ACPI Control Structure */
#define ACPI_SIG_FPDT	"FPDT"  /* Firmware Performance Data Table */
#define ACPI_SIG_GTDT	"GTDT"  /* Generic Timer Description Table */
#define ACPI_SIG_HEST	"HEST"  /* Hardware Error Source Table */
#define ACPI_SIG_MISC	"MISC"  /* Miscellaneous GUIDed Table Entries */
#define ACPI_SIG_MSCT	"MSCT"  /* Maximum System Characteristics Table */
#define ACPI_SIG_MPST	"MPST"  /* Memory Power StateTable */
#define ACPI_SIG_NFIT	"NFIT"  /* NVDIMM Firmware Interface Table */
#define ACPI_SIG_OEMx	"OEMx"  /* OEM Specific Information Tables */
#define ACPI_SIG_PCCT	"PCCT"  /* Platform Communications Channel Table */
#define ACPI_SIG_PHAT	"PHAT"  /* Platform Health Assessment Table */
#define ACPI_SIG_PMTT	"PMTT"  /* Platform Memory Topology Table */
#define ACPI_SIG_PPTT	"PPTT"  /* Processor Properties Topology Table */
#define ACPI_SIG_PSDT	"PSDT"  /* Persistent System Description Table */
#define ACPI_SIG_RASF	"RASF"  /* ACPI RAS Feature Table */
#define ACPI_SIG_RAS2	"RAS2"  /* ACPI RAS2 Feature Table */
#define ACPI_SIG_RSDT	"RSDT"  /* Root System Description Table */
#define ACPI_SIG_SBST	"SBST"  /* Smart Battery Specification Table */
#define ACPI_SIG_SDEV	"SDEV"  /* Secure DEVices Table */
#define ACPI_SIG_SLIT	"SLIT"  /* System Locality Distance Information Table */
#define ACPI_SIG_SRAT	"SRAT"  /* System Resource Affinity Table */
#define ACPI_SIG_SSDT	"SSDT"  /* Secondary System Description Table */
#define ACPI_SIG_SVKL	"SVKL"  /* Storage Volume Key Data table in the Intel Trusted Domain Extensions*/
#define ACPI_SIG_XSDT	"XSDT"  /* Extended System Description Table */

/* The signatures reserved in acpi specification*/
#define ACPI_SIG_RSV_AEST	"AEST"
#define ACPI_SIG_RSV_AGDI	"AGDI"
#define ACPI_SIG_RSV_APMT	"APMT"
#define ACPI_SIG_RSV_BDAT	"BDAT"
#define ACPI_SIG_RSV_BOOT	"BOOT"
#define ACPI_SIG_RSV_CEDT	"CEDT"
#define ACPI_SIG_RSV_CSRT	"CSRT"
#define ACPI_SIG_RSV_DBGP	"DBGP"
#define ACPI_SIG_RSV_DBG2	"DBG2"
#define ACPI_SIG_RSV_DMAR	"DMAR"
#define ACPI_SIG_RSV_DRTM	"DRTM"
#define ACPI_SIG_RSV_DTPR	"DTPR"
#define ACPI_SIG_RSV_ETDT	"ETDT"
#define ACPI_SIG_RSV_HPET	"HPET"
#define ACPI_SIG_RSV_IBET	"IBET"
#define ACPI_SIG_RSV_IERS	"IERS"
#define ACPI_SIG_RSV_IORT	"IORT"
#define ACPI_SIG_RSV_IVRS	"IVRS"
#define ACPI_SIG_RSV_KEYP	"KEYP"
#define ACPI_SIG_RSV_LPIT	"LPIT"
#define ACPI_SIG_RSV_MCFG	"MCFG"
#define ACPI_SIG_RSV_MCHI	"MCHI"
#define ACPI_SIG_RSV_MHSP	"MHSP"
#define ACPI_SIG_RSV_MPAM	"MPAM"
#define ACPI_SIG_RSV_MSDM	"MSDM"
#define ACPI_SIG_RSV_NBFT	"NBFT"
#define ACPI_SIG_RSV_PRMT	"PRMT"
#define ACPI_SIG_RSV_RGRT	"RGRT"
#define ACPI_SIG_RSV_SDEI	"SDEI"
#define ACPI_SIG_RSV_SLIC	"SLIC"
#define ACPI_SIG_RSV_SPCR	"SPCR"
#define ACPI_SIG_RSV_SPMI	"SPMI"
#define ACPI_SIG_RSV_STAO	"STAO"
#define ACPI_SIG_RSV_SWFT	"SWFT"
#define ACPI_SIG_RSV_TCPA	"TCPA"
#define ACPI_SIG_RSV_TPM2	"TPM2"
#define ACPI_SIG_RSV_UEFI	"UEFI"
#define ACPI_SIG_RSV_WAET	"WAET"
#define ACPI_SIG_RSV_WDAT	"WDAT"
#define ACPI_SIG_RSV_WDDT	"WDDT"
#define ACPI_SIG_RSV_WDRT	"WDRT"
#define ACPI_SIG_RSV_WPBT	"WPBT"
#define ACPI_SIG_RSV_WSMT	"WSMT"
#define ACPI_SIG_RSV_XENV	"XENV"
#endif