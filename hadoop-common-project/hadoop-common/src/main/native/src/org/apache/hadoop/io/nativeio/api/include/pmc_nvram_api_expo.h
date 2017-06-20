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
* $Author: dvoryyos $
* Release $Name:  $
*
****************************************************************************/

#ifndef __PMC_NVRAM_API_EXPO_H__
#define __PMC_NVRAM_API_EXPO_H__

#include <stdint.h>
#include "pmc_defines.h"

/* 
 * how many data frames for the log / backup log,
 * each frame has 32 entries of 128 Byte each.
 * if changed here, need to change also in more places in systemConfig.h 
 * maximum tested is 30, can use up to 31,
 * (because 32*32 > 1000, and on 1000 there is WA which we need to know about)
 */
#define PAGES_PER_CORE (30)


/**********************************************************/
/* NVRAM API version */
/**********************************************************/
/*
 *
 * Regarding to the version, we have to address the following:
 * 1. Significant change (e.g. new spec support, API compatibility break, etc.)
 * 2. Incremental maintenance release
 * 3. Internal build
 * 4. Fixes for already released fw
 *  
 * Thus, the following format: S.M.BB.F represents:
 *  
 *    - S specifies the item #1 above (one character) - *** MUST be the same as the API version ***
 *    - M is for item #2 (one character) - *** MUST be the same as the API version ***
 *    - BB is for item #3 (2 characters)
 *    - F is for item #4 (one character)
 */
#define NVRAM_API_LIB_VER_MAJOR      "2"
#define NVRAM_API_LIB_VER_MINOR      "7"
#define NVRAM_API_LIB_VER_BUILD      "02"
#define NVRAM_API_LIB_MAINTENANCE    "0"


/**********************************************************/
/* NVRAM Data Types */
/**********************************************************/

/* Maximal number of Mt Ramon devices in the system,
   NOTE: May be changed by the user according to the system */
#define MAX_NUM_DEVICES        16


/* Size of the card UID */
#define NVRAM_CARD_UID_SIZE        64

/* Size of vesrion buffer */
#define NVRAM_VERSION_SIZE        16

/* Number of log strings per page */
#define NVRAM_LOG_STRINGS_PER_PAGE        32
/* Size of log string */
#define NVRAM_LOG_STRING_SIZE        128
/* Firmware wraparound max line number */
#define NVRAM_FW_LOG_PAGE_WA        999
/* maximum length of FW file path */
#define NVRAM_FW_PATH_MAX_LEN        256
/* maximum FW image size (in bytes) */
#define NVRAM_MAX_FW_IMAGE_SIZE        1024*1024*2 /* 2MB */
/* maximum FW image size (in bytes) */
#define PRP_SIZE                4096



/* Size of authentication key */
#define NVRAM_AUTH_KEY_SIZE        32  /* 256 bits */

/* Size of encryption key */
#define NVRAM_ENC_KEY_SIZE        32  /* 256 bits */

/* Size of RAM disk encryption key */
#define NVRAM_RAMDISK_ENC_KEY_SIZE        32  /* 256 bits */


/* 
   Memory map flags. */
#define NVRAM_MEM_MAP_FLAGS_DATA_NONE            0x01    /* Memory data cannot be accessed */
#define NVRAM_MEM_MAP_FLAGS_DATA_READ            0x02    /* Memory data can be read */
#define NVRAM_MEM_MAP_FLAGS_DATA_WRITE            0x04    /* Memory data can be written */
#define NVRAM_MEM_MAP_FLAGS_DATA_EXEC            0x08    /* Memory data can be executed */


/* 
   Events polling interval bounds. */
#define EVENTS_POLLING_INTERVAL_MIN                500 /* millisecond */
#define EVENTS_POLLING_INTERVAL_MAX                5000    /* millisecond - NOTE!!! bigger value may cause problem since
                                                           FW time counter wrap around every ~8000 milliseconds */

/******************************************/
/* Event subtype bitmasks */
/******************************************/

/* VAULT Event subtype bitmask */
#define NVRAM_EVENT_SUBTYPE_BACKUP_FINISHED                  (1 << 0)
#define NVRAM_EVENT_SUBTYPE_RESTORE_FINISHED                  (1 << 1)
#define NVRAM_EVENT_SUBTYPE_ERASE_FINISHED                      (1 << 2)
#define NVRAM_EVENT_SUBTYPE_BAD_BLOCK_SCAN_FINISHED      (1 << 3)

/* CPU Event subtype bitmask */
#define NVRAM_EVENT_SUBTYPE_CPU_TEMPERATURE                (1 << 0)
#define NVRAM_EVENT_SUBTYPE_CPU_CRITICAL_TEMPERATURE        (1 << 1)

/* SYSTEM Event subtype bitmask */
#define NVRAM_EVENT_SUBTYPE_SYSTEM_TEMPERATURE                (1 << 0)
#define NVRAM_EVENT_SUBTYPE_SYSTEM_CRITICAL_TEMPERATURE    (1 << 1)

/* BACKUP_POWER Event subtype bitmask */
#define NVRAM_EVENT_SUBTYPE_BACKUP_POWER_TEMPERATURE        (1 << 0)
#define NVRAM_EVENT_SUBTYPE_BACKUP_POWER_CRITICAL_TEMPERATURE    (1 << 1)
#define NVRAM_EVENT_SUBTYPE_BACKUP_POWER_CHARGE_LEVEL        (1 << 2)
#define NVRAM_EVENT_SUBTYPE_BACKUP_POWER_VOLTAGE            (1 << 3)
#define NVRAM_EVENT_SUBTYPE_BACKUP_POWER_CURRENT            (1 << 4)
#define NVRAM_EVENT_SUBTYPE_BACKUP_POWER_BALANCE            (1 << 5)
#define NVRAM_EVENT_SUBTYPE_BACKUP_POWER_EOL                (1 << 6)    // To check
#define NVRAM_EVENT_SUBTYPE_BACKUP_POWER_LEARNING            (1 << 7)    // To check
#define NVRAM_EVENT_SUBTYPE_BACKUP_POWER_HEALTH                (1 << 8)
#define NVRAM_EVENT_SUBTYPE_BACKUP_POWER_SRC                (1 << 9)
#define NVRAM_EVENT_SUBTYPE_BACKUP_POWER_ERROR                (1 << 10)   // To check


/* DDR ECC Event subtype bitmask */
#define NVRAM_EVENT_SUBTYPE_DDR_ECC_ERROR                    (1 << 0)

/* GENERAL Event subtype bitmask */
#define NVRAM_EVENT_SUBTYPE_GENERAL_BIST                    (1 << 0)
/* BIST ERRORS types */
#define BIST_ERROR_SUPERCAP_UNCONNECTED     (1<<0)
#define BIST_ERROR_FLASH_MODULE_UCONNECTED     (1<<1)
#define BIST_ERROR_SUPERCAP_FATAL_ERROR        (1<<2)
#define BIST_ERROR_DIE_TEMP_ERROR        (1<<3)
#define BIST_ERROR_SUPERCAP_HEALTH_ERROR    (1<<4)
#define BIST_ERROR_SUPERCAP_OPERATION_ERROR    (1<<5)
#define BIST_ERROR_TEMP_SENSOR_ALERT        (1<<6)
#define BIST_ERROR_FLASH_CHANNEL_ERROR_MAP(_bits_)    ((_bits_ & 0xFF) << 8)
#define BIST_ERROR_DDR_FAILED                (1 << 16)

#define NVRAM_NO_AUTH_KEY          "__SPECIAL_KEY_NO_AUTHENTICATION_"


/* heartbeat configuration constants */
#define NVRAM_HEARTBEAT_INTERVAL_VAL_MIN 1  /* minimal number of seconds - 1 second */
#define NVRAM_HEARTBEAT_INTERVAL_VAL_MAX 10 /* maximal number of seconds - 10 seconds */
#define NVRAM_HEARTBEAT_LOST_MSGS_MIN 3 /* minimal number of lost messages */
#define NVRAM_HEARTBEAT_LOST_MSGS_MAX 7 /* maximal number of lost messages */


/* config bounds */
#define NVRAM_CONFIG_TYPE_CPU_TEMPERATURE_THRESHOLD_MIN 272
#define NVRAM_CONFIG_TYPE_CPU_TEMPERATURE_THRESHOLD_MAX 368
#define NVRAM_CONFIG_TYPE_CPU_CRITICAL_TEMPERATURE_THRESHOLD_MIN 272
#define NVRAM_CONFIG_TYPE_CPU_CRITICAL_TEMPERATURE_THRESHOLD_MAX 368
#define NVRAM_CONFIG_TYPE_SYSTEM_TEMPERATURE_THRESHOLD_MIN 272
#define NVRAM_CONFIG_TYPE_SYSTEM_TEMPERATURE_THRESHOLD_MAX 358
#define NVRAM_CONFIG_TYPE_SYSTEM_CRITICAL_TEMPERATURE_THRESHOLD_MIN 272
#define NVRAM_CONFIG_TYPE_SYSTEM_CRITICAL_TEMPERATURE_THRESHOLD_MAX 358
#define NVRAM_CONFIG_TYPE_BACKUP_POWER_TEMPERATURE_THRESHOLD_MIN 272
#define NVRAM_CONFIG_TYPE_BACKUP_POWER_TEMPERATURE_THRESHOLD_MAX 323
#define NVRAM_CONFIG_TYPE_BACKUP_POWER_CHARGE_LEVEL_THRESHOLD_MIN 1
#define NVRAM_CONFIG_TYPE_BACKUP_POWER_CHARGE_LEVEL_THRESHOLD_MAX 100
#define NVRAM_CONFIG_TYPE_DMI_SIZE_MIN 0x1  // bytes
#define NVRAM_CONFIG_TYPE_DMI_SIZE_MAX 0x400000000  // 0x400000000 bytes = 16 Gbytes
#define NVRAM_CONFIG_TYPE_RAMDISK_SIZE_MIN 0x1  // bytes
#define NVRAM_CONFIG_TYPE_RAMDISK_SIZE_MAX 0x400000000  // 0x400000000 bytes = 16 Gbytes

/* 
 *  Debug defines to mark all even or all odd blocks in lun 
 */
#define DBG_ALL_EVEN_BLOCKS 1000
#define DBG_ALL_ODD_BLOCKS     1001

/*
 * maximal defines in MTR_CARD, aligned to the block_map array in nvram manager
 */
#define MAX_CHANNELS  8
#define MAX_TARGETS   2
#define MAX_LUNS      1
#define MAX_BLOCKS    1000

/*
 * maximal defines in MTR_CARD, as they are listed in FW code
 */
#define FW_MAX_CHANNELS 32
#define FW_MAX_TARGETS   8
#define FW_MAX_LUNS      2


/*
 * Number of FW slots
 */
#define NUM_OF_FW_SLOTS  3

/* 
   Name: NVMe SCT Description: NVMe SCT codes which returned by the FW. */
typedef enum
{
    NVME_STATUS_SCT_VENDOR_SPC = 0x7    /* Vendor Specific SCT */
} NVME_STATUS_sct_e;


/* 
   Name: NVMe SC Description: NVMe SC codes which returned by the FW. */
typedef enum
{
    NVME_STATUS_SC_VENDOR_SPC_AUTH_FAILED = 0xD0,   /* Authentication failed SC */
    NVME_STATUS_SC_VENDOR_SPC_BUSY = 0xD1,  /* System is busy, try later */
    NVME_STATUS_SC_VENDOR_SPC_SUPER_CAP_NOT_EXIST = 0xD2,   /* super cap not exist */
    NVME_STATUS_SC_VENDOR_SPC_FLASH_OPERATED = 0xD3,    /* flash is under the operation, try later */
    NVME_STATUS_SC_VENDOR_SPC_BAD_PARAM = 0xD4, /* bad parameter value in commnad */
    NVME_STATUS_SC_VENDOR_SPC_UNSUPPORTED = 0xD5,   /* The operation is unsupported */
    NVME_STATUS_SC_VENDOR_SPC_NOT_PERMITTED = 0xD6,  /* The operation is not permitted */
    NVME_STATUS_SC_VENDOR_SPC_BANK_IS_EMPTY = 0xD7  /* The erase failed, bank is already empty */
} NVME_STATUS_sc_e;


