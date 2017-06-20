/****************************************************************************
*
* Copyright (c) 2010 - 2013 PMC-Sierra, Inc.
*      All Rights Reserved
*
* Distribution of source code or binaries derived from this file is not
* permitted except as specifically allowed for in the PMC-Sierra
* Software License agreement.  All copies of this source code
* modified or unmodified must retain this entire copyright notice and
* comment as is.
*
* THIS SOFTWARE IS PROVIDED "AS IS" AND WITHOUT ANY EXPRESS OR
* IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
* WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
*
* Description                  : PMC NVMe header
*
* $Revision: 1.1 $
* $Date: 2013/09/23 07:04:23 $
* $Author: dvoryyos $
* Release $Name:  $
*
****************************************************************************/

#ifndef __PMC_NVME_H__
#define __PMC_NVME_H__

#include <stdint.h>


#define PRP_SIZE					4096
#define IOCTL_TIMEOUT_SECS			60

#define FW_VER_LEN          8
/* 
   Name: NVRAM_admin_opcode_e Description: Admin Commands Opcodes. */
typedef enum
{
    /* The range 0x0-07F used by the standard commandes */
    ADMIN_OPCODE_DELETE_IO_SQ = 0x00,   /* Delete I/O Submission Queue */
    ADMIN_OPCODE_CREATE_IO_SQ = 0x01,   /* Create I/O Submission Queue */
    ADMIN_OPCODE_GET_LOG_PAGE = 0x02,   /* Get Log Page */
    ADMIN_OPCODE_DELETE_IO_CQ = 0x04,   /* Delete I/O Completion Queue */
    ADMIN_OPCODE_CREATE_IO_CQ = 0x05,   /* Create I/O Completion Queue */
    ADMIN_OPCODE_IDENTIFY = 0x06,   /* Identify */
    ADMIN_OPCODE_ABORT = 0X08,  /* Abort */
    ADMIN_OPCODE_SET_FEATURES = 0x09,   /* Set Features */
    ADMIN_OPCODE_GET_FEATURES = 0x0A,   /* Get Features */
    ADMIN_OPCODE_ASYNC_EVENT_REQUEST = 0x0C,    /* Asynchronous Event Request */
    ADMIN_OPCODE_FW_ACTIVATE = 0x10,    /* Firmware Activate */
    ADMIN_OPCODE_FW_DOWNLOAD = 0x11,    /* Firmware Image Download */

    /* The range 0x80-0xBF reserved for I/O Command Set specific */

    /* The range 0xC0-0xFF reserved Vendor specific */
    /* Defined by PMC */
    ADMIN_OPCODE_VENDOR_SYS_CONFIG = 0xc1,  /* PMC System Configuration Commands Set */
    ADMIN_OPCODE_VENDOR_SYS_DEBUG = 0xC5,   /* PMC System Debug Commands Set */
    ADMIN_OPCODE_VENDOR_SYS_DIAG = 0xca,    /* PMC System Diagnostics Commands Set */
    /* Defined by PMC */
    ADMIN_OPCODE_VENDOR_NVRAM = 0xE0    /* PMC NVRAM Commands Sets */
} NVRAM_admin_opcode_e;


/**
 * @brief Subcommand opcode for System Configuration
 */
typedef enum
{
    /**
     * @brief Option ROM Enable
     */
    SYS_CONFIG_ROM_ENABLE = 0x00,
    /**
     * @brief Format Flash Media
     */
    SYS_CONFIG_FORMAT_FLASH = 0x01,
    /**
     * @brief Delete Namespace
     */
    SYS_CONFIG_DELETE_NMSPC = 0x02,
    /**
     * @brief Create Namespace
     */
    SYS_CONFIG_CREATE_NMSPC = 0x03,
    /**
     * @brief Download EEPROM Image
     */
    SYS_CONFIG_DW_EEPROM_IMAGE = 0x04,
    /**
     * @brief Commit EEPROM Image
     */
    SYS_CONFIG_COMMIT_EEPROM_IMAGE = 0x05,
    /**
     * @brief Flash Bad Block Download
     */
    SYS_CONFIG_FLASH_BAD_BLK_DW = 0x06,
    /**
     * @brief Vendor Defined firmware activate
     */
    SYS_CONFIG_FW_ACTIVATE_VENDOR = 0x07,
    /**
     * @brief Create Storage
     */
    SYS_CONFIG_CREATE_STORAGE = 0x08,
    /**
     * @brief Delete Storage
     */
    SYS_CONFIG_DELETE_STORAGE = 0x09,
    /**
     * @brief Map Namespace ID
     */
    SYS_CONFIG_MAP_NAMESPACE_ID = 0x0A,
    /**
     * @brief Unmap Namespace ID
     */
    SYS_CONFIG_UNMAP_NAMESPACE_ID = 0x0B,
    /**
     * @brief I2C access command
     */
    SYS_CONFIG_I2C_READ = 0x0C,
    /**
     * @brief I2C access command
     */
    SYS_CONFIG_I2C_WRITE = 0x0D,
    /**
     * @brief I2C raw access command
     */
    SYS_CONFIG_I2C_READ_RAW = 0x1C,
    /**
     * @brief I2C raw access command
     */
    SYS_CONFIG_I2C_WRITE_RAW = 0x1D,
    /**
     * @brief End of system Configuration opcode
     */
    SYS_CONFIG_END,
}sys_config_opcodes_e;



