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
* Description                  : PMC NVRAM API header
*
* $Revision: 1.1 $
* $Date: 2013/09/23 07:04:23 $
* $Author: shivatzi $
* Release $Name:  $
*
****************************************************************************/

#ifndef __PMC_NVRAM_API_H__
#define __PMC_NVRAM_API_H__

#include <stdint.h>
#include "pmc_nvram_api_expo.h"

/* Size of the serial number */
#define NVRAM_SN_SIZE		64

/* 
   Name: NVRAM_vendor_subcmd_e Description: Vendor Specific Sub Commands. */
typedef enum
{
    NVRAM_VENDOR_SUBCMD_INFO_GET                = 0x00, /* Get NVRAM info */
    NVRAM_VENDOR_SUBCMD_BACKUP                  = 0x01, /* Backup RAM to flash bank */
    NVRAM_VENDOR_SUBCMD_RESTORE                 = 0x02, /* Restore RAM from flash bank */
    NVRAM_VENDOR_SUBCMD_FLASH_BANK_INFO_GET     = 0x03, /* Get flash bank info */
    NVRAM_VENDOR_SUBCMD_CONFIG_SET              = 0x04, /* Set NVRAM configuration */
    NVRAM_VENDOR_SUBCMD_CONFIG_GET              = 0x05, /* Get NVRAM configuration */
    NVRAM_VENDOR_SUBCMD_AUTHENTICATE_MASTER     = 0x06, /* Authentication master between Host and NVM controller */
    NVRAM_VENDOR_SUBCMD_AUTHENTICATE_ADMIN      = 0x07, /* Authentication admin between Host and NVM controller */
    NVRAM_VENDOR_SUBCMD_ERASE                   = 0x08, /* Erase flash bank */
    NVRAM_VENDOR_SUBCMD_BLOCK_CONTENT_SET       = 0x09, /* Write block content */
    NVRAM_VENDOR_SUBCMD_STATUS_GET              = 0x0B, /* Get NVRAM device statuses */
    NVRAM_VENDOR_SUBCMD_EVENTS_GET              = 0x0C, /* Get event list */
    NVRAM_VENDOR_SUBCMD_DDR_CONTENT_GET         = 0x0D, /* Get DDR content */
    NVRAM_VENDOR_SUBCMD_EVENT_REGISTER          = 0x0E, /* Register event */
    NVRAM_VENDOR_SUBCMD_TIME_OF_DAY_SET         = 0x0F, /* Set the current time of day(in seconds) */
    NVRAM_VENDOR_SUBCMD_STATS_GET               = 0x10, /* Get statistics from NVRAM device */
    NVRAM_VENDOR_SUBCMD_STATS_RESET             = 0x11, /* Get statistics from NVRAM device */
    NVRAM_VENDOR_SUBCMD_SCAN_BAD_BLOCKS         = 0x12, /* Scan flash for Bad Blocks */
    NVRAM_VENDOR_SUBCMD_HEARTBEAT_COMMAND       = 0x13, /* Host heartbit command */
    NVRAM_VENDOR_SUBCMD_HEARTBEAT_INDICATION    = 0x14, /* Host heartbit indication */
    NVRAM_VENDOR_SUBCMD_FW_FAIL_TEST            = 0x15, /* Debug FW fail */
    NVRAM_VENDOR_SUBCMD_PAGE_CONTENT_GET        = 0x16, /* get page content                                    */
    NVRAM_VENDOR_SUBCMD_LBA_ADDR_MAP_GET        = 0x17, /* Get LBA <=> DDR Address mapping                    */
    NVRAM_VENDOR_SUBCMD_RESTORE_CORRUPTED       = 0x18, /* Restore corrupted RAM image from flash bank        */
    NVRAM_VENDOR_SUBCMD_INJECT_UC_ERRORS        = 0x19, /* Inject uncorrectable errors                            */
    NVRAM_VENDOR_SUBCMD_DEBUG_BAD_BLOCK_SET     = 0x1A, /* Set bad block count                             */
    NVRAM_VENDOR_SUBCMD_DEBUG_BAD_BLOCK_SET_LUN = 0x1B, /* Set blocks as bad/good in specific LUN         */
    NVRAM_VENDOR_SUBCMD_SELF_TEST               = 0x1C, /* Run self test */
    NVRAM_VENDOR_SUBCMD_FRIM_MAPPING_GET        = 0x1D, /* Get FRIM mapping Meta Data */
    NVRAM_VENDOR_SUBCMD_REGISTER_ACCESS         = 0x1E, /* Hardware Register Set and Get */
    NVRAM_VENDOR_SUBCMD_FRIM_MAPPING_CLEAR      = 0x1F, /* Clear FRIM block mapping */
    NVRAM_VENDOR_SUBCMD_DEBUG_CONFIG_SET        = 0x20, /* Set NVRAM Debug configuration                    */
    NVRAM_VENDOR_SUBCMD_DEBUG_CONFIG_GET        = 0x21, /* Get NVRAM Debug configuration                    */
    NVRAM_VENDOR_SUBCMD_SIMULATE_BAD_BLOCK      = 0x22, /* Simulate bad block (dynamically) */
    NVRAM_VENDOR_SUBCMD_PROD_FEATURE_INFO_GET   = 0x23, /* Get PRODUCT feature info */
    NVRAM_VENDOR_SUBCMD_RAMDISK_ENC_KEY_SET     = 0x24, /* Set RAM disk encryption key                    */
    NVRAM_VENDOR_SUBCMD_FW_BAD_BLOCK_GET        = 0x25, /* Get FW area bad block list               */
    NVRAM_VENDOR_SUBCMD_DEBUG_FW_BAD_BLOCK_SET  = 0x26, /* Set FW area bad block                */

    NVRAM_VENDOR_SUBCMD_USER_END,                       /* End of user subcommands                    */

    /* SUBCMDs for internal use */
    NVRAM_VENDOR_SUBCMD_INTERNAL_FIRST          = 0x50, /* First Internal API */
    NVRAM_VENDOR_SUBCMD_INTERNAL_DATA_GET       = NVRAM_VENDOR_SUBCMD_INTERNAL_FIRST, /* Get Internal NVRAM data */

    NVRAM_VENDOR_SUBCMD_LAST    /* Last subcommand */
} NVRAM_vendor_subcmd_e;