/* 
   Name: P_STATUS Description: Error codes which returned by the NVRAM APIs. */
typedef enum
{
    P_STATUS_OK = 0x0000,       /* No Error */

    /* NVMe Errors */
    /*=============*/
    P_STATUS_NVME_GEN_ERROR = 0x0C00,   /* General NVMe error */

    /* Generic Command Status (SCT = 0h) */
    P_STATUS_NVME_DATA_TRANSFER_ERROR = 0x0004, /* Data Transfer Error */
    P_STATUS_NVME_ABORTED_DUE_TO_POWER_LOSS = 0x0005,   /* Commands Aborted due to Power Loss Notification */
    P_STATUS_NVME_INTERNAL_DEVICE_ERROR = 0x0006,   /* Internal Device Error */
    P_STATUS_NVME_ABORT_REQUESTED = 0x0007, /* Command Abort Requested */

    /* Command Specific Status (SCT = 1h) */
    P_STATUS_NVME_ABORT_COMMAND_LIMIT_EXCEEDED = 0x0103,    /* Abort Command Limit Exceeded */
    P_STATUS_NVME_ASYNCHRONOUS_EVENT_REQUEST_LIMIT_EXCEEDED = 0x0105,   /* Asynchronous Event Request Limit Exceeded */
    P_STATUS_NVME_INVALID_FIRMWARE_IMAGE = 0x0107,  /* Invalid Firmware Image */
    P_STATUS_NVME_FIRMWARE_REQUIRES_RESET = 0x010B, /* Firmware Application Requires Conventional Reset */

    /* Vendor Specific Errors (SCT = 7h) */
    /*==================================*/
    P_STATUS_NVME_AUTH_FAILED = ((NVME_STATUS_SCT_VENDOR_SPC << 8) | NVME_STATUS_SC_VENDOR_SPC_AUTH_FAILED),    /* Authentication 
                                                                                                                   failed 
                                                                                                                 */
    P_STATUS_NVME_BUSY = ((NVME_STATUS_SCT_VENDOR_SPC << 8) | NVME_STATUS_SC_VENDOR_SPC_BUSY),  /* System is busy, try
                                                                                                   later */
    P_STATUS_NVME_SUPER_CAP_NOT_EXIST = ((NVME_STATUS_SCT_VENDOR_SPC << 8) | NVME_STATUS_SC_VENDOR_SPC_SUPER_CAP_NOT_EXIST),    /* super 
                                                                                                                                   cap 
                                                                                                                                   doesn't 
                                                                                                                                   exist 
                                                                                                                                 */
    P_STATUS_NVME_FLASH_OPERATED = ((NVME_STATUS_SCT_VENDOR_SPC << 8) | NVME_STATUS_SC_VENDOR_SPC_FLASH_OPERATED),  /* flash 
                                                                                                                       is 
                                                                                                                       under 
                                                                                                                       the 
                                                                                                                       operation, 
                                                                                                                       try 
                                                                                                                       later 
                                                                                                                     */
    P_STATUS_NVME_BAD_PARAM = ((NVME_STATUS_SCT_VENDOR_SPC << 8) | NVME_STATUS_SC_VENDOR_SPC_BAD_PARAM),    /* bad
                                                                                                               parameter 
                                                                                                               value in 
                                                                                                               command */
    P_STATUS_NVME_UNSUPPORTED = ((NVME_STATUS_SCT_VENDOR_SPC << 8) | NVME_STATUS_SC_VENDOR_SPC_UNSUPPORTED),    /* The
                                                                                                                   operation 
                                                                                                                   is
                                                                                                                   unsupported 
                                                                                                                 */
    P_STATUS_NVME_NOT_PERMITTED = ((NVME_STATUS_SCT_VENDOR_SPC << 8) | NVME_STATUS_SC_VENDOR_SPC_NOT_PERMITTED),    /* The 
                                                                                                                       operation 
                                                                                                                       is 
                                                                                                                       not 
                                                                                                                       permitted 
                                                                                                                     */


    P_STATUS_NVME_BANK_IS_EMPTY = ((NVME_STATUS_SCT_VENDOR_SPC << 8) | NVME_STATUS_SC_VENDOR_SPC_BANK_IS_EMPTY), /* The
                                                                                                                    erase
                                                                                                                    failed,
                                                                                                                    bank is
                                                                                                                    already
                                                                                                                    empty */

    /* LIB level Errors */
    /*==================*/
    P_STATUS_LIB_GEN_ERROR = 0x0A01,    /* General error */
    P_STATUS_LIB_DRIVER_NOT_FOUND = 0x0A02, /* NVMe driver not installed */
    P_STATUS_LIB_CARD_NOT_FOUND = 0x0A03,   /* NVRAM card not found */
    P_STATUS_LIB_TIMEOUT = 0x0A04,  /* Timeout */
    P_STATUS_LIB_NOT_INITIALIZED = 0x0A05,  /* Not initialized yet */
    P_STATUS_LIB_BAD_PARAM = 0x0A06,    /* Bad parameter */
    P_STATUS_LIB_TOO_BIG = 0x0A07,  /* Too big */
    P_STATUS_LIB_UNSUPPORTED = 0x0A08,  /* Unsupported */
    P_STATUS_LIB_NOT_EXIST = 0x0A09,    /* Not exist */
    P_STATUS_LIB_ALREADY_EXIST = 0x0A0A,    /* Already exist */
    P_STATUS_LIB_FULL = 0x0A0B, /* Full */
    P_STATUS_LIB_EMPTY = 0x0A0C,    /* Empty */
    P_STATUS_LIB_NO_MEMORY = 0x0A0D,    /* No memory */
    P_STATUS_LIB_ENC_KEY_UNDEFINED = 0x0A0E,    /* The encryption key is not defined */
    P_STATUS_LIB_PMC_MODULE_NOT_FOUND = 0x0A0F, /* PMC module not installed */
    P_STATUS_LIB_RESTORE_EMPTY = 0x0A10,    /* Restore Error - flash is empty */
    P_STATUS_LIB_RESTORE_NOT_SAVED = 0x0A11,    /* Restore Error - image is not saved */
    P_STATUS_LIB_EXCEED_MAX_BAD_BLOCK = 0x0A12, /* Error in exceeding maximum allowed bad blocks */
    P_STATUS_LIB_BACKUP_ABORTED = 0x0A13,   /* Backup error - aborted */
    P_STATUS_LIB_BACKUP_NO_MORE_BLOCKS = 0x0A14,    /* Backup error - no more blocks to do backup */
    P_STATUS_LIB_FW_VER_NOT_COMPATIBLE = 0x0A15,    /* FW and API versions are not compatible */

    /* Host Operating System Errors */
    /*==============================*/
    P_STATUS_HOST_OS_ERROR = 0x0B01 /* Host Operating System Error - Note: For this error, detailed info can be
                                       retrieved using "errno" variable */
} P_STATUS;


/* 
   Name: NVRAM_operation_mode_e
   Description: The NVRAM operation mode. */
typedef enum
{
    NVRAM_OPERATION_MODE_AUTO   = 0,    /* The operation is handled by NVRAM lib automatically */
    NVRAM_OPERATION_MODE_USER   = 1     /* The operation is handled by the user */

} NVRAM_operation_mode_e;


/* 
   Name: NVRAM_system_config_type_e
   Description: NVRAM system global configuration type to set or get. */
typedef enum
{
    NVRAM_SYSTEM_CONFIG_TYPE_FIRST = 0,
    NVRAM_SYSTEM_CONFIG_TYPE_THREAD_MODE = 0,    /* Threads operation mode. NOTE: this mode must be set before any other API call. */
    NVRAM_SYSTEM_CONFIG_TYPE_SEMAPHORE_MODE = 1, /* Semaphores operation mode. NOTE: this mode must be set before any other API call.*/
    NVRAM_SYSTEM_CONFIG_TYPE_NUM

} NVRAM_system_config_type_e;


/* 
   Name: NVRAM_auto_backup_mode_e Description: The mode of NVRAM auto backup in case of power loss/heartbeat
   loss/temperature threshold. */
typedef enum
{
    AUTO_BACKUP_MODE_DISABLE = 0,   /* Disable backup on power loss */
    AUTO_BACKUP_MODE_ENABLE = 1 /* Enable backup on power loss */
} NVRAM_auto_backup_mode_e;

/* 
   Name: NVRAM_auto_restore_mode_e Description: The Auto restoration mode of NVRAM Image. */
typedef enum
{
    AUTO_RESTORE_MODE_DISABLE = 0,
    AUTO_RESTORE_FROM_BACKUP = 1,
    AUTO_RESTORE_FROM_BANK = 2
} NVRAM_auto_restore_mode_e;



/* 
   Name: NVRAM_fw_slot_e Description: Slot of the NVRAM FW image. */
typedef enum
{
    NVRAM_FW_SLOT_0 = 0,        /* SLOT #0 (according to NVME spec) */
    NVRAM_FW_SLOT_1 = 1,        /* SLOT #1 */
    NVRAM_FW_SLOT_2 = 2,        /* SLOT #2 */
    NVRAM_FW_SLOT_3 = 3,        /* SLOT #3 */

} NVRAM_fw_slot_e;


/* 
   Name: NVRAM_fw_slot_e Description: fw activate action of the NVRAM FW image. */
typedef enum
{
    NVRAM_FW_ACTION_STORE = 0,  /* Store FW only */
    NVRAM_FW_ACTION_STORE_AND_ACTIVATE = 1, /* Store and actvate FW */
    NVRAM_FW_ACTION_ACTIVATE = 2    /* Activate FW only */
} NVRAM_fw_activate_action_e;



/* 
   Name: NVRAM_heartbeat_command_e Description: The heartbeat command type. */
typedef enum
{
    HEARTBEAT_COMMAND_START = 0,
    HEARTBEAT_COMMAND_STOP = 1,
    HEARTBEAT_COMMAND_ABORT = 2
} NVRAM_heartbeat_command_e;

/* 
   Name: NVRAM_fw_source_e Description: Source of the NVRAM FW image. */
typedef enum
{
    NVRAM_FW_SOURCE_FILE = 0,   /* The FW image will be taken from a file */
    NVRAM_FW_SOURCE_MEMORY = 1, /* The FW image will be taken from the memory */

} NVRAM_fw_source_e;


/* 
   Name: NVRAM_info_group_e Description: NVRAM information group. */
typedef enum
{
    NVRAM_INFO_GROUP_FIRST = 0, /* First group ID. Must be 0 */
    NVRAM_INFO_GROUP_GENERAL = NVRAM_INFO_GROUP_FIRST,  /* NVRAM General info group (to use with
                                                           NVRAM_info_data_u.general) */
    NVRAM_INFO_GROUP_DMI = 1,   /* NVRAM DMI info group (to use with NVRAM_info_data_u.dmi) */
    NVRAM_INFO_GROUP_RAMDISK = 2,   /* NVRAM RAM disk info group (to use with NVRAM_info_data_u.ramdisk) */
    NVRAM_INFO_GROUP_FLASH = 3, /* NVRAM flash info group (to use with NVRAM_info_data_u.flash) */
    NVRAM_INFO_GROUP_TEMPERATURE = 4,   /* NVRAM temprature sensors info group (to use with
                                           NVRAM_info_data_u.temperature) */
    NVRAM_INFO_GROUP_DDR = 5,   /* NVRAM ddr info group (to use with NVRAM_info_data_u.ddr) */
    NVRAM_INFO_GROUP_BACKUP_POWER = 6,  /* NVRAM power info group (to use with NVRAM_info_data_u.backup_power) */
    NVRAM_INFO_GROUP_PCIE = 7,  /* NVRAM pcie info group (to use with NVRAM_info_data_u.pcie) */
    NVRAM_INFO_GROUP_DEBUG = 8, /* NVRAM debug info group (to use with NVRAM_info_data_u.debug) */
    NVRAM_INFO_GROUP_NUMBER     /* Total number of groups. Must be last in enumeration */
} NVRAM_info_group_e;