/* 
   Name: cq_entry Description: NVMe Completion Queue entry. */
struct cq_entry
{
    /**
     * Command Specific Completion Code.
     */
    union
    {
        uint32_t cmdSpecific;
        uint32_t numSubQAlloc:16, numCplQAlloc:16;
    } param;
    /**
     * Reserved.
     */
    uint32_t reserved;
    uint32_t sqHdPtr:16, sqID:16;
    uint32_t cmdID:16, phaseTag:1, SC:8, SCT:3,:2, more:1, noRetry:1;
};


/* 
   Name: nvme_cmd_vendor_specific Description: NVMe Vendor Specific Command (the last 6 Dwords). */
struct nvme_cmd_vendor_specific
{
    uint32_t buffNumDW;
    uint32_t metaNumDW;
    uint32_t vndrCDW12;
    uint32_t vndrCDW13;
    uint32_t vndrCDW14;
    uint32_t vndrCDW15;
};

/* 
   Name: nvme_cmd_identify Description: NVMe Identify Command (Dword10). */
struct nvme_cmd_identify
{
    /**
     * This field indicates the identify structure to retrieve. If
     * controllerStructure is set as 1, it retrieves the controller structure,
     * otherwise, it retrieves the namespace structure associated with the
     * namespaceID indicated in header.
     */
    uint32_t controllerStructure:1, reserved:31;
};


/* 
   Name: nvme_cmd_hdr Description: NVMe Command header. */
struct nvme_cmd_hdr
{
    /**
     * Opcode.
     *
     * This field indicates the opcode of the command to be executed.
     */
    uint32_t opCode:8,
    /**
     * Fused Operation.
     *
     * In a fused operation, a complex command is created by fusing
     * together two simpler commands. This field indicates whether
     * this command is part of a fused operation and if so, which
     * command it is in the sequence.
     * - 00b Normal operation.
     * - 01b Fused operation, first command.
     * - 10b Fused operation, second command.
     * - 11b Reserved.
     */
      fusedOp:2,
     /**
      * Reserved.
      */
    : 6,
     /**
      * Command Identifier.
      *
      * This field indicates a unique identifier for the command when
      * combined with the Submission Queue identifier.
      */
      cmdID:16;
    /**
     * Napespace Identifier.
     *
     * This field indicates the namespace that this command applies to.
     * If the namespace is not used for the command, then this field
     * shall be cleared to 0h.  If a command shall be applied to all
     * namespaces on the device, then this value shall be set to
     * FFFFFFFFh.
     */
    uint32_t namespaceID;
    /**
     * Reserved.
     */
    uint64_t reserved;
    /**
     * Metadata Pointer.
     *
     * This field contains the address of a contiguous physical buffer of
     * metadata.  This field is only used if metadata is not interleaved
     * with the LBA data, as specified in the Format NVM command. This
     * field shall be Dword aligned.
     */
    uint64_t metadataPtr;
    /**
     * PRP Entry 1 & 2.
     *
     * PRP entry 1 contains the first PRP entry for the command.
     * PRP entry 2 contains the second PRP entry for the command. If the data
     * transfer spans more than two memory pages, then this field is a PRP
     * List pointer.
     */
    uint64_t prp1;
    uint64_t prp2;
};


/* 
   Name: nvme_cmd Description: NVMe Command. */
struct nvme_cmd
{
    union
    {
        struct
        {
            /**
			* Command header.
			*/
            struct nvme_cmd_hdr header;