/* 
   Name: NVRAM_internal_data_group_e Description: NVRAM internal data group. */
typedef enum
{
    NVRAM_INTERNAL_DATA_GROUP_FIRST = 0,    /* First group ID. Must be 0 */
    NVRAM_INTERNAL_DATA_GROUP_GENERAL = NVRAM_INTERNAL_DATA_GROUP_FIRST,    /* NVRAM internal General data group (to
                                                                               use with NVRAM_internal_data_u.general) */
    NVRAM_INTERNAL_DATA_GROUP_HEARTBEAT = 1,    /* NVRAM internal heartbeat data group (to use with
                                                   NVRAM_internal_data_u.heartbeat) */

    NVRAM_INTERNAL_DATA_GROUP_NUMBER    /* Total number of groups. Must be last in enumeration */
} NVRAM_internal_data_group_e;


/* 
   Name: NVRAM_internal_data_u Description: NVRAM internal data which can be retrieved from NVRAM system. */
typedef union
{
    struct
    {
        char card_serial_number[NVRAM_SN_SIZE]; /* Serial number of the NVRAM card */

    } general;

    struct
    {
        uint32_t heartbeat_msg_interval;    /* Heartbeat msg interval */

    } heartbeat;

} NVRAM_internal_data_u;

/* 
   Name: NVRAM_debug_bad_block_by_lun Description: NVRAM debug struct which is used to set spesific blocks as bad
   block. */
typedef struct
{
    /* DW 13 */
    uint16_t num_of_blocks;     /* The number of blocks to modify */
    uint16_t start_block;       /* The first block */
    /* DW 14 */
    uint8_t chan;               /* The channel number */
    uint8_t target;             /* The target number */
    uint8_t lun;                /* The lun number */
    uint8_t is_bad;             /* set as bad block? */
    /* DW 15 */
    NVRAM_flash_bank_id_e bank; /* The bank to use */
} NVRAM_debug_bad_block_by_lun;



// ////////////////////////////////////////////////////////////////////////////////////////////////////////////
#endif /* __PMC_NVRAM_API_H__ */