/* 
   Name: NVRAM_status_group_e Description: NVRAM status groups. */
typedef enum
{
    NVRAM_STATUS_GROUP_FIRST = 0,   /* First group ID. Must be 0 */
    NVRAM_STATUS_GROUP_GENERAL = NVRAM_STATUS_GROUP_FIRST,  /* NVRAM General status group (to use with
                                                               NVRAM_status_data_u.general) */
    NVRAM_STATUS_GROUP_CPU = 1, /* NVRAM CPU ststus group (to use with NVRAM_status_data_u.cpu) */
    NVRAM_STATUS_GROUP_SYSTEM = 2,  /* NVRAM system ststus group (to use with NVRAM_status_data_u.system) */
    NVRAM_STATUS_GROUP_DEBUG = 3,   /* NVRAM firmware status group (to use with NVRAM_status_data_u.debug) */
    NVRAM_STATUS_GROUP_FLASH = 4,   /* NVRAM flash flash group (to use with NVRAM_status_data_u.flash) */
    NVRAM_STATUS_GROUP_BACKUP_POWER = 5,    /* NVRAM backup power status group (to use with
                                               NVRAM_status_data_u.backup_power) */
    NVRAM_STATUS_GROUP_PCIE = 6,    /* NVRAM pcie statistics status group (to use with NVRAM_status_data_u.pcie) */

    NVRAM_STATUS_GROUP_NUMBER   /* Total number of groups. Must be last in enumeration */
} NVRAM_status_group_e;



/* 
   Name: NVRAM_fw_image_state_e Description: State of the NVRAM image. */
typedef enum
{
    NVRAM_FW_IMAGE_STATE_NONE = 0,  /* There is no FW image */
    NVRAM_FW_IMAGE_STATE_EXIST = 1, /* FW image exists */
    NVRAM_FW_IMAGE_STATE_CORRUPTED = 2, /* FW image exists but corrupted */
    NVRAM_FW_IMAGE_STATE_DURING_DOWNLOAD = 3    /* FW image is during download */
} NVRAM_fw_image_state_e;


/* 
   Name: NVRAM_flash_bank_id_e Description: IDs of the banks which used to store RAM copies on the NVRAM flash. */
typedef enum
{
    NVRAM_FLASH_BANK_0 = 0,     /* NVRAM flash bank #0 */
    NVRAM_FLASH_BANK_1 = 1,     /* NVRAM flash bank #1 */

    NVRAM_FLASH_BANK_NUM        /* Number of flash banks */
} NVRAM_flash_bank_id_e;


/* 
   Name: NVRAM_config_type_e Description: NVRAM configuration type to set or get. */
typedef enum
{
    NVRAM_CONFIG_TYPE_FIRST = 0,
    NVRAM_CONFIG_TYPE_ACTIVE_FW_INDEX = 0,  /* The active FW index */
    NVRAM_CONFIG_TYPE_ENABLE_BIST = 1,  /* Enable/Disable BIST (Built-In Self-Test) - TBD ??? */
    NVRAM_CONFIG_TYPE_AUTHENTICATION_KEY_MASTER = 2,    /* Master authentication key */
    NVRAM_CONFIG_TYPE_AUTHENTICATION_KEY_ADMIN = 3, /* Admin authentication key */
    NVRAM_CONFIG_TYPE_RESERVED_1 = 4,   /* Reserved 1 */
    NVRAM_CONFIG_TYPE_RESERVED_2 = 5,    /* Reserved 2 */
    NVRAM_CONFIG_TYPE_CPU_TEMPERATURE_THRESHOLD = 6,    /* CPU temperature threshold which trigger "temperature" alarm
                                                           when crossed */
    NVRAM_CONFIG_TYPE_CPU_CRITICAL_TEMPERATURE_THRESHOLD = 7,   /* CPU critical temperature threshold which trigger
                                                                   "temperature" alarm when crossed */
    NVRAM_CONFIG_TYPE_SYSTEM_TEMPERATURE_THRESHOLD = 8, /* System temperature threshold which trigger "temperature"
                                                           alarm when crossed */
    NVRAM_CONFIG_TYPE_SYSTEM_CRITICAL_TEMPERATURE_THRESHOLD = 9,    /* System criticaltemperature threshold which
                                                                       trigger "temperature" alarm when crossed */
    NVRAM_CONFIG_TYPE_BACKUP_POWER_TEMPERATURE_THRESHOLD = 10,  /* Backup Power temperature threshold which trigger
                                                                   "temperature" alarm when crossed */
    NVRAM_CONFIG_TYPE_BACKUP_POWER_CRITICAL_TEMPERATURE_THRESHOLD = 11, /* Backup Power temperature threshold which
                                                                           trigger "temperature" alarm when crossed */
    NVRAM_CONFIG_TYPE_BACKUP_POWER_CHARGE_LEVEL_THRESHOLD = 12, /* Backup Power charge level threshold which trigger
                                                                   "backup power" alarm when crossed */
    NVRAM_CONFIG_TYPE_EVENTS_POLLING_INTERVAL = 13, /* Events polling interval */
    NVRAM_CONFIG_TYPE_AUTO_RESTORE_BANK = 14,   /* Default bank to load restore data */
    NVRAM_CONFIG_TYPE_AUTO_RESTORE_MODE = 15,   /* NVRAM auto restore mode */
    NVRAM_CONFIG_TYPE_HEARTBEAT_MSG_INTERVAL = 16,  /* Set heartbeat message interval */
    NVRAM_CONFIG_TYPE_HEARTBEAT_MSG_NUMBER = 17,    /* Set heartbeat number of lost hearbeats that indiace a nessesity
                                                       of vaulting */
    NVRAM_CONFIG_TYPE_AUTO_BACKUP_MODE_WHEN_POWER_LOST = 18,    /* Enable/Disable NVRAM auto backup */
    NVRAM_CONFIG_TYPE_AUTO_BACKUP_BANK_WHEN_POWER_LOST = 19,    /* ID of the flash bank which will be used to store the 
                                                                   RAM backup on the next power down */
    NVRAM_CONFIG_TYPE_AUTO_BACKUP_MODE_WHEN_HEARTBEAT_LOST = 20,    /* NVRAM heartbeat loss backup mode */
    NVRAM_CONFIG_TYPE_AUTO_BACKUP_BANK_WHEN_HEARTBEAT_LOST = 21,    /* ID of the flash bank which will be used to store 
                                                                       the RAM backup on the heartbeat loss */
    NVRAM_CONFIG_TYPE_AUTO_BACKUP_MODE_WHEN_CPU_OVER_TEMPERATURE = 22,  /* Enable/Disable auto backup when CPU over
                                                                           temperature */
    NVRAM_CONFIG_TYPE_AUTO_BACKUP_BANK_WHEN_CPU_OVER_TEMPERATURE = 23,  /* Auto backup bank ID when CPU over
                                                                           temperature */
    NVRAM_CONFIG_TYPE_AUTO_BACKUP_MODE_WHEN_SYSTEM_OVER_TEMPERATURE = 24,   /* Enable/Disable auto backup when System
                                                                               over temperature */
    NVRAM_CONFIG_TYPE_AUTO_BACKUP_BANK_WHEN_SYSTEM_OVER_TEMPERATURE = 25,   /* Auto backup bank ID when System over
                                                                               temperature */
    NVRAM_CONFIG_TYPE_AUTO_BACKUP_MODE_WHEN_BACKUP_POWER_OVER_TEMPERATURE = 26, /* Enable/Disable auto backup when
                                                                                   Backup Power over temperature */
    NVRAM_CONFIG_TYPE_AUTO_BACKUP_BANK_WHEN_BACKUP_POWER_OVER_TEMPERATURE = 27, /* Auto backup bank ID when Backup
                                                                                   Power over temperature */
    NVRAM_CONFIG_TYPE_ENABLE_RAMDISK_ENCRYPTION = 28,   /* Enable/Disable RAM drive encryption */
    NVRAM_CONFIG_TYPE_ENABLE_RAMDISK_DMI_OVERLAP = 29,  /* Enable/Disable RAM drive and DMI overlap */
    NVRAM_CONFIG_TYPE_DMI_SIZE = 30,    /* DMI size (bytes) */
    NVRAM_CONFIG_TYPE_RAMDISK_SIZE = 31,    /* RAM drive size (bytes) */
    NVRAM_CONFIG_TYPE_BAD_BLOCK_SCAN_USE_VBBS = 32, /* Use VBBS table during bad block scan */
    NVRAM_CONFIG_TYPE_ENABLE_DEBUG = 33,    /* Enable/disable Debug functions - can be set only in admin mode */

    NVRAM_CONFIG_TYPE_NUM
} NVRAM_config_type_e;

/* 
   Name: NVRAM_device_state_e Description: Status of device. */
typedef enum
{
    NVRAM_DEVICE_STATE_UNINITIALIZED = 0,   /* Device is not initialized */
    NVRAM_DEVICE_STATE_INITIALIZED = 1, /* Device is initialized */
    NVRAM_DEVICE_STATE_OPERATIONAL = 2, /* Device is in operational state */
    NVRAM_DEVICE_STATE_BACKUP = 3,  /* Device is in backup state */
    NVRAM_DEVICE_STATE_RESTORE = 4, /* Device is in restore state */
    NVRAM_DEVICE_STATE_ERASE = 5,   /* Device is in eraze state */

} NVRAM_device_state_e;


/*
Name:        NVRAM_device_health_e
Description: Health of device.
*/
typedef enum 
{ 
    NVRAM_DEVICE_HEALTH_OK                       = 0,         /* Device health is OK */
    NVRAM_BIT0_DEVICE_HEALTH_RESTORE_UC_ERROR    = (1<<0),         /* Bit0 -  Device health is Restore UC Error */
    NVRAM_BIT1_DEVICE_HEALTH_PROCESSING_ERROR    = (1<<1),    /* Bit1 -  Device health is Processing Error */
    NVRAM_BIT2_DEVICE_HEALTH_GENERAL_ERROR       = (1<<2),    /* Bit2 -  Device health is General error */
    NVRAM_BIT3_FIRST_RIC_OUT_OF_ORDER            = (1<<3),    /* Bit3 -  1st RIC is Out Of Order */
    NVRAM_BIT4_SECOND_RIC_OUT_OF_ORDER            = (1<<4),     /* Bit4 -  2nd RIC is Out Of Order */
    NVRAM_BIT5_SUPERCAP_HAD_OVER_VOLTAGE = (1<<5) /* Bit5 - SuperCap had passed Over Voltage */
} NVRAM_device_health_e;


/*
 * MASK of device health
 */
#define NVRAM_DEVICE_HEALTH_MASK (NVRAM_BIT0_DEVICE_HEALTH_RESTORE_UC_ERROR | \
                                  NVRAM_BIT1_DEVICE_HEALTH_PROCESSING_ERROR | \
                                  NVRAM_BIT2_DEVICE_HEALTH_GENERAL_ERROR | \
                                  NVRAM_BIT5_SUPERCAP_HAD_OVER_VOLTAGE)

/*
 * MASK of RIC out of order
 */
#define NVRAM_RIC_OUT_OF_ORDER_MASK (NVRAM_BIT3_FIRST_RIC_OUT_OF_ORDER | NVRAM_BIT4_SECOND_RIC_OUT_OF_ORDER)