            union
            {
                /**
				* NVM identify command specific info.
				*/
                struct nvme_cmd_identify identify;
#if 0
                /**
				* NVM set features command specific info.
				*/
                struct nvme_cmd_set_feature setFeatures;
                /**
				* NVM get features command specific info.
				*/
                struct nvme_cmd_get_feature getFeatures;
                /**
				* NVM firmware activate command specific info.
				*/
                struct nvme_cmd_firmware_activate firmwareActivate;
                /**
				* NVM firmware download command specific info.
				*/
                struct nvme_cmd_firmware_download firmwareDownload;
                /**
				* NVM get log page command specific data.
				*/
                struct nvme_cmd_get_log_page getLogPage;
                /**
				* NVM/Admin command Vendor Specific command data.
				*/
#endif                          /* #if 0 */
                struct nvme_cmd_vendor_specific vendorSpecific;
                /**
				* generic command template.
				*/
                uint32_t asUlong[6];

            } cmd;
        };

        uint32_t dw[16];
    };
};


/* 
   Name: usr_io Description: PMC NVMe Command. */
struct usr_io
{
    struct nvme_cmd cmd;        /* Submission queue entry. */
    struct cq_entry comp;       /* Completion entry */
    uint8_t namespace;          /* Namespace ID, -1 for non-specific */
    uint8_t direction;          /* direction TO_DEVICE/FROM_DEVICE */
    uint16_t timeout;           /* timeout in seconds */
    uint32_t status;            /* Command status */
    uint32_t length;            /* data length */
    uint32_t meta_length;       /* meta data length */
    uint64_t addr;              /* data address */
    uint64_t meta_addr;         /* meta data address */
};


/* 
   Name: nvme_admin_cmd Description: Intel NVMe Command. */
struct nvme_admin_cmd
{
    uint8_t opcode;
    uint8_t flags;
    uint16_t rsvd1;
    uint32_t nsid;
    uint32_t cdw2;
    uint32_t cdw3;
    uint64_t metadata;
    uint64_t addr;
    uint32_t metadata_len;
    uint32_t data_len;
    uint32_t cdw10;
    uint32_t cdw11;
    uint32_t cdw12;
    uint32_t cdw13;
    uint32_t cdw14;
    uint32_t cdw15;
    uint32_t timeout_ms;
    uint32_t result;
};


/* 
   Name: nvme_arg_s Description: ioctl argument for NVMe commands. */
typedef struct
{
    struct nvme_cmd nvme_cmd;   /* IN - NMVe command */
    uint32_t data_length;       /* IN - NMVe command timeout in seconds */
    uint8_t do_not_print_err;   /* IN - supress NMVe error */
    uint32_t cq_dword0;         /* OUT - Compliation Queue's Dword #0 (Command Specific) */
    uint8_t sct;                /* OUT - Compliation Queue's Status Code Type (SCT) */
    uint8_t sc;                 /* OUT - Compliation Queue's Status Code (SC) */


} nvme_arg_s;




/**
 * @brief Identify - Power State Descriptor Data Structure.
 */
struct pwr_state_desc
{
    /**
     * @brief Maximum Power.
     *
     * This field indicates the maximum power consumed by the NVM
     * subsystem in this power state. The power in Watts is equal to the value
     * in this field multiplied by
     * 0.01
     */
    uint16_t maxPower;
    /**
     * @brief Reserved.
     */
    uint16_t reservedA;
    /**
     * @brief Entry Latency.
     *
     * This field indicates the maximum entry latency in microseconds
     * associated with entering this power state.
     */
    uint32_t entryLat;
    /**
     * @brief Exit Latency.
     *
     * This field indicates the maximum exit latency in microseconds
     * associated with entering this power state.
     */
    uint32_t exitLat;
    /**
     * @brief Relative Read Throughput.
     *
     * This field indicates the relative read throughput associated
     * with this power state. The value in this field shall be less than the
     * number of supported power states (e.g., if the controller supports
     * 16 power states, then valid values are 0 through 15). A lower value
     * means higher read throughput.
     */
    uint8_t relRdThpt;
    /**
     * @brief Relative Read Latency.
     *
     * This field indicates the relative read latency associated with
     * this power state. The value in this field shall be less than the number
     * of supported power states (e.g., if the controller supports 16 power
     * states, then valid values are 0 through 15). A lower value means lower
     * read latency.
     */
    uint8_t relRdLat;
    /**
     * @brief Relative Write Throughput.
     *
     * This field indicates the relative write throughput
     * associated with this power state. The value in this field shall be less
     * than the number of supported power states (e.g., if the controller
     * supports 16 power states, then valid values are 0 through 15). A lower
     * value means higher write throughput.
     */
    uint8_t relWrThpt;
    /**
     * @brief Relative Write Latency.
     *
     * This field indicates the relative write latency associated with
     * this power state. The value in this field shall be less than the number
     * of supported power states (e.g., if the controller supports 16 power
     * states, then valid values are 0 through 15). A lower value means lower
     * write latency.
     */
    uint8_t relWrLat;
    /**
     * @brief Reserved.
     */
    uint8_t reserveds[16];
};