/*
Name:            NVRAM_backup_reason_e
Description:    Reason of a RAM backup.
*/
typedef enum 
{ 
    NVRAM_BACKUP_REASON_POWER_LOST                        = 0,    /* RAM backup due to power lost                        */
    NVRAM_BACKUP_REASON_HOST_SHUTDOWN                    = 1,    /* RAM backup due to host shutdown                    */
    NVRAM_BACKUP_REASON_USER_REQUEST                    = 2,    /* RAM backup due to user request via Backup API    */
    NVRAM_BACKUP_REASON_HEARTBEAT_LOSS                    = 3,    /* RAM backup due to loss of HOST heart beats    */
    NVRAM_BACKUP_REASON_CPU_OVER_TEMPERATURE            = 4,    /* RAM backup due to CPU temperature over treschold    */
    NVRAM_BACKUP_REASON_SYSTEM_OVER_TEMPERATURE            = 5,    /* RAM backup due to System temperature over treschold    */
    NVRAM_BACKUP_REASON_BACKUP_POWER_OVER_TEMPERATURE    = 6        /* RAM backup due to CPU temperature over treschold    */

} NVRAM_backup_reason_e;


/* 
   Name: NVRAM_event_type_e Description: NVRAM event type. */
typedef enum
{
    NVRAM_EVENT_TYPE_FIRST = 0, /* First event type. Must be 0 */
    NVRAM_EVENT_TYPE_VAULT = NVRAM_EVENT_TYPE_FIRST,    /* Event which sent out when vault operation (backup, restore,
                                                           erase) has finished */
    NVRAM_EVENT_TYPE_CPU = 1,   /* Event which sent out when CPU event occurred */
    NVRAM_EVENT_TYPE_SYSTEM = 2,    /* Event which sent out when system event occurred */
    NVRAM_EVENT_TYPE_BACKUP_POWER = 3,  /* Event which sent out when backup power component event occurred */
    NVRAM_EVENT_TYPE_DDR_ECC = 4,   /* Event which sent out when DDR ECC occurred */
    NVRAM_EVENT_TYPE_GENERAL = 5,   /* Event which sent out when general event occurred */
    NVRAM_EVENT_TYPE_DEV_HEALTH        = 6,                    /* Event which sent out when device health is damaged                                    */
    NVRAM_EVENT_TYPE_NUMBER     /* Total number of events. Must be last in enumeration */
} NVRAM_event_type_e;


/* 
   Name: NVRAM_flash_erase_type_e Description: Type of the NVRAM flash erase. */
typedef enum
{
    NVRAM_FLASH_ERASE_TYPE_STANDARD = 0,    /* Erase the flash */
    NVRAM_FLASH_ERASE_TYPE_SECURE = 1   /* Set all flash cells to 0x0 and then erase the flash */
} NVRAM_flash_erase_type_e;


/* 
   Name: NVRAM_flash_bank_state_e Description: State of the NVRAM flash bank. */
typedef enum
{
    NVRAM_FLASH_BANK_STATE_EMPTY = 0,   /* The flash bank is empty */
    NVRAM_FLASH_BANK_STATE_SAVED = 1,   /* The flash bank accommodate RAM image */
    NVRAM_FLASH_BANK_STATE_CORRUPTED = 2,   /* The flash bank data is corrupted */
    NVRAM_FLASH_BANK_STATE_DURING_BACKUP = 3,   /* The flash bank data is under creating of a backup image */
    NVRAM_FLASH_BANK_STATE_DURING_RESTORE = 4   /* The flash bank data is under restoring of a backup image */
} NVRAM_flash_bank_state_e;


/* 
   Name: NVRAM_statistics_group_e Description: NVRAM statistics group. */
typedef enum
{
    NVRAM_STATISTICS_GROUP_START = 0,   /* TBD */
    NVRAM_STATISTICS_GROUP_FLASH = NVRAM_STATISTICS_GROUP_START,    /* TBD */
    NVRAM_STATISTICS_GROUP_DDR = 1, /* TBD */
    NVRAM_STATISTICS_GROUP_GENERAL = 2, /* TBD */
    NVRAM_STATISTICS_GROUP_NUMBER
} NVRAM_statistics_group_e;

/* 
   Name: NVRAM_flash_type_e Description: Flash type. */
typedef enum
{
    NVRAM_FLASH_TYPE_MT29F128G08 = 0,
    NVRAM_FLASH_TYPE_K9GCGD8 = 1,
    NVRAM_FLASH_TYPE_MT29F256G08 = 2,
    NVRAM_FLASH_TYPE_TH58TEG8 = 3,
    NVRAM_FLASH_TYPE_MT29F32G08 = 4,
    NVRAM_FLASH_TYPE_MT29F128G08_MLC_TYPE_S = 5,
} NVRAM_flash_type_e;

/* 
   Name: NVRAM_pcie_lanes_e Description: Number of lanes in PCIe. */
typedef enum
{
    LANES_X4 = 4,
    LANES_X8 = 8
} NVRAM_pcie_lanes_e;

/* 
   Name: NVRAM_pcie_generation_e Description: TODO: PCI generation. */
typedef enum
{
    PCIE_GENERATION_1 = 1,
    PCIE_GENERATION_2 = 2,
    PCIE_GENERATION_3 = 3
} NVRAM_pcie_generation_e;


/* 
   Name: NVRAM_backup_power_src_e Description: Power source. */
typedef enum
{
    BACKUP_POWER_SRC_NONE = 0,
    BACKUP_POWER_SRC_SUPERCAP = 1,
    BACKUP_POWER_SRC_AUX = 2
} NVRAM_backup_power_src_e;


/* 
   Name: NVRAM_backup_power_learning_status_e Description: NVRAM Backup Power Learning Status */
typedef enum
{
    BACKUP_POWER_LEARNING_STATUS_RUNNING = 0,
    BACKUP_POWER_LEARNING_STATUS_SUCCESS = 1,
    BACKUP_POWER_LEARNING_STATUS_FAILED = 2,
    BACKUP_POWER_LEARNING_STATUS_UNDEFINED = 3
} NVRAM_backup_power_learning_status_e;


/* */
/* 
   Name: NVRAM_health_state_e Description: NVRAM Backup Power health state */
typedef enum
{
    NVRAM_HEALTH_STATE_WARN,    /* The health state is in warning state */
    NVRAM_HEALTH_STATE_LOW,     /* The health state is too low */
    NVRAM_HEALTH_STATE_FAIL,    /* The health state is failed */
    NVRAM_HEALTH_STATE_NORMAL   /* The health state is normal */
} NVRAM_health_state_e;


/* 
   Name: NVRAM_log_page_id_e Description: Log page ID. */
typedef enum
{
    LOG_PAGE_PROC_34 = 0,
    LOG_PAGE_PROC_12 = 1,
    LOG_PAGE_PROC_14 = 2,
    LOG_PAGE_PROC_15 = 3,
    LOG_PAGE_PROC_21 = 4,
    LOG_PAGE_PROC_22 = 5,
    LOG_PAGE_PROC_24 = 6,
    LOG_PAGE_PROC_25 = 7,
    LOG_PAGE_PROC_33 = 8,
    LOG_PAGE_PROC_11 = 9,
    LOG_PAGE_PROC_35 = 10,
    LOG_PAGE_PROC_36 = 11,
    LOG_PAGE_PROC_43 = 12,
    LOG_PAGE_PROC_44 = 13,
    LOG_PAGE_PROC_45 = 14,
    LOG_PAGE_PROC_46 = 15,
    LOG_PAGE_STAT_HISTORY = 16,

    LOG_PAGE_PROC_LAST
} NVRAM_log_page_id_e;


#ifdef PLATFORM_HOST            /* Host */

/* 
   Name: NVRAM_log_from_backup_e Description: Indicates whether the log page should be retrieved from backup or not. */
typedef enum
{
    LOG_PAGE_NORMAL = 0,
    LOG_PAGE_BACKED_UP = 1,
} NVRAM_log_page_from_backup_e;

#endif /* PLATFORM_HOST */

/* 
   Name: NVRAM_restore_status_e Description: Status of the last DDR restore operation. */
typedef enum
{
    NVRAM_RESTORE_STATUS_NOT_AVAILABLE = 0, /* The restore is never performed */
    NVRAM_RESTORE_STATUS_DONE = 1,  /* Last restore operation was succeeded */
    NVRAM_RESTORE_STATUS_FAILED = 2 /* Last restore operation was failed */
} NVRAM_restore_status_e;


/* 
   Name: NVRAM_power_lost_backup_start_status_e Description: Last power lost backup start status. */
typedef enum
{
    NVRAM_POWER_LOST_NOT_OCCURRED = 0,  /* Power lost not occurred */
    NVRAM_POWER_LOST_BACKUP_STARTED = 1,    /* Power lost backup was started */
    NVRAM_POWER_LOST_BACKUP_NOT_STARTED_NOT_CONFIGURED = 2, /* Power lost backup was not started since was not
                                                               configured by
                                                               PMC_NVRAM_config_set(NVRAM_CONFIG_TYPE_AUTO_BACKUP_MODE_WHEN_POWER_LOST) 
                                                             */
    NVRAM_POWER_LOST_BACKUP_NOT_STARTED_NOT_FULLY_CHARGED = 3,  /* Power lost backup was not started since backup power 
                                                                   was not fully charged */
    NVRAM_POWER_LOST_BACKUP_NOT_STARTED_NO_POWER = 4,   /* Power lost backup was not started since backup power was too 
                                                           low */
    NVRAM_POWER_LOST_BACKUP_NOT_STARTED_RECEIVED_DURING_RESTORE = 5 /* Power lost backup was not started since it was
                                                                       received during restore operation */
} NVRAM_power_lost_backup_start_status_e;

/* 
   Name: NVRAM_fw_fail_test_op_id_e Description: The operation ID of FW crash test. */
typedef enum
{
    FW_FAIL_TEST_DIV0 = 0,
    FW_FAIL_TEST_HANG = 1,
    FW_FAIL_TEST_DIV0_NEXT_INIT = 2,
    FW_FAIL_TEST_HANG_NEXT_INIT = 3,
} NVRAM_fw_fail_test_op_id_e;


/*
Name:            NVRAM_debug_config_type_e
Description:    NVRAM debug configuration type to set or get.
*/
typedef enum 
{ 
    NVRAM_DEBUG_CONFIG_TYPE_FIRST                               = 0,
    NVRAM_DEBUG_CONFIG_TYPE_IGNORE_BAD_BLOCK_THRESHOLD          = NVRAM_DEBUG_CONFIG_TYPE_FIRST,    /* Ignore bad block threshold */

    NVRAM_DEBUG_CONFIG_TYPE_NUM

} NVRAM_debug_config_type_e;


/* 
   Name: NVRAM_card_s Description: NVRAM card data. */
typedef struct
{
    char card_uid[NVRAM_CARD_UID_SIZE]; /* A unique identifier of the NVRAM card */
    uint32_t nvme_index;        /* The nvme index, 0 for /dev/nvme0, 1 for /dev/nvme1, etc... */

} NVRAM_card_s;


/* 
   Name: NVRAM_system_config_data_u
   Description: NVRAM system global configuration data which can be configured in NVRAM system. */
typedef union
{
    NVRAM_operation_mode_e      thread_mode;        /* Threads operation mode. NOTE: this mode must be set before any other API call. */
    NVRAM_operation_mode_e      semaphore_mode;     /* Semaphores operation mode. NOTE: this mode must be set before any other API call. */

} NVRAM_system_config_data_u;


/* 
   Name: NVRAM_version_s Description: General version number. */
typedef char NVRAM_version_s[NVRAM_VERSION_SIZE + 1];



/* 
   Name: NVRAM_fw_info_s Description: FW general information. */
typedef struct
{
    NVRAM_version_s version;    /* FW version */
    NVRAM_fw_image_state_e state;   /* FW image state */

} NVRAM_fw_info_s;


/* 
   Name: NVRAM_battery_state_s Description: The Battery state. */
typedef struct
{
    uint8_t battery_percentage; /* Battery percentage (0 - 100 percent) */
    p_bool_t battery_too_low;   /* Is battery percentage is below the defined threshold */
    /* (TRUE - Battery too low. FALSE - Battery is ready) */

} NVRAM_battery_state_s;


/* 
   Name: NVRAM_ramdisk_enc_key_s Description: The RAM disk encryption key. */
typedef struct 
{
    uint32_t    index;                                /* RAMDISK Encryption key index - NOT IN USE */
    uint8_t        key1[NVRAM_RAMDISK_ENC_KEY_SIZE];    /* RAMDISK Encryption key1 value */
    uint8_t        key2[NVRAM_RAMDISK_ENC_KEY_SIZE];    /* RAMDISK Encryption key2 value */

} NVRAM_ramdisk_enc_key_s;


/*
 * Name: NVRAM_actual_geometry 
 * Description: The actual bank geometry.
 * used in debug ioctl of get frim mapping.
 */
typedef struct
{
    uint32_t actual_channels;
    uint32_t actual_targets;
    uint32_t actual_luns;
    uint32_t actual_blocks; /* per bank */
    uint64_t bank_address[NVRAM_FLASH_BANK_NUM];
} NVRAM_actual_geometry;


/* 
   Name: NVRAM_info_data_u Description: NVRAM information data which can be retrieved from NVRAM system. */
typedef union
{
    struct
    {
        char card_uid[NVRAM_CARD_UID_SIZE]; /* A unique identifier of the NVRAM card */
        NVRAM_version_s hw_version; /* The version number of the HW */
        NVRAM_version_s driver_version; /* The version number of the NVRAM driver */
        NVRAM_version_s pmc_module_version; /* The version number of PMC NVRAM module */
        NVRAM_version_s api_version;    /* The version number of this API */
        NVRAM_fw_info_s fw1_info;   /* Information regarding FW with index 1 */
        NVRAM_fw_info_s fw2_info;   /* Information regarding FW with index 2 */
        NVRAM_fw_info_s fw3_info;   /* Information regarding FW with index 3 */
        uint8_t num_of_banks;   /* Total number of flash banks */
        uint16_t pci_vendor_id; /* The Manufacturer PCI ID */
        uint16_t pci_device_id; /* The Device ID */
        NVRAM_version_s controller_version; /* Controller version */
        uint16_t cpld_version;  /* The CPLD FW version */
        NVRAM_version_s sbl_version;    /* SBL version */
    } general;

    struct
    {
        uint16_t num_of_ranks;  /* The amount of chip selects */
        uint16_t width;         /* DDR bus Width */
        uint16_t speed;         /* What is the DDR Operation Frequency */
        uint16_t ecc_type;      /* Type of ECC algorithm */
        uint64_t size;          /* Total DDR memory on the board */
        uint64_t max_user_data_size;    /* Maximal DDR size available for user data */
        p_bool_t ramdisk_dmi_overlap_enabled;   /* Whether ramdisk and DMI overlapped */
    } ddr;

    struct
    {
        uint64_t size;          /* Flash size */
        NVRAM_flash_type_e type;
        uint16_t num_of_devices;
        uint16_t bus_speed;
        uint32_t num_channels;
        uint32_t page_size;
        uint32_t block_size;    /* Minimum Erasing size */
        uint32_t num_of_targets_per_dev;    /* How many targets are in each device */
        uint32_t num_of_luns_per_target;    /* How many LUNs in each target */
        uint32_t num_of_planes_per_lun; /* How many planes in each LUN */
        uint32_t num_of_pages_per_block;    /* How many pages in each block */
        uint16_t ecc_type;      /* Type of ECC algorithm */
    } flash;

    struct
    {
        NVRAM_backup_power_src_e    power_src;                        /* Backup power source (None/Supercap/Auxiliary)    */    
        p_bool_t                    removable_unit;                    /* Whether backup power unit is removable    */
        uint16_t                    bq_flash_version;               /* The version number of the BQ internal flash */
    } backup_power;

    struct
    {
        p_bool_t cpu_sensor_calib_supported;    /* Whether the CPU temperature sensor can be calibrated */
        p_bool_t system_sensor_calib_supported; /* Whether the system temperature sensor can be calibrated */
    } temperature;

    struct
    {
        NVRAM_pcie_lanes_e bus_width;
        NVRAM_pcie_generation_e generation;
    } pcie;

    struct
    {
        uint32_t watchdog_interval; /* Max Time between watchdog timer reset */
    } debug;

    struct
    {
        uint64_t physical_addres;   /* First DMI address */
        uint64_t offset;        /* DMI offset */
        uint64_t size;          /* DMI size */
    } dmi;

    struct
    {
        p_bool_t is_exist;      /* Is RAM disk exist */
        uint64_t size;          /* RAM disk size */
    } ramdisk;

} NVRAM_info_data_u;


/* 
   Name: NVRAM_status_data_u 
   Description: NVRAM device statuses which can be retrieved from NVRAM system. 
*/
typedef union
{
    struct
    {
        NVRAM_device_state_e device_state;
        uint32_t time_of_day;               /* time of day in units of seconds */
        uint32_t bist_errors;               /* error bitmap result from BIST */
        uint32_t device_health_bitmask;        /* health bitmask  */
    } general;

    struct
    {
        uint16_t temperature;   /* CPU temperature */
        uint16_t min_lifetime_temperature;  /* CPU min temperature which was ever detected */
        uint16_t max_lifetime_temperature;  /* CPU max temperature which was ever detected */
    } cpu;

    struct
    {
        uint16_t temperature;   /* system temperature */
        uint16_t min_lifetime_temperature;  /* system min temperature which was ever detected */
        uint16_t max_lifetime_temperature;  /* system max temperature which was ever detected */
    } system;

    struct
    {
        uint32_t watchdog_timer;    /* How many time left till timer expires (in cycles) */
    } debug;

    struct
    {
        uint32_t nvram_bad_blocks;
        uint32_t ctrl_bad_blocks;
    } flash;

    struct
    {
        uint16_t temperature;   /* Backup power temperature */
        uint16_t charge_level;  /* The charge level of the backup power in % */
        uint16_t health;        /* The health of the backup power component in %, based on the present capacitance in
                                   comparison to the initial capacitance. */
        NVRAM_health_state_e health_state;  /* The health state of the backup power component as defined in
                                               NVRAM_health_state_e. */
        uint16_t end_of_life_time;  /* The backup power component end of life time estimation. */
        uint16_t reserved1;       /* Reserved field */
        uint16_t max_lifetime_temperature;  /* Max temperature which was ever detected in the backup power component */
        uint16_t voltage;       /* Current voltage on the backup power component */
        uint16_t voltage_cap1;  /* Current voltage on the backup power's capacitor #1 */
        uint16_t voltage_cap2;  /* Current voltage on the backup power's capacitor #2 */
        uint16_t charging_voltage;  /* Current charging voltage on the backup power component */
        uint16_t max_charging_voltage;  /* Max charging voltage of the backup power component */
        uint16_t reserved2;       /* Reserved field */
        uint16_t current;       /* Present current supplied by the backup power component. */
        uint16_t charging_current;  /* Present charging current on the backup power component. */
        uint16_t capacitance;   /* Present capacitance of the backup power component */
        uint16_t esr;           /* Present ESR (Equivalent Series Resistance) of the backup power component) */
        p_bool_t fully_charged; /* Indicates whether the backup power component is fully charged. */
        NVRAM_backup_power_learning_status_e learning_status;   /* The backup power internal learning status. */
        p_bool_t voltage_imbalance; /* Indicates whether the backup power voltage spread among capacitors is imbalance */
        p_bool_t over_temperature;  /* Indicates whether the backup power temperature is too high. */
        p_bool_t over_critical_temperature; /* Indicates whether the backup power critical temperature is too high. */
        p_bool_t over_voltage;  /* Indicates whether the backup power voltage is too high. */
        p_bool_t over_current;  /* Indicates whether the backup power current is too high. */
        p_bool_t short_circuit; /* Indicates whether the backup power has short circuit. */

    } backup_power;

    struct
    {
        NVRAM_pcie_generation_e cur_bus_generation; /* Current bus generation */
    } pcie;


} NVRAM_status_data_u;


/* 
   Name: NVRAM_config_data_u Description: NVRAM configuration data which can be configured in NVRAM system. */
typedef union
{
    NVRAM_fw_slot_e active_fw_index;    /* The index of the FW to activate on the next controller reboot */
    p_bool_t enable_bist;       /* Enable RAM BIST (Built-In Self-Test) - TBD ??? */
    char auth_key[NVRAM_AUTH_KEY_SIZE]; /* Authentication key. NOTE: This field cannot be retrieved by NVRAM Config Get 
                                           API */
    uint16_t backup_power_charge_level_threshold;   /* Backup power percentage threshold which triggers "backup power
                                                       charge level" event when crossed, 0-100 percent */
    uint16_t cpu_temperature_threshold; /* Temperature threshold which triggers "CPU temperature" event when crossed,
                                           Kelvin degrees */
    uint16_t cpu_critical_temperature_threshold;    /* Critical Temperature threshold which triggers "CPU temperature"
                                                       event when crossed, Kelvin degrees */
    uint16_t system_temperature_threshold;  /* Temperature threshold which triggers "system temperature" event when
                                               crossed, Kelvin degrees */
    uint16_t system_critical_temperature_threshold; /* Critical Temperature threshold which triggers "system
                                                       temperature" event when crossed, Kelvin degrees */
    uint16_t backup_power_temperature_threshold;    /* Temperature threshold which triggers "backup power temperature"
                                                       event when crossed, Kelvin degrees */
    uint16_t backup_power_critical_temperature_threshold;   /* Critical Temperature threshold which triggers "backup
                                                               power temperature" event when crossed, Kelvin degrees */
    uint32_t events_polling_interval;   /* Events polling interval, millisecond.  */
    NVRAM_auto_restore_mode_e auto_restore_mode;    /* NVRAM Auto Restore Mode */
    NVRAM_flash_bank_id_e auto_restore_bank;    /* Auto restore bank ID */
    uint32_t heartbeat_msg_interval;    /* Interval of heart beat messages (in milliseconds) */
    uint32_t heartbeat_msg_number;  /* Number ofLost Beats to do vaulting */
    NVRAM_auto_backup_mode_e auto_backup_mode_when_power_lost;  /* Enable/Disable auto backup when power lost */
    NVRAM_flash_bank_id_e auto_backup_bank_when_power_lost; /* Auto backup bank ID when power lost */
    NVRAM_auto_backup_mode_e auto_backup_mode_when_heartbeat_lost;  /* Enable/Disable auto backup when heartbeat lost */
    NVRAM_flash_bank_id_e auto_backup_bank_when_heartbeat_lost; /* Auto backup bank ID when heartbeat lost */
    NVRAM_auto_backup_mode_e auto_backup_mode_when_cpu_over_temperature;    /* Enable/Disable auto backup when CPU over 
                                                                               temperature */
    NVRAM_flash_bank_id_e auto_backup_bank_when_cpu_over_temperature;   /* Auto backup bank ID when CPU over
                                                                           temperature */
    NVRAM_auto_backup_mode_e auto_backup_mode_when_system_over_temperature; /* Enable/Disable auto backup when System
                                                                               over temperature */
    NVRAM_flash_bank_id_e auto_backup_bank_when_system_over_temperature;    /* Auto backup bank ID when System over
                                                                               temperature */
    NVRAM_auto_backup_mode_e auto_backup_mode_when_backup_power_over_temperature;   /* Enable/Disable auto backup when
                                                                                       Backup Power over temperature */
    NVRAM_flash_bank_id_e auto_backup_bank_when_backup_power_over_temperature;  /* Auto backup bank ID when Backup
                                                                                   Power over temperature */
    p_bool_t enable_ramdisk_encryption; /* Enable encryption of the RAM Drive content */
    p_bool_t enable_ramdisk_dmi_overlap;    /* Enable RAM Drive and DMI overlap */
    uint64_t dmi_size;          /* DMI size (bytes) */
    uint64_t ramdisk_size;      /* RAM Drive size (bytes) */
    p_bool_t bad_block_scan_use_vbbs;   /* Use VBBS table during bad block scan */
    p_bool_t enable_debug;      /* Enable/Disable debug fucntions */
} NVRAM_config_data_u;