/**
 * @brief Identify - Controller Data Structure.
 */
struct iden_controller
{
    /**
     * @brief PCI Vendor ID
     *
     * Contains the company vendor identifier that is assigned by the
     * PCI SIG. This is the same value as reported in the ID register in
     * section 2.1.1.
     */
    uint16_t pcieVID;
    /**
     * @brief PCI Subsystem Vendor ID
     *
     * Contains the company vendor identifier that is
     * assigned by the PCI SIG for the subsystem. This is the same value as
     * reported in the SS register.
     */
    uint16_t pcieSSVID;
    /**
     * @brief Serial Number
     *
     * Contains the serial number for the NVM subsystem that is
     * assigned by the vendor as an ASCII string.
     */
    uint8_t serialNum[20];
    /**
     * @brief Model Number
     *
     * Contains the model number for the NVM subsystem that is
     * assigned by the vendor as an ASCII string.
     */
    uint8_t modelNum[40];
    /**
     * @brief Firmware Revision
     *
     * Contains the currently active firmware revision for the
     * NVM subsystem. This is the same revision information that may be
     * retrieved with the Get Log Page command.
     */
    uint8_t firmwareRev[FW_VER_LEN];
    /**
     * @brief Recommended Arbitration Burst
     *
     * This is the recommended Arbitration Burst size.
     */
    uint8_t arbBurstSize;
    /**
     * @brief Reserved
     */
    uint8_t reservedA[255-73+1];
    /**
     * @brief Optional Admin Command Support
     *
     * This field indicates the optional
     * Admin commands supported by the controller.
     * - Bits 15:3 are reserved.
     * - Bit 2 if set to 1 then the controller supports the Firmware Activate
     *   and Firmware Download commands. If cleared to 0 then the controller
     *   does not support the Firmware Activate and Firmware Download commands.
     * - Bit 1 if set to 1 then the controller supports the Format NVM command.
     *   If cleared to 0 then the controller does not support the Format NVM
     *   command.
     * - Bit 0 if set to 1 then the controller supports the Security Send and
     *   Security Receive commands. If cleared to 0 then the controller does
     *   not support the Security Send and Security Receive commands.
     */
    uint16_t adminCmdSup;
    /**
     * @brief Abort Command Limit
     *
     * This field is used to convey the maximum number of
     * concurrently outstanding Abort commands supported by the controller.
     * This is a 0s based value. It is recommended that implementations support
     * a minimum of four Abort commands outstanding simultaneously.
     * abortComLmt;
     */
    uint8_t abortCmdLmt;
    /**
     * @brief Asynchronous Event Request Limit
     *
     * This field is used to convey the
     * maximum number of concurrently outstanding Asynchronous Event Request
     * commands supported by the controller. This is a 0s based value.
     * It is recommended that implementations support a minimum of four
     * Asynchronous Event Request Limit commands oustanding simultaneously.
     */
    uint8_t asyncReqLmt;
    /**
     * @brief Firmware Updates
     *
     * This field indicates capabilities regarding firmware
     * updates.
     * - Bits 7:4 are reserved.
     * - Bits 3:1 indicate the number of firmware slots that the device
     *   supports. This field shall specify a value between one and seven,
     *   indicating that at least one firmware slot is supported and up to
     *   seven maximum. This corresponds to firmware slots 1 through 7.
     * - Bit 0 if set to 1 indicates that the first firmware slot (slot 1)
     *   is read only. If cleared to 0 then the first firmware slot (slot 1)
     *   is read/write. Implementations may choose to have a baseline
     *   read only firmware image.
     */
    uint8_t firmUpdt;
    /**
     * @brief Log Page Attributes
     *
     * This field indicates optional attributes for log pages that
     * are accessed via the Get Log Page command.
     * - Bits 7:1 are reserved.
     * - Bit 0 if set to 1 then the controller supports the SMART / Health
     *   information log page on a per namespace basis. If cleared to 0
     *   then the controller does not support the SMART / Health information
     *   log page on a per namespace basis; the log page returned is global
     *   for all namespaces.
     */
    uint8_t logPgAttrib;
    /**
     * @brief Error Log Page Entries
     *
     * This field indicates the number of Error Information
     * log entries that are stored by the controller. This field is a 0s based
     * value.
     */
    uint8_t errLogPgEntr;
    /**
     * @brief Number of Power States Support
     *
     * This field indicates the number of
     * NVMHCI power states supported by the controller. This is a 0s based
     * value. Power states are numbered sequentially starting at power state 0.
     * A controller shall support at least one power state (i.e., power
     * state 0) and may support up to 31 additional power states
     * (i.e., up to 32 total).
     */
    uint8_t numPowerSt;
    /**
     * @brief Admin Vendor Specific Command Configuration (AVSCC): 
     *  
     * This field indicates the configuration settings for admin vendor 
     * specific command handling.
     *
     * Bits 7:1 are reserved.
     *
     * Bit 0 if set to .1. indicates that all Admin Vendor Specific Commands 
     * use the format defined in Figure 8. If cleared to .0. indicates that 
     * format of all Admin Vendor Specific Commands is vendor specific.
     */
    uint8_t admVendCmdCfg;
    /**
     * @brief Reserved
     */
    uint8_t reservedB[511-265+1];
    /**
     * @brief Submission Queue Entry Size
     *
     * This field defines the required and
     * maximum Submission Queue entry size when using the NVM Command Set.
     * - Bits 7:4 define the maximum Submission Queue entry size when using
     *   the NVM Command Set. This value is larger than or equal to the
     *   required SQ entry size. The value is in bytes and is reported as
     *   a power of two (2^n).
     * - Bits 3:0 define the required Submission Queue Entry size when using
     *   the NVM Command Set. This is the minimum entry size that may be used.
     *   The value is in
     *   bytes and is reported as a power of two (2^n). The required value
     *   shall be 6, corresponding to 64.
     */
    uint8_t subQSize;
    /**
     * @brief Completion Queue Entry Size
     *
     * This field defines the required and
     * maximum Completion Queue entry size when using the NVM Command Set.
     * - Bits 7:4 define the maximum Completion Queue entry size when using
     *   the NVM Command Set. This value is larger than or equal to the
     *   required CQ entry size. The value is in bytes and is reported as
     *   a power of two (2^n).
     * - Bits 3:0 define the required Completion Queue entry size when using
     *   the NVM Command Set. This is the minimum entry size that may be used.
     *    The value is in bytes and is reported as a power of two (2^n).
     *    The required value shall be 4, corresponding to 16.
     */
    uint8_t compQSize;
    /**
     * @brief Reserved
     */
    uint8_t reservedC[515-514+1];
    /**
     * @brief Number of Namespaces
     *
     * This field defines the number of valid namespaces
     * present for the controller. Namespaces shall be allocated in order
     * (starting with 0) and packed sequentially. This is a 0s based value.
     */
    uint32_t numNmspc;
    /**
     * @brief Optional NVM Command Support
     *
     * This field indicates the optional NVM
     * commands supported by the controller. Refer to section 6.
     * - Bits 15:3 are reserved.
     * - Bit 2 if set to 1 then the controller supports the Dataset Management
     *   command. If cleared to 0 then the controller does not support the
     *   Dataset Management command.
     * - Bit 1 if set to 1 then the controller supports the Write Uncorrectable
     *   command. If cleared to 0 then the controller does not support the
     *   Write Uncorrectable command.
     * - Bit 0 if set to 1 then the controller supports the Compare command.
     *   If cleared to 0 then the controller does not support the Compare
     *   command.
     */
    uint16_t cmdSupt;
    /**
     * @brief Fused Operation Support
     *
     * This field indicates the fused operations that
     * the controller supports.
     * - Bits 15:1 are reserved.
     * - Bit 0 if set to 1 then the controller supports the Compare and Write
     *   fused operation. If cleared to 0 then the controller does not support
     *   the Compare and Write fused operation. Compare shall be the first
     *   command in the sequence.
     */
    uint16_t fuseSupt;
    /**
     * @brief Format NVM Attributes
     *
     * This field indicates attributes for the Format NVM
     * command.
     * - Bits 7:3 are reserved.
     * - Bit 2 indicates whether cryptographic erase is supported as part of
     *   the secure erase functionality. If set to 1, then cryptographic
     *   erase is supported. If cleared to 0, then cryptographic erase is
     *   not supported.
     * - Bit 1 indicates whether secure erase functionality applies to all
     *   namespaces or is specific to a particular namespace. If set to1,
     *   then a secure erase of a particular namespace as part of a format
     *   results in a secure erase of all namespaces. If cleared to 0, then
     *   a secure erase as part of a format is performed on a per namespace
     *   basis.
     * - Bit 0 indicates whether the format operation applies to all namespaces
     *   or is specific to a particular namespace. If set to 1, then all
     *   namespaces shall be configured with the same attributes and a format
     *   of any namespace results in a format of all namespaces. If cleared
     *   to 0, then the controller supports format on a per namespace basis.
     */
    uint8_t cmdAttrib;
    /**
     * @brief Volatile Write Cache
     *
     * This field indicates attributes related to the presence
     * of a volatile write cache in the implementation.
     * - Bits 7:1 are reserved.
     * - Bit 0 if set to 1 indicates that a volatile write cache is present.
     *   If cleared to 0, a volatile write cache is not present. If a volatile
     *   write cache is present, then the host may issue Flush commands and
     *   control whether it is enabled with Set Features specifying the
     *   Volatile Write Cache feature identifier. If a volatile write cache
     *   is not present, the host shall not issue Flush commands nor Set
     *   Features or Get Features with the Volatile Write Cache identifier.
     */
    uint8_t volWrCache;
    /**
     * @brief Atomic Write Unit Normal
     *
     * This field indicates the atomic write size for the
     * controller during normal operation. This field is specified in logical
     * blocks and is a 0s based value. If a write is issued of this size or
     * less, the host is guaranteed that the write is atomic to the NVM with
     * respect to other read or write operations. A value of FFh indicates
     * all commands are atomic as this is the largest command size. It is
     * recommended that implementations support a minimum of 128KB
     * (appropriately scaled based on LBA size).
     */
    uint16_t atomWrNorm;
    /**
     * @brief Atomic Write Unit Power Fail
     *
     * This field indicates the atomic write size for the controller during a
     * power fail condition. This field is specified in logical blocks and
     * is a 0s based value. If a write is issued of this size or less, the
     * host is guaranteed that the write is atomic to the NVM with respect
     * to other read or write operations.
     */
    uint16_t atomWrFail;
    /**
     * @brief NVM Vendor Specific Command Configuration (NVSCC):
     * This field indicates the configuration settings for NVM vendor
     * specific command handling.
     *
     * Bits 7:1 are reserved.
     * 
     * Bit 0 if set to .1. indicates that all NVM Vendor Specific Commands
     * use the format defined in Figure 8. If cleared to .0. indicates that
     * format of all NVM Vendor Specific Commands is vendor specific.
     */
    uint8_t nvmVendCmdCfg;
    /**
     * @brief Reserved
     */
    uint8_t reservedE[2047-531+1];
    /**
     * @brief Power State Descriptors
     *
     * This field indicates the characteristics of the power states.
     */
    struct pwr_state_desc pwrStateDesc[32];

    /**
     * @brief Vendor Specific
     * This range of bytes is allocated for vendor specific usage.
     */
    uint8_t resevedF[4095-3072+1];
};






/* 
   Name: NVME_IOCTL_ADMIN_CMD Description: ioctl CMD for NVMe Admin Commands. */
//#define NVME_IOCTL_ADMIN_CMD	_IO('N', 0x41)
#define NVME_IOCTL_ADMIN_CMD (Get_is_pmc_driver()?NVME_IOCTL_PMC_ADMIN_CMD:NVME_IOCTL_INBOX_ADMIN_CMD)
#define NVME_IOCTL_PMC_ADMIN_CMD	_IOWR('N', 0x41, struct usr_io)
#define NVME_IOCTL_RESTART      _IO('N', 0x43)
#define NVME_IOCTL_EVENT		_IO('N', 0x46)
#define NVME_IOCTL_INBOX_ADMIN_CMD	_IOWR('N', 0x41, struct nvme_admin_cmd)


#endif /* __PMC_NVME_H__ */