/*
Name:            NVRAM_debug_config_data_u
Description:    NVRAM debug configuration data which can be configured in NVRAM system.
*/
typedef union
{
    p_bool_t        ignore_bad_block_threshold;                      /* Ignore bad block threshold                   */
    uint32_t        reserve;                                        /* reserve, to force PRP size to be at least 4 bytes    */

} NVRAM_debug_config_data_u;



/******************************************/
/* EVENTS description */
/******************************************/

/* VAULT Event subtype data structure */
#ifdef PLATFORM_HOST
typedef struct
{
    struct
    {
        P_STATUS status;        /* Backup operation status */
        uint64_t size;          /* Image size (byte) */
#if 0                           // TBD
        NVRAM_flash_bank_id_e bank_id;  /* Bank ID */
        NVRAM_backup_reason_e reason;   /* backup reason */
#endif
    } backup;

    struct
    {
        P_STATUS status;        /* Restore operation status */
        uint64_t size;          /* Image size (byte) */
#if 0                           // TBD
        NVRAM_flash_bank_id_e bank_id;  /* Bank ID */
#endif
    } restore;

    struct
    {
        P_STATUS status;        /* Erase operation status */
        uint64_t size;          /* Image size (byte) */
#if 0                           // TBD
        NVRAM_flash_bank_id_e bank_id;  /* Bank ID */
#endif
    } erase;

    struct
    {
        P_STATUS status;        /* Erase operation status */
        uint64_t size;          /* Image size (byte) */
#if 0                           // TBD
        NVRAM_flash_bank_id_e bank_id;  /* Bank ID */
#endif
    } bad_block_scan;

} NVRAM_event_vault_data_s;
#else
typedef struct
{

    uint64_t backup_size;       /* Image size (byte) */
    uint64_t restore_size;      /* Image size (byte) */
    uint64_t erase_size;        /* Image size (byte) */
    uint64_t bad_block_scan_size;   /* Image size (byte) */

    P_STATUS backup_status;     /* Backup operation status */
    P_STATUS restore_status;    /* Restore operation status */
    P_STATUS erase_status;      /* Erase operation status */
    P_STATUS bad_block_scan_status; /* Erase operation status */

    NVRAM_backup_reason_e backup_reason;    /* backup reason */
    NVRAM_flash_bank_id_e bank_id;  /* Bank ID */

} NVRAM_event_vault_data_s;
#endif

/* CPU Event subtype data structure */
typedef struct
{
    struct
    {
        p_bool_t above_threshold;   /* Is cpu temperature exceeds the threshold */
        uint32_t value;         /* The cpu temperature value */
    } temperature;

    struct
    {
        p_bool_t above_threshold;   /* Is critical cpu temperature exceeds the threshold */
        uint32_t value;         /* The critical cpu temperature value */
    } critical_temperature;

} NVRAM_event_cpu_data_s;

/* SYSTEM Event subtype data structure */
typedef struct
{
    struct
    {
        p_bool_t above_threshold;   /* Is system temperature exceeds the threshold */
        uint32_t value;         /* The system temperature value */
    } temperature;

    struct
    {
        p_bool_t above_threshold;   /* Is critical system temperature exceeds the threshold */
        uint32_t value;         /* The critical system temperature value */
    } critical_temperature;

} NVRAM_event_system_data_s;

/* POWER_BACKUP Event subtype data structure */
#ifdef PLATFORM_HOST
typedef struct
{
    struct
    {
        p_bool_t above_threshold;   /* Is supercap temperature exceeds the threshold */
        uint32_t value;         /* The supercap temperature value */
    } temperature;

    struct
    {
        p_bool_t above_threshold;   /* Is critical supercap temperature exceeds the threshold */
        uint32_t value;         /* The critical supercap temperature value */
    } critical_temperature;

    struct
    {
        p_bool_t fully_charged; /* Is backup power fully charged */
        uint32_t value;         /* The charge level value */
    } charge_level;

    struct
    {
        p_bool_t above_threshold;   /* Is voltage exceeds the threshold */
        uint32_t value;         /* The voltage value */
    } voltage;

    struct
    {
        p_bool_t above_threshold;   /* Is current exceeds the threshold */
        uint32_t value;         /* The current value */
    } current;

    struct
    {
        p_bool_t imbalance;     /* Is supercap balanced */
        uint32_t voltage_cap1;  /* Supercap capacitor #1 voltage */
        uint32_t voltage_cap2;  /* Supercap capacitor #2 voltage */
    } balance;

    struct
    {
        p_bool_t failed;        /* Is learning opertion failes */
    } learning;

    struct
    {
        NVRAM_health_state_e state; /* Health state */
    } health;

    struct
    {
        NVRAM_backup_power_src_e new_power_src; /* New power source */
    } power_src;

} NVRAM_event_backup_power_data_s;
#else
typedef struct
{
    uint32_t temperature_value; /* The supercap temperature value */
    uint32_t critical_temperature_value;    /* The critical supercap temperature value */
    uint32_t charge_level_value;    /* The charge level value */
    uint32_t voltage_value;     /* The voltage value */
    uint32_t current_value;     /* The current value */
    uint32_t balance_voltage_cap1;  /* Supercap capacitor #1 voltage */
    uint32_t balance_voltage_cap2;  /* Supercap capacitor #2 voltage */

    p_bool_t temperature_above_threshold;   /* Is supercap temperature exceeds the threshold */
    p_bool_t critical_temperature_above_threshold;  /* Is critical supercap temperature exceeds the threshold */
    p_bool_t charge_level_fully_charged;    /* Is backup power fully charged */
    p_bool_t voltage_above_threshold;   /* Is voltage exceeds the threshold */
    p_bool_t current_above_threshold;   /* Is current exceeds the threshold */
    p_bool_t balance_imbalance; /* Is supercap balanced */
    p_bool_t learning_failed;   /* Is learning opertion failes */
    NVRAM_health_state_e health_state;  /* Health state */
    NVRAM_backup_power_src_e new_power_src; /* New power source */

} NVRAM_event_backup_power_data_s;
#endif

/* ECC Event subtype data structure */
typedef struct
{
    // TBD src_id; // Enum should be defined by Ehud
    uint64_t address;

} NVRAM_event_ddr_ecc_data_s;

/* GENERAL Event subtype data structure */
typedef struct
{
    struct
    {
        P_STATUS status;        /* The status of BIST */
        uint32_t num_of_errors; /* The number of errors which found during BIST */

    } bist;                     /* Built In Self Test */

} NVRAM_event_general_s;

/* DEVICE HEALTH Event subtype data structure  */
typedef struct
{
       uint32_t device_health_bitmask;        /* health bitmask  */
} NVRAM_event_dev_health_s;



/* 
   Name: NVRAM_event_data_u Description: Data of incoming NVRAM event. */
typedef union
{
    NVRAM_event_vault_data_s vault_data;    /* Vault event data */
    NVRAM_event_cpu_data_s cpu_data;    /* CPU event data */
    NVRAM_event_system_data_s system_data;  /* System event data */
    NVRAM_event_backup_power_data_s backup_power_data;  /* Backup power event data */
    NVRAM_event_ddr_ecc_data_s ddr_ecc_data;    /* DDR ECC data */
    NVRAM_event_general_s general_data; /* General data */
    NVRAM_event_dev_health_s        dev_health_data;        /* Device health data */
} NVRAM_event_data_u;





/* 
   Name: NVRAM_flash_bank_info_data_s Description: Flash bank information data. */
typedef struct
{
    NVRAM_flash_bank_state_e bank_state;    /* The current state of the bank */
    p_bool_t is_power_lost_backup_bank; /* Indicates whether this flash bank will be used for RAM backup on the next
                                           power-lost (defined by Config Set API) */
    NVRAM_power_lost_backup_start_status_e is_last_power_lost_backup_started;   /* Indicates whether last power loss
                                                                                   bcakup was started or not, and why
                                                                                   not */
    uint64_t backup_id;         /* Incrementally ID which identify the backup */
    NVRAM_backup_reason_e backup_reason;    /* The reason of the backup */
    uint64_t backup_image_size; /* The size of the backup image */
    p_time_t backup_time_stamp; /* The time stamp of the last backup, seconds (from 1/1/1970) */
    uint64_t last_backup_duration;  /* The duration of the last backup operation, seconds */
    uint64_t last_restore_duration; /* The duration of the last restore operation, seconds */
    NVRAM_restore_status_e last_restore_status; /* The status of last restore */
    uint32_t bad_block_cnt;     /* counter of the bad block in the bank */
    uint32_t good_block_cnt;    /* counter of the good block in the bank */
    uint32_t unmapped_block_cnt;    /* counter of the unmapped block in the bank */

} NVRAM_flash_bank_info_data_s;



/* 
   Name: NVRAM_fw_metadata_s Description: FW status. */
typedef struct
{
    /* 
     * Indicates the number of the active slot
     */
    uint8_t activeSlot;
    
    /* 
     * Indicates the number of the active copy in the slot (0-Ping, 1-Pong)
     */
    uint8_t activeCopy[NUM_OF_FW_SLOTS];

    /* 
     * Indicates FW download counter per slot
     */
    uint16_t fwDownloadCounter[NUM_OF_FW_SLOTS];

    /* 
     * Array of bad block, each entry in the array is a byte that represent 8 block which are blocks 0-7.
     * Only blocks 1-3 will have valis value that indicates the status of the FW area blocks
     */
    uint8_t fwAreaBadBlockArr[FW_MAX_CHANNELS][FW_MAX_TARGETS][FW_MAX_LUNS];
} NVRAM_fw_metadata_s;


/* 
   Name: NVRAM_statistics_data_u Description: NVRAM statistics data. */
typedef union
{
    struct
    {
        uint32_t num_bad_blocks;
        uint32_t num_blocks_erase_failures;
        uint32_t num_blocks_write_failures;
        uint32_t num_blocks_read_failures;
        uint32_t correctable_ecc_errors;
        uint32_t max_correctable_ecc_bit_errors;
        uint32_t max_correctable_ecc_bit_errors_last_restore;
        uint32_t uncorrectable_ecc_errors;
    } flash;

    struct
    {
        uint32_t correctable_ecc_errors;
        uint32_t uncorrectable_ecc_errors;
    } ddr;

    struct
    {
        uint32_t bank0_num_vault_success;
        uint32_t bank0_num_vault_failed;
        uint32_t bank0_num_vault_aborted;
        uint32_t bank0_num_restore_success;
        uint32_t bank0_num_restore_failed;
        uint32_t bank0_num_erase_success;
        uint32_t bank0_num_erase_failed;
        uint32_t bank1_num_vault_success;
        uint32_t bank1_num_vault_failed;
        uint32_t bank1_num_vault_aborted;
        uint32_t bank1_num_restore_success;
        uint32_t bank1_num_restore_failed;
        uint32_t bank1_num_erase_success;
        uint32_t bank1_num_erase_failed;
        uint32_t num_power_loss;
        uint32_t num_host_reboot;
    } general;

} NVRAM_statistics_data_u;


typedef struct 
{
    NVRAM_flash_bank_id_e    bank_id;                /* Incrementally ID which identify the injection of errors                */
    p_bool_t    enable_rule;                        /* Inject or not - use intenrally by the FW - always TRUE from host side    */
    uint32_t    start_channel;                        /* The first channel to inject errors                                */
    uint32_t    start_target;                          /* The first target  to inject errors                                */
    uint32_t    start_lun;                              /* The first lun to inject errors                                    */
    uint32_t    start_block;                          /* The first block to inject errors                                    */
    uint32_t    start_page;                              /* The first page to inject errors                                    */
    uint32_t    skip_channel;                        /* How many channel skip for the next errors injectiion                 */
    uint32_t    skip_target;                          /* How many targets to skip for the next errors injectiion                 */
    uint32_t    skip_lun;                              /* How many luns to skip for the next errors injectiion                 */
    uint32_t    skip_block;                              /* How many blocks to skip for the next errors injectiion                 */
    uint32_t    skip_page;                              /* How many pages to skip for the next errors injectiion                 */
} NVRAM_debug_error_injection_rule;



#ifdef PLATFORM_HOST

/**********************************************************/
/* NVRAM event handlers prototype */
/**********************************************************/

/*==========================================================================================================*/
/* Name: PMC_NVRAM_gen_event_handler_t */
/* Description: NVRAM event handler function.  */
/*==========================================================================================================*/
typedef P_STATUS(*PMC_NVRAM_gen_event_handler_t) (const dev_handle_t dev_handle,    /* The device handle */
                                                  const NVRAM_event_type_e event_type,  /* The NVRAM event type */
                                                  const uint32_t subtype_bitmask,   /* The NVRAM event subtype bitmask */
                                                  const NVRAM_event_data_u * event_data);   /* The NVRAM event data,
                                                                                               may be NULL for some
                                                                                               event types */



/**********************************************************/
/* NVRAM APIs */
/**********************************************************/

/*==========================================================================================================*/
/* Name: PMC_NVRAM_system_config_set */
/* Description: This API sets the NVRAM system global configuration.  */
/*==========================================================================================================*/
     P_STATUS PMC_NVRAM_system_config_set(const NVRAM_system_config_type_e config_type,   /* The system configuration type to set */
                                   const NVRAM_system_config_data_u *config_data);    /* The system configuration data to set */


/*==========================================================================================================*/
/* Name: PMC_NVRAM_system_config_get */
/* Description: This API gets the NVRAM system global configuration.  */
/*==========================================================================================================*/
     P_STATUS PMC_NVRAM_system_config_get(const NVRAM_system_config_type_e config_type,   /* The system configuration type to get */
                                   NVRAM_system_config_data_u *config_data);  /* A pointer to configuration data structure which will be filled by this API */


/*==========================================================================================================*/
/* Name: PMC_NVRAM_card_list_get */
/* Description: This API gets list of the NVRAM cards in the system.  */
/* NOTE: To get only the NVRAM cards count set card_list_size to 0.  */
/* NOTE: Driver module must be loaded before calling this API.  */
/*==========================================================================================================*/
P_STATUS PMC_NVRAM_card_list_get(NVRAM_card_s card_list[],  /* Array of NVRAM_card_s to fill with cards info */
                                 const uint16_t card_list_size, /* The size of card_list array */
                                 uint16_t * card_list_count,    /* The count of items which actually filled in
                                                                   card_list array */
                                 uint16_t * card_total_count);  /* The total number of NVRAM cards in the system */


/*==========================================================================================================*/
/* Name: PMC_NVRAM_init */
/* Description: This API establishes a connection with the NVM controller and Initializes the NVRAM system. */
/* NOTE: Driver module must be loaded before calling this API.  */
/*==========================================================================================================*/
P_STATUS PMC_NVRAM_init(const char card_uid[],  /* A unique identifier of the NVRAM card, Null terminated string with
                                                   length of up to NVRAM_CARD_UID_SIZE */
                        dev_handle_t * dev_handle); /* A returned handle which identifies the NVRAM device uniquely and 
                                                       should be used by all other APIs */


/*==========================================================================================================*/
/* Name: PMC_NVRAM_release */
/* Description: This API closes the connection with the NVM controller and release system resources.  */
/*==========================================================================================================*/
P_STATUS PMC_NVRAM_release(const dev_handle_t dev_handle);  /* The device handle */


/*==========================================================================================================*/
/* Name: PMC_NVRAM_mem_map */
/* Description: This API maps the NVRAM memory into host virtual memory.  */
/*==========================================================================================================*/
P_STATUS PMC_NVRAM_mem_map(const dev_handle_t dev_handle,   /* The device handle */
                           const uint64_t size, /* Size of the mapped memory, 0-2^64-1 bytes) */
                           const int flags, /* Memory map flags: Either NVRAM_MEM_MAP_FLAGS_DATA_NONE or a bitwise
                                               inclusive OR of one or more of the following flags:
                                               NVRAM_MEM_MAP_FLAGS_DATA_READ,NVRAM_MEM_MAP_FLAGS_DATA_WRITE,
                                               NVRAM_MEM_MAP_FLAGS_DATA_EXEC. */
                           void **virtual_address); /* Returned pointer to the mapped area */


/*==========================================================================================================*/
/* Name: PMC_NVRAM_fw_download */
/* Description: This API starts downloading of firmware image into NVRAM controller.  */
/*==========================================================================================================*/
     P_STATUS PMC_NVRAM_fw_download(const dev_handle_t dev_handle,  /* The device handle */
                                    const uint8_t fw_index, /* The index of the FW image */
                                    const NVRAM_fw_source_e fw_source,  /* The source of the FW image */
                                    const void *fw_image,   /* The FW image */
                                    const uint32_t fw_image_size);  /* The size of the FW image */

/*====================================================*/
/* API: PMC_NVRAM_sbl_download */
/* Description: This API starts downloading of SBL image into NVRAM controller.  */
/*====================================================*/
P_STATUS PMC_NVRAM_sbl_download(const dev_handle_t dev_handle,
                               const NVRAM_fw_source_e sbl_source, const void *sbl_image, const uint32_t sbl_image_size);


/*==========================================================================================================*/
/* Name: PMC_NVRAM_reboot */
/* Description: This API reboots the NVRAM controller.  */
/*==========================================================================================================*/
     P_STATUS PMC_NVRAM_reboot(const dev_handle_t dev_handle);  /* The device handle */


/*==========================================================================================================*/
/* Name: PMC_NVRAM_config_set */
/* Description: This API sets the NVRAM configuration.  */
/*==========================================================================================================*/
     P_STATUS PMC_NVRAM_config_set(const dev_handle_t dev_handle,   /* The device handle */
                                   const NVRAM_config_type_e config_type,   /* The configuration type to set */
                                   const NVRAM_config_data_u * config_data);    /* The configuration data to set */


/*==========================================================================================================*/
/* Name: PMC_NVRAM_config_get */
/* Description: This API gets the NVRAM configuration.  */
/*==========================================================================================================*/
     P_STATUS PMC_NVRAM_config_get(const dev_handle_t dev_handle,   /* The device handle */
                                   const NVRAM_config_type_e config_type,   /* The configuration type to get */
                                   NVRAM_config_data_u * config_data);  /* The configuration data to get */


/*==========================================================================================================*/
/* Name: PMC_NVRAM_master_authenticate */
/* Description: This API runs master authentication between Host and NVM controller.  */
/*==========================================================================================================*/
     P_STATUS PMC_NVRAM_master_authenticate(const dev_handle_t dev_handle,  /* The device handle */
                                            const char auth_key[]); /* Authentication key */

/*==========================================================================================================*/
/* Name: PMC_NVRAM_admin_authenticate */
/* Description: This API runs admin authentication between Host and NVM controller.  */
/*==========================================================================================================*/
     P_STATUS PMC_NVRAM_admin_authenticate(const dev_handle_t dev_handle,   /* The device handle */
                                           const char auth_key[]);  /* Authentication key */

/*==========================================================================================================*/
/* Name: PMC_NVRAM_info_get */
/* Description: This API gets general information regarding the NVRAM card.  */
/*==========================================================================================================*/
     P_STATUS PMC_NVRAM_info_get(const dev_handle_t dev_handle, /* The device handle */
                                 const NVRAM_info_group_e info_group,   /* The NVRAM information group */
                                 NVRAM_info_data_u * info_data);    /* The NVRAM information data */


/*==========================================================================================================*/
/* Name: PMC_NVRAM_status_get */
/* Description: This API gets the NVRAM device statuses.  */
/*==========================================================================================================*/
     P_STATUS PMC_NVRAM_status_get(const dev_handle_t dev_handle,   /* The device handle */
                                   const NVRAM_status_group_e status_group, /* The NVRAM status group */
                                   NVRAM_status_data_u * status_data);  /* The NVRAM status data */

/*==========================================================================================================*/
/* Name: PMC_NVRAM_event_handler_register */
/* Description: This API registers NVRAM event hook handler.  */
/*==========================================================================================================*/
     P_STATUS PMC_NVRAM_event_handler_register(const dev_handle_t dev_handle,   /* The device handle */
                                               const NVRAM_event_type_e event_type, /* The NVRAM event type */
                                               const PMC_NVRAM_gen_event_handler_t event_handler,   /* Pointer to the
                                                                                                       handler callback 
                                                                                                       function, for
                                                                                                       details look at
                                                                                                       PMC_NVRAM_gen_event_handler_t 
                                                                                                     */
                                               uint16_t * event_context_id);    /* returned ID which used to unregister 
                                                                                   the event handler */


/*==========================================================================================================*/
/* Name: PMC_NVRAM_event_handler_unregister */
/* Description: This API unregisters NVRAM event hook handler.  */
/*==========================================================================================================*/
     P_STATUS PMC_NVRAM_event_handler_unregister(const dev_handle_t dev_handle, /* The device handle */
                                                 const uint16_t event_context_id);  /* ID of the event to unregister
                                                                                       (ID which returned by
                                                                                       PMC_NVRAM_event_handler_register 
                                                                                       API) */


/*==========================================================================================================*/
/* Name: PMC_NVRAM_event_poll */
/* Description: This API polls for events from all devices in the system */
/*              and executes the relevant registered event handlers (hook functions).  */
/*                 NOTE: Applicable only when thread mode is set to NVRAM_OPERATION_MODE_USER.  */
/*==========================================================================================================*/
     P_STATUS PMC_NVRAM_event_poll(void);



/*==========================================================================================================*/
/* Name: PMC_NVRAM_bad_block_scan */
/* Description: This API start scan bad block operation.  */
/*==========================================================================================================*/
     P_STATUS PMC_NVRAM_bad_block_scan(const dev_handle_t dev_handle,   /* The device handle */
                                       const NVRAM_flash_bank_id_e bank_id);    /* The ID of the bank */


/*==========================================================================================================*/
/* Name: PMC_NVRAM_erase */
/* Description: This API erases a flash bank.  */
/*==========================================================================================================*/
     P_STATUS PMC_NVRAM_erase(const dev_handle_t dev_handle,    /* The device handle */
                              const NVRAM_flash_bank_id_e bank_id,  /* The ID of the bank */
                              const NVRAM_flash_erase_type_e erase_type);   /* The type of the erase */


/*==========================================================================================================*/
/* Name: PMC_NVRAM_backup */
/* Description: This API starts a backup of the RAM content on a flash bank.  */
/*==========================================================================================================*/
     P_STATUS PMC_NVRAM_backup(const dev_handle_t dev_handle,   /* The device handle */
                               const NVRAM_flash_bank_id_e bank_id);    /* The ID of the bank */


/*==========================================================================================================*/
/* Name: PMC_NVRAM_restore */
/* Description: This API starts restoring of the RAM content from a flash bank.  */
/*==========================================================================================================*/
     P_STATUS PMC_NVRAM_restore(const dev_handle_t dev_handle,  /* The device handle */
                                const NVRAM_flash_bank_id_e bank_id);   /* The ID of the bank */

/*==========================================================================================================*/
/* Name: PMC_NVRAM_restore_corrupted */
/* Description: This API starts restoring corrupted of the RAM content from a flash bank.  */
/*==========================================================================================================*/
     P_STATUS PMC_NVRAM_restore_corrupted(const dev_handle_t dev_handle,    /* The device handle */
                                          const NVRAM_flash_bank_id_e bank_id); /* The ID of the bank */

/*==========================================================================================================*/
/* Name: PMC_NVRAM_flash_bank_info_get */
/* Description: This API gets information about flash bank.  */
/*==========================================================================================================*/
     P_STATUS PMC_NVRAM_flash_bank_info_get(const dev_handle_t dev_handle,  /* The device handle */
                                            const NVRAM_flash_bank_id_e bank_id,    /* The ID of the bank */
                                            NVRAM_flash_bank_info_data_s * info_data);  /* The info data of the bank */


/*==========================================================================================================*/
/* Name: PMC_NVRAM_fw_metadata_get */
/* Description: This API gets FW metadata.  */
/*==========================================================================================================*/
    P_STATUS PMC_NVRAM_fw_metadata_get(const dev_handle_t dev_handle,
                                       NVRAM_fw_metadata_s *fw_metadata);


/*==========================================================================================================*/
/* Name: PMC_NVRAM_statistics_get */
/* Description: This API gets NVRAM statistics.  */
/*==========================================================================================================*/
     P_STATUS PMC_NVRAM_statistics_get(const dev_handle_t dev_handle,   /* The device handle */
                                       const NVRAM_statistics_group_e statistics_group, /* The NVRAM statistics group
                                                                                           to get */
                                       NVRAM_statistics_data_u * statistics_data);  /* The NVRAM statistics data */

/*==========================================================================================================*/
/* Name: PMC_NVRAM_statistics_reset */
/* Description: This API resets NVRAM statistics.  */
/*==========================================================================================================*/
     P_STATUS PMC_NVRAM_statistics_reset(const dev_handle_t dev_handle, /* The device handle */
                                         const NVRAM_statistics_group_e statistics_group);  /* The NVRAM statistics
                                                                                               group to reset */


/*====================================================*/
/* API: PMC_NVRAM_debug_get_log_page */
/* Description: This DEBUG API gets the log page from appropriate manager.  */
/*====================================================*/
     P_STATUS PMC_NVRAM_debug_get_log_page(const dev_handle_t dev_handle,   /* The device handle */
                                           const NVRAM_log_page_id_e log_page_id,   /* LOG page ID (manager #) */
                                           const uint32_t log_strings_num,  /* Number of LOG strings */
                                           const NVRAM_log_page_from_backup_e from_backup,  /* Get backed-up log page */
                                           char *log_buf    /* Output LOG buffer */
    );

/*================================================================*/
/* API: PMC_NVRAM_debug_page_of_block_get */
/* Description: This DEBUG API retrievs a block or page from flash */
/*================================================================*/
     P_STATUS PMC_NVRAM_debug_page_of_block_get(const dev_handle_t dev_handle,  /* The device handle */
                                                const uint32_t channel, /* channel number */
                                                const uint32_t target,  /* target number */
                                                const uint32_t lun, /* lun number */
                                                const uint32_t block,   /* block number */
                                                const uint32_t page,    /* page number */
                                                const uint32_t page_size,   /* page size */
                                                char *out_buf   /* The buffer where the content is copied */
    );

/*============================================================*/
/* API: PMC_NVRAM_debug_block_write */
/* Description: This DEBUG API writes to a specific block */
/*============================================================*/
     P_STATUS PMC_NVRAM_debug_block_write(const dev_handle_t dev_handle,    /* The device handle */
                                          const uint32_t channel,   /* channel number */
                                          const uint32_t target,    /* target number */
                                          const uint32_t lun,   /* lun number */
                                          const uint32_t block, /* block number */
                                          const uint32_t page,  /* page number */
                                          const uint32_t page_size, /* page size */
                                          char *in_buf  /* The buffer holds the data to write to the block */
    );

/*============================================================*/
/* API: PMC_NVRAM_debug_inject_UC_errors */
/* Description: This DEBUG API to inject uncorrectable errors */
/*============================================================*/
     P_STATUS PMC_NVRAM_debug_inject_UC_errors(const dev_handle_t dev_handle,   /* The device handle */
                                               NVRAM_debug_error_injection_rule * inject_UC_error    /* A pointer to the
                                                                                                   inject errors */
    );


/*============================================================*/
/* API: PMC_NVRAM_debug_simulate_bad_block */
/* Description: This DEBUG API to simulate bad blocks */
/*============================================================*/
P_STATUS PMC_NVRAM_debug_simulate_bad_block(const dev_handle_t dev_handle,    /* The device handle */
                                            NVRAM_debug_error_injection_rule * simulate_bad_block /* A pointer to the bad block config */ );


/*============================================================*/
/* API: PMC_NVRAM_register_access */
/* Description: This API access registers */
/*============================================================*/
P_STATUS PMC_NVRAM_register_access(const dev_handle_t dev_handle,  /* The device handle */
                                     const uint32_t set_get, /* either set or get register */
                                      const uint32_t address, /* register address */
                                      const uint32_t value,   /* register value */
                                      const uint32_t mask,    /* what bits to read modify write */
                                      uint32_t * out /* return arg */
);

/*============================================================*/
/* API: PMC_NVRAM_self_test */
/* Description: This API to run self test */
/*============================================================*/
     P_STATUS PMC_NVRAM_self_test(const dev_handle_t dev_handle /* The device handle */
    );

/*====================================================*/
/* API: PMC_NVRAM_debug_get_ddr */
/* Description: This DEBUG API gets the chunk of DDR by address and size.  */
/*====================================================*/
     P_STATUS PMC_NVRAM_debug_get_ddr(const dev_handle_t dev_handle,    /* The device handle */
                                      const uint64_t ddr_addr,  /* The address of ddr to be get */
                                      const uint32_t ddr_size,  /* The size of ddr to be get */
                                      char *out_buf /* The buffer where DDR content is copied */
    );

/*====================================================*/
/* API: PMC_NVRAM_debug_get_frim_mapping */
/* Description: This DEBUG API gets the frim mapping for select channel target luns or for all  */
/*====================================================*/
     P_STATUS PMC_NVRAM_debug_get_frim_mapping(const dev_handle_t dev_handle, /* The device handle */
                                               const uint32_t bank_id, /* the requested bank_id */
                                               const uint32_t chs,   /* number of channels */
                                               const uint32_t targets,   /* number of targets */
                                               const uint32_t luns,   /* number of luns */
                                               const uint32_t get_all, /* get one or get all */
                                               unsigned short *out_buf,           /* The buffer where frim mapping content is copied */
                                               NVRAM_actual_geometry * geometry  /* the actual geometry used */
     );

/*====================================================*/
/* API: PMC_NVRAM_clear_frim_mapping */
/* Description: This API instructs the firmware to clear the FRIM block mapping */
/*====================================================*/
P_STATUS PMC_NVRAM_clear_frim_mapping(const dev_handle_t dev_handle, 
                                      const NVRAM_flash_bank_id_e bank_id);

/*====================================================*/
/* API: PMC_NVRAM_debug_fw_fail */
/* Description: This DEBUG API simulates the FW failure.  */
/*====================================================*/
     P_STATUS PMC_NVRAM_debug_fw_fail(const dev_handle_t dev_handle,    /* The device handle */
                                      const NVRAM_log_page_id_e proc_id,    /* The id of processor to be failes */
                                      const NVRAM_fw_fail_test_op_id_e fail_op  /* The fail operation */
    );

/*====================================================*/
/* API: PMC_NVRAM_time_of_day_sync */
/* Description: This API sets the time of day in the controller based on the current system time */
/*====================================================*/
     P_STATUS PMC_NVRAM_time_of_day_sync(const dev_handle_t dev_handle);

/*====================================================*/
/* API: PMC_NVRAM_debug_bad_block_count_set */
/* Description: This DEBUG API sets the number of bad blocks */
/*====================================================*/
     P_STATUS PMC_NVRAM_debug_bad_block_count_set(const dev_handle_t dev_handle,    /* The device handle */
                                                  const uint32_t block_count,   /* The number of blocks to set */
                                                  const uint32_t value  /* The value to be set in the blocks */
    );

/*====================================================*/
/* API: PMC_NVRAM_debug_bad_block_set_by_lun */
/* Description: This DEBUG API sets blocks as bad/good by lun */
/*====================================================*/
     P_STATUS PMC_NVRAM_debug_bad_block_set_by_lun(const dev_handle_t dev_handle,   /* The device handle */
                                                   const uint32_t num_of_blocks,    /* The number of blocks to set */
                                                   const uint32_t channel,  /* channel number */
                                                   const uint32_t target,   /* target number */
                                                   const uint32_t lun,  /* lun number */
                                                   const uint32_t first_block,  /* first block number */
                                                   const NVRAM_flash_bank_id_e bank,    /* bank number */
                                                   const uint32_t set_as_bad    /* The value to be set in the blocks (1 
                                                                                   for bad, 0 for good) */
    );


/*==========================================================================================================*/
/* Name:        PMC_NVRAM_debug_config_set                                                                  */
/* Description: This API sets the NVRAM debug configuration.                                                */
/*==========================================================================================================*/
P_STATUS PMC_NVRAM_debug_config_set(
                    const dev_handle_t                    dev_handle,        /* The device handle                */
                    const NVRAM_debug_config_type_e        config_type,    /* The configuration type to set    */
                    const NVRAM_debug_config_data_u        *config_data);    /* The configuration data to set    */


/*==========================================================================================================*/
/* Name:        PMC_NVRAM_debug_config_get                                                                  */
/* Description: This API gets the NVRAM debug configuration.                                                */
/*==========================================================================================================*/
P_STATUS PMC_NVRAM_debug_config_get(
                    const dev_handle_t                  dev_handle,        /* The device handle                */
                    const NVRAM_debug_config_type_e     config_type,    /* The configuration type to get    */
                    NVRAM_debug_config_data_u           *config_data);    /* The configuration data to get    */


/*====================================================*/
/* API: PMC_NVRAM_heartbeat_listen */
/* Description: This API starts/stops/resets the heartbeat listening by FW */
/*====================================================*/
     P_STATUS PMC_NVRAM_heartbeat_listen(const dev_handle_t dev_handle, /* The device handle */
                                         NVRAM_heartbeat_command_e heartbeat_command    /* Heart beat command */
    );


/*====================================================*/
/* API: PMC_NVRAM_heartbeat */
/* Description: This API sends the heartbeat */
/*====================================================*/
     void *PMC_NVRAM_heartbeat(void *arg);


/*====================================================*/
/* API: PMC_NVRAM_lba_to_addr_map_get */
/* Description: This API get the mapping between LBA */
/* and DDR Address.  */
/*====================================================*/
     P_STATUS PMC_NVRAM_lba_to_addr_map_get(const dev_handle_t dev_handle,  /* The device handle */
                                            const uint32_t lba, /* The LBA */
                                            uint64_t * addr);   /* The DDR Adrress of the LBA */

/*====================================================*/
/* API: PMC_NVRAM_addr_to_lba_map_get */
/* Description: This API get the mapping between DDR */
/* Address and LBA.  */
/*====================================================*/
     P_STATUS PMC_NVRAM_addr_to_lba_map_get(const dev_handle_t dev_handle,  /* The device handle */
                                            const uint64_t addr,    /* The DDR Adrress */
                                            uint32_t * lba);    /* The LBA of the DDR Adrress */

/*====================================================*/
/* API: PMC_NVRAM_pfi_get */
/* Description: This API retrieves Product Feature Info data from device */
/*====================================================*/
     P_STATUS PMC_NVRAM_pfi_get(const dev_handle_t dev_handle,  uint32_t pfi_data_size,
                               char     *pfi_data);

/*====================================================*/
/* API: PMC_NVRAM_ramdisk_enc_key_set */
/* Description: This API sets RAM disk encription key to device */
/*====================================================*/
     P_STATUS PMC_NVRAM_ramdisk_enc_key_set(const dev_handle_t dev_handle, 
                           const NVRAM_ramdisk_enc_key_s * ramdisk_enc_key);

#endif /* PLATFORM_HOST */

// ////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ////////////////////////////////////////////////////////////////////////////////////////////////////////////
#endif /* __PMC_NVRAM_API_EXPO_H__ */
