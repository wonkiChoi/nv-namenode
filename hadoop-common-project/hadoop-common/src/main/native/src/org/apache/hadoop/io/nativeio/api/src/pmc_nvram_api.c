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
* Description                  : PMC NVRAM API
*
* $Revision: 1.1 $
* $Date: 2013/09/23 07:07:42 $
* $Author: dvoryyos $
* Release $Name:  $
*
****************************************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include "pmc_nvram_api.h"
#include "pmc_nvram_api_expo.h"
#include "pmc_log.h"
#include "pmc_nvme.h"
#include "pmc_nvram_events.h"
#include "pmc_utils.h"
#include "pmc_ossrv.h"
#ifdef OS_FREE_BSD
#include <sys/pciio.h>
#include <sys/module.h>
#include <sys/param.h>
#include <sys/linker.h>
#include <sys/disk.h>
#include <dev/nvme/nvme.h>
#endif



// #include "pmc_periodic_tasks.h"





/**********************************************************/
/* Internal Defines */
/**********************************************************/



#define DEV_PCI_PATH				"/dev/pci" /* dev pci */
#define DEV_PATH					"/dev/" /* dev sysfs */
#define NVME_DRIVER					"nvme"  /* NVMe driver name */
#define SYSFS_ENTRY_LB_SIZE_PREF    "/sys/dev/block/"   /* prefix of the path to sysfs entry that holds logical block
                                                           size */
#define SYSFS_ENTRY_LB_SIZE_SUFF    "/queue/logical_block_size" /* suffix of the path to sysfs entry that holds logical 
                                                                   block size */
#define PMC_NVRAM_MOD				"pmc_nvram" /* PMC NVRAM module name */
#define PMC_NVRAM_MOD_PATH			"/dev/pmc_nvram"    /* PMC NVRAM module name */

#define PROC_MODULES_PATH			"/proc/modules" /* sysfs to retrieve modules existence */
#define PROC_DEVICES_PATH			"/proc/bus/pci/devices" /* sysfs to retrieve some PCI info */
#define SYS_NVME_MODULE_VER			"/sys/module/nvme/version"  /* sysfs to retrieve NVMe driver module version */
#define SYS_PMC_NVRAM_MODULE_VER	"/sys/module/pmc_nvram/version" /* sysfs to retrieve PMC NVRAM module version */



#define PCI_ADDR_MEM_MASK			0x0F    /* mask for the 4 lsb bits in the memory BAR */
#define	PMC_PCI_VENDOR_ID1			"111d"
#define	PMC_PCI_VENDOR_ID2			"11F8"
#define	PMC_PCI_DEVICE0				"80d0"
#define	PMC_PCI_DEVICE1				"80d1"
#define	PMC_PCI_DEVICE2				"80d2"
#define	PMC_PCI_DEVICE3				"f117"
#define	PMC_PCI_VENDOR_DEVICE0		PMC_PCI_VENDOR_ID1 PMC_PCI_DEVICE0
#define	PMC_PCI_VENDOR_DEVICE1		PMC_PCI_VENDOR_ID1 PMC_PCI_DEVICE1
#define	PMC_PCI_VENDOR_DEVICE2		PMC_PCI_VENDOR_ID1 PMC_PCI_DEVICE2
#define	PMC_PCI_VENDOR_DEVICE3		PMC_PCI_VENDOR_ID2 PMC_PCI_DEVICE3
#define	PMC_PCI_MT_RAMON_DEVICE		PMC_PCI_VENDOR_ID2":"PMC_PCI_DEVICE3

#define PMC_PCI_MT_RAMON_VENDOR_ID   0x11f8
#define PMC_PCI_MT_RAMON_DEVICE_ID   0xf117

#define MAX_LINE_LEN				256
#define MAX_TOKENS_COUNT			256
#define MAX_PCI_FIELD_LEN			16 /*(8 bytes * 2 chars per byte)*/

#define UNDEFINED_FD				-1
#define UNDEFINED_DEV_ID			-1

/* Prefix of log page ID */
#define NVRAM_LOG_LOG_PAGE_PROC_0            (0xC0)

/* Prefix of backed up log page ID */
#define NVRAM_LOG_BACKED_UP_LOG_PAGE_PROC_0            (0xE0)


/* Repeat count and delay for Get Status Backup Power command */
#define GET_POWER_STATUS_REPEAT_COUNT       3
#define GET_POWER_STATUS_REPEAT_DELAY       1000

#define	PMC_NVME_DRIVER_VERSION	    "-pmc"
/**********************************************************/
/* Internal Enums and Data types */
/**********************************************************/
typedef struct
{
    char card_uid[NVRAM_CARD_UID_SIZE];
    char dev_path[MAX_PATH_LEN];    /* character device path */
    char block_dev_path[MAX_PATH_LEN];  /* block device path */
    NVRAM_version_s driver_version;
    uint32_t nvme_index;

} Pmc_card_s;


typedef union
{
    void *base_addr;

} Pmc_pci_info_data_u;

typedef enum
{
    PCI_INFO_TYPE_BASE_ADDR
} Pmc_pci_info_type_e;

typedef enum
{
    PMC_IMAGE_TYPE_FW,
    PMC_IMAGE_TYPE_SBL
} Pmc_image_type_e;




/**********************************************************/
/* Internal Variables */
/**********************************************************/
/* PMC NVRAM module file descriptor */
static int g_module_handle = UNDEFINED_FD;

/* API semaphore */
static OSSRV_counting_semaphore_t g_api_sem;

/* Init once status */
static int pmc_init_once_status;

/* Events polling interval status */
static int g_is_polling_interval_initialized = PMC_FALSE;

/* Heart beat interval in milliseconds */
static unsigned g_heartbeat_interval = NVRAM_HEARTBEAT_INTERVAL_VAL_MIN;

/* buffer for log page request */
static char prp_inner[PAGES_PER_CORE * PRP_SIZE];

/* Any API already called */
static int g_any_api_already_called = PMC_FALSE;

/* Threads operation mode */
NVRAM_operation_mode_e g_thread_mode = NVRAM_OPERATION_MODE_AUTO;

/* Smaphores operation mode */
NVRAM_operation_mode_e g_semaphore_mode = NVRAM_OPERATION_MODE_AUTO;


/**********************************************************/
/* Internal functions */
/**********************************************************/

/* This func init api lib semaphore */
static void Pmc_init_once(void)
{
    /* init semaphore */
    API_SEM_INIT(g_api_sem, 1, pmc_init_once_status);
}

/* this func get module version */
#ifdef OS_FREE_BSD
static int Pmc_module_version_get(const char *module_name, NVRAM_version_s * version)
{
	sprintf((char*)version,"%s","FreeBSD");
	return (P_STATUS_OK);
}
#else/*Linux OS case*/
static int Pmc_module_version_get(const char *module_version_path, NVRAM_version_s * version)
{
    FILE *f;

    f = fopen(module_version_path, "r");
    if (f == NULL)
    {
        PMCLOG_3(PMCLOG_ERROR, "Failed to open '%s', errno %d (%s).\n", module_version_path, errno, strerror(errno));
        return (P_STATUS_HOST_OS_ERROR);
    }

    if (fgets((char *)version, sizeof(NVRAM_version_s), f) == NULL)
    {
        PMCLOG_1(PMCLOG_ERROR, "Failed to read '%s'.\n", module_version_path);
        return (P_STATUS_LIB_GEN_ERROR);
    }

    Pmc_strtrim((char *)version);

    PMCLOG_1(PMCLOG_DEBUG, "Module version '%s'\n", version);

    fclose(f);

    return (P_STATUS_OK);
}
#endif
/* this func get module version */
#ifdef OS_FREE_BSD
void Pmc_nvme_driver_detect(const char *module_version_path)
{
	/*PMC nvme driver is not supported in FreeBSD*/
	Set_is_pmc_driver(PMC_FALSE);
    
}
#else
void Pmc_nvme_driver_detect(const char *module_version_path)
{
    NVRAM_version_s version;

    Pmc_module_version_get(module_version_path, &version);
    if (strstr((char*)&version,PMC_NVME_DRIVER_VERSION))
    	Set_is_pmc_driver(PMC_TRUE);
    else
    	Set_is_pmc_driver(PMC_FALSE);
    
}
#endif
#if 0
/* This func reterive PCI info from "/proc/bus/pci/devices" sysfs */
static int Pmc_pci_info_get(Pmc_pci_info_type_e info_type, Pmc_pci_info_data_u * info_data)
{
    FILE *f;
    int count;
    char file[MAX_PATH_LEN];
    char str[MAX_LINE_LEN];
    char *toks[MAX_TOKENS_COUNT];
    const char *bus_dev, *vendor_device, *bar4;
    long long unsigned int base_addr;


    memset(info_data, 0, sizeof(Pmc_pci_info_data_u));


    strcpy(file, PROC_DEVICES_PATH);
    f = fopen(file, "r");
    if (f == NULL)
    {
        PMCLOG_3(PMCLOG_ERROR, "Failed to open '%s', errno %d (%s).\n", file, errno, strerror(errno));
        return (P_STATUS_HOST_OS_ERROR);
    }


    while (fgets(str, sizeof(str), f))
    {
        /* str tok */
        Pmc_str_tok(str, toks, sizeof(toks) / sizeof(char *), &count);
        if (count < 8)          /* valid line must contains at least 8 tokens */
            continue;

        bus_dev = toks[0];      /* tok #0 = PCI bus/dev */
        vendor_device = toks[1];    /* tok #1 = PCI vendor/device */
        bar4 = toks[7];         /* tok #7 = PCI BAR 4 */


        if ((strcasecmp(vendor_device, PMC_PCI_VENDOR_DEVICE0) == 0) || (strcasecmp(vendor_device, PMC_PCI_VENDOR_DEVICE1) == 0) || (strcasecmp(vendor_device, PMC_PCI_VENDOR_DEVICE2) == 0) || (strcasecmp(vendor_device, PMC_PCI_VENDOR_DEVICE3) == 0))   /* PMC 
                                                                                                                                                                                                                                                               card 
                                                                                                                                                                                                                                                               found 
                                                                                                                                                                                                                                                             */
        {
            PMCLOG_2(PMCLOG_DEBUG, "PMC card '%s' was detected at PCI bus_dev '%s'.\n", vendor_device, bus_dev);

            switch (info_type)
            {
            case PCI_INFO_TYPE_BASE_ADDR:
                if (sscanf(bar4, "%LX", &base_addr) != 1)
                {
                    fclose(f);
                    PMCLOG_1(PMCLOG_ERROR, "Failed to parse %s.\n", file);
                    return (P_STATUS_LIB_GEN_ERROR);
                }

                info_data->base_addr = (void *)(base_addr & ~PCI_ADDR_MEM_MASK);    /* Remove the 4 lsb bits of the BAR 
                                                                                       (they are not part of th address 
                                                                                       but only indicate BAR
                                                                                       properties) */
                PMCLOG_2(PMCLOG_DEBUG, "PMC card '%s': memory base_addr = 0x%016LX.\n", vendor_device,
                         (uint64_t) info_data->base_addr);

                /* Check if addres is undefined */
                if (((uint64_t) info_data->base_addr) == 0x0)
                {
                    fclose(f);
                    PMCLOG_0(PMCLOG_ERROR, "Base address is undefined.\n");
                    return (P_STATUS_LIB_GEN_ERROR);
                }
                break;

            default:
                {
                    fclose(f);
                    PMCLOG_1(PMCLOG_ERROR, "Invalid info_type %d.\n", info_type);
                    return (P_STATUS_LIB_GEN_ERROR);
                }
            }


            fclose(f);
            return (P_STATUS_OK);
        }
    }


    fclose(f);

    PMCLOG_4(PMCLOG_ERROR, "PMC card of one of the types: '%s', '%s', '%s', '%s' was not detected.\n",
             PMC_PCI_VENDOR_DEVICE0, PMC_PCI_VENDOR_DEVICE1, PMC_PCI_VENDOR_DEVICE2, PMC_PCI_VENDOR_DEVICE3);
    return (P_STATUS_LIB_CARD_NOT_FOUND);
}
#endif
/* This func check if module is installed */
#ifdef OS_FREE_BSD
static int Pmc_check_module(const char *module_name, p_bool_t showNotFoundtMsg)
{
	int file_id;
	
	file_id = kldfind(module_name);//module_name);
	if (file_id < 0)
	{
        if (showNotFoundtMsg) 
        {
            PMCLOG_1(PMCLOG_ERROR, "Module '%s' is not installed.\n", module_name);
        }

		return (P_STATUS_LIB_NOT_EXIST);    /* driver not found */
	
	}
		
	return (P_STATUS_OK);
}
#else /*Linux OS case*/
static int Pmc_check_module(const char *module_name, p_bool_t showNotFoundtMsg)
{
    FILE *file;
    char line[MAX_LINE_LEN];
    p_bool_t found = PMC_FALSE;


    /* open /proc/modules virtual file */
    file = fopen(PROC_MODULES_PATH, "r");
    if (file == NULL)
    {
        PMCLOG_3(PMCLOG_ERROR, "Failed to open '%s', errno %d (%s).\n", PROC_MODULES_PATH, errno, strerror(errno));
        return (P_STATUS_HOST_OS_ERROR);
    }

    /* iterate the lines in /proc/modules and look for module_name */
    while (fgets(line, sizeof(line), file))
    {
        if (strstr(line, module_name) != NULL)
        {
            found = PMC_TRUE;
            break;
        }
    }

    fclose(file);

    if (!found)
    {
        if (showNotFoundtMsg) 
        {
            PMCLOG_1(PMCLOG_ERROR, "Module '%s' is not installed.\n", module_name);
        }

        return (P_STATUS_LIB_NOT_EXIST);    /* driver not found */
    }

    return (P_STATUS_OK);
}
#endif
/* This func get internal NVRAM data */
P_STATUS Pmc_internal_data_get(const dev_handle_t dev_handle,
                               const NVRAM_internal_data_group_e data_group, NVRAM_internal_data_u * data)
{
    int res;
    char prp[PRP_SIZE];
    nvme_arg_s arg;


    /* check params */
    CHECK_BOUNDS(data_group, NVRAM_INTERNAL_DATA_GROUP_FIRST, NVRAM_INTERNAL_DATA_GROUP_NUMBER - 1);
    CHECK_PTR(data);

    bzero(prp, sizeof(prp));
    bzero(data, sizeof(NVRAM_internal_data_u));

    bzero(&arg, sizeof(nvme_arg_s));
    arg.nvme_cmd.header.opCode = ADMIN_OPCODE_VENDOR_NVRAM; /* NVRAM command */
    arg.nvme_cmd.header.namespaceID = 0;    /* Namespace ID not in use */
    arg.nvme_cmd.header.prp1 = (uint64_t) prp;  /* PRP 1 (Output data) */
    arg.data_length = sizeof(prp);  /* PRP data len in Dwords */
    arg.nvme_cmd.cmd.vendorSpecific.vndrCDW12 = NVRAM_VENDOR_SUBCMD_INTERNAL_DATA_GET;  /* Vendor Specific Internal
                                                                                           Data Get command */
    arg.nvme_cmd.cmd.vendorSpecific.vndrCDW13 = (uint32_t) data_group;  /* Vendor Specific Internal Data Group */

    /* send command to device */
    res = pmc_ioctl(dev_handle, NVME_IOCTL_ADMIN_CMD, &arg);
    if (res != 0)
    {
        PMCLOG_1(PMCLOG_ERROR, "pmc_ioctl failed, error 0x%04x.\n", res);
        return (res);
    }


    /* copy the returned PRP to the user data structure */
    memcpy(data, prp, sizeof(NVRAM_internal_data_u));

    /* now take care of those fields which need final touch OR not retrieved from the card (like NVMe driver version
       for example ) */
    switch (data_group)
    {
    case NVRAM_INTERNAL_DATA_GROUP_GENERAL:
        /* Convert SN to string */
        Pmc_to_ascii(data->general.card_serial_number, 0, 6, data->general.card_serial_number, NVRAM_SN_SIZE);
        break;

    case NVRAM_INTERNAL_DATA_GROUP_HEARTBEAT:
        /* Nothing to do */
        break;

    default:
        PMCLOG_1(PMCLOG_ERROR, "Invalid data_group %d.\n", data_group);
        return (P_STATUS_LIB_BAD_PARAM);
        break;
    }

    return (P_STATUS_OK);
}


P_STATUS Pmc_sync_version(const dev_handle_t dev_handle)
{
    int res;
    char prp[PRP_SIZE];
    nvme_arg_s arg;
    struct iden_controller *buf = (struct iden_controller *)prp;

    bzero(prp, sizeof(prp));
	
    bzero(&arg, sizeof(nvme_arg_s));
    arg.nvme_cmd.header.opCode = ADMIN_OPCODE_IDENTIFY; /* NVRAM command */
    arg.nvme_cmd.header.namespaceID = 0;    /* Namespace ID not in use */
    arg.nvme_cmd.header.prp1 = (uint64_t) prp;  /* PRP 1 (Output data) */
    arg.data_length = sizeof(prp);  /* PRP data len in Dwords */
    arg.nvme_cmd.cmd.identify.controllerStructure = 1;


    /* send command to device */
    res = pmc_ioctl(dev_handle, NVME_IOCTL_ADMIN_CMD, &arg);
    if (res != 0)
    {
        PMCLOG_1(PMCLOG_ERROR, "pmc_ioctl failed, error 0x%04x.\n", res);
        return (res);
    }

    if (memcmp(buf->firmwareRev, NVRAM_API_LIB_VER_MAJOR, strlen(NVRAM_API_LIB_VER_MAJOR)) != 0) 
    {
        PMCLOG_1(PMCLOG_ERROR, "API LIB version is not compatible with firmware %s!\n", buf->firmwareRev);
        PMCLOG_1(PMCLOG_ERROR, "Firmware MAJOR version (%s.X.XX.X). is required!\n", NVRAM_API_LIB_VER_MAJOR);
        return (P_STATUS_LIB_FW_VER_NOT_COMPATIBLE);
    }

    return (P_STATUS_OK);
}



/* This func get NVRAM info */
static int Pmc_info_get(const dev_handle_t dev_handle,
                        const NVRAM_info_group_e info_group, NVRAM_info_data_u * info_data)
{
    int res;
    char prp[PRP_SIZE];
    nvme_arg_s arg;
    // Pmc_pci_info_data_u pci_info_data;

    /* check params */
    CHECK_BOUNDS(info_group, NVRAM_INFO_GROUP_FIRST, NVRAM_INFO_GROUP_NUMBER - 1);
    CHECK_PTR(info_data);

    bzero(prp, sizeof(prp));
    bzero(info_data, sizeof(NVRAM_info_data_u));

    bzero(&arg, sizeof(nvme_arg_s));
    arg.nvme_cmd.header.opCode = ADMIN_OPCODE_VENDOR_NVRAM; /* NVRAM command */
    arg.nvme_cmd.header.namespaceID = 0;    /* Namespace ID not in use */
    arg.nvme_cmd.header.prp1 = (uint64_t) prp;  /* PRP 1 (Output data) */
    arg.data_length = sizeof(prp);  /* PRP data len in Dwords */
    arg.nvme_cmd.cmd.vendorSpecific.vndrCDW12 = NVRAM_VENDOR_SUBCMD_INFO_GET;   /* Vendor Specific Info Get command */
    arg.nvme_cmd.cmd.vendorSpecific.vndrCDW13 = (uint32_t) info_group;  /* Vendor Specific Info Group */
#if 0
    arg.nvme_cmd.header.opCode = ADMIN_OPCODE_IDENTIFY; /* Identify command */
    arg.nvme_cmd.header.namespaceID = 0;    /* Namespace ID not in use */
    arg.nvme_cmd.header.prp1 = (uint64_t) prp;  /* PRP 1 (Output data) */
    arg.data_length = sizeof(prp);  /* PRP data len in Dwords */
    arg.nvme_cmd.cmd.identify.controllerStructure = 1;  /* Controller or Namespace Structure (CNS) = 1(controller) */
#endif


    /* send command to device */
    res = pmc_ioctl(dev_handle, NVME_IOCTL_ADMIN_CMD, &arg);
    if (res != 0)
    {
        PMCLOG_1(PMCLOG_ERROR, "pmc_ioctl failed, error 0x%04x.\n", res);
        return (res);
    }


    /* copy the returned PRP to the user info_data structure */
    memcpy(info_data, prp, sizeof(NVRAM_info_data_u));

    /* now take care of those fields which need final touch OR not retrieved from the card (like NVMe driver version
       for example ) */
    switch (info_group)
    {
    case NVRAM_INFO_GROUP_GENERAL:
        /* Convert card UID to string */
        Pmc_to_ascii(info_data->general.card_uid, 0, 6, info_data->general.card_uid, NVRAM_CARD_UID_SIZE);

        /* Convert controller version to string */
        Pmc_to_ascii(info_data->general.controller_version, 2, 2, info_data->general.controller_version,
                     sizeof(info_data->general.controller_version));

        /* retrieved NVMe driver version */
        res = Pmc_module_version_get(SYS_NVME_MODULE_VER, &info_data->general.driver_version);
        if (res != 0)
        {
            PMCLOG_1(PMCLOG_ERROR, "Pmc_module_version_get failed, error 0x%04x.\n", res);
            return (res);
        }

        /* retrieved PMC NVRAM module version if module installed */
        /* check if PMC NVRAM module is installed */
        res = Pmc_check_module(PMC_NVRAM_MOD, PMC_FALSE);
        if (res == P_STATUS_OK)
        {
            res = Pmc_module_version_get(SYS_PMC_NVRAM_MODULE_VER, &info_data->general.pmc_module_version);
            if (res != 0)
            {
                PMCLOG_1(PMCLOG_ERROR, "Pmc_module_version_get failed, error 0x%04x.\n", res);
                return (res);
            }
        }
        else
        {
            strcpy(info_data->general.pmc_module_version, "Not Installed");
        }


        /* API version */
        sprintf(info_data->general.api_version, "%s.%s.%s.%s", NVRAM_API_LIB_VER_MAJOR, NVRAM_API_LIB_VER_MINOR, NVRAM_API_LIB_VER_BUILD, NVRAM_API_LIB_MAINTENANCE);
        break;

    case NVRAM_INFO_GROUP_DMI:
    case NVRAM_INFO_GROUP_RAMDISK: /* defined in FW - nothing to do here */
    case NVRAM_INFO_GROUP_FLASH:   /* defined in FW - nothing to do here */
    case NVRAM_INFO_GROUP_TEMPERATURE: /* defined in FW - nothing to do here */
    case NVRAM_INFO_GROUP_DDR: /* defined in FW - nothing to do here */
    case NVRAM_INFO_GROUP_BACKUP_POWER:    /* defined in FW - nothing to do here */
    case NVRAM_INFO_GROUP_PCIE:    /* defined in FW - nothing to do here */
    case NVRAM_INFO_GROUP_DEBUG:   /* defined in FW - nothing to do here */


        break;

    default:
        PMCLOG_1(PMCLOG_ERROR, "Invalid info_group %d.\n", info_group);
        return (P_STATUS_LIB_BAD_PARAM);
        break;
    }

    return (P_STATUS_OK);
}


/* This func gets card UID */
static int Pmc_card_uid_get(Pmc_card_s * card)
{
    int res;
    dev_handle_t dev_handle;
    NVRAM_internal_data_u data;

    dev_handle.mod_dev_id = UNDEFINED_DEV_ID;

    /* open virtual file */
    dev_handle.nvme_fd = open(card->dev_path, O_RDONLY);
    if (dev_handle.nvme_fd < 0)
    {
        PMCLOG_3(PMCLOG_ERROR, "Failed to open '%s', errno %d (%s).\n", card->dev_path, errno, strerror(errno));
        return (P_STATUS_HOST_OS_ERROR);
    }

    /* get nvram card info */
    res = Pmc_internal_data_get(dev_handle, NVRAM_INTERNAL_DATA_GROUP_GENERAL, &data);
    if (res != P_STATUS_OK)
    {
        PMCLOG_1(PMCLOG_ERROR, "Pmc_internal_data_get failed, error 0x%04x.\n", res);
        close(dev_handle.nvme_fd);
        return (res);
    }

    close(dev_handle.nvme_fd);

    memcpy(card->card_uid, data.general.card_serial_number, sizeof(card->card_uid));

    return (P_STATUS_OK);
}

/* This func detects if the card is Mt Ramon card */
#ifdef OS_FREE_BSD
static int Pmc_is_mt_ramon_card(const uint32_t nvme_index, p_bool_t * is_mt_ramon)
{
	int fd,res;
	struct pci_conf_io mtr_pc;
	struct pci_conf mtr_conf[255], *p;
	struct pci_match_conf mtr_match_conf[1];
	(*is_mt_ramon) = PMC_FALSE;
	
	fd = open(DEV_PCI_PATH, O_RDONLY, 0);
	if (fd < 0)
	{
		
		PMCLOG_1(PMCLOG_ERROR, "ERROR: Could not open file %s!\n", DEV_PCI_PATH);
		return (P_STATUS_HOST_OS_ERROR);
	}
	/*preparing a match strcuture that contains the Mt ramon card info to match*/
	bzero(&mtr_pc, sizeof(struct pci_conf_io));
	mtr_pc.match_buf_len = sizeof(mtr_conf);
	mtr_pc.matches = mtr_conf;
	bzero(&mtr_match_conf, sizeof(mtr_match_conf));
	mtr_match_conf[0].pc_vendor = PMC_PCI_MT_RAMON_VENDOR_ID;
	mtr_match_conf[0].pc_device = PMC_PCI_MT_RAMON_DEVICE_ID;
	mtr_match_conf[0].flags = PCI_GETCONF_MATCH_VENDOR | PCI_GETCONF_MATCH_DEV;
	mtr_pc.num_patterns = 1;
	mtr_pc.pat_buf_len = sizeof(mtr_match_conf);
	mtr_pc.patterns = mtr_match_conf;
	/*Get the infomation on all Mt ramon cards connected*/
	if ((res=ioctl(fd, PCIOCGETCONF, &mtr_pc)) == -1)
	{
		PMCLOG_1(PMCLOG_ERROR, "pci ioctl failed, error 0x%04x.\n", res);
		return (P_STATUS_HOST_OS_ERROR);
	}
	
	/*Go over on all Mt ramon cards found and match the pd_unit to the nvme index*/
	for (p = mtr_conf; p < &mtr_conf[mtr_pc.num_matches]; p++)
	{
		if(p->pd_unit == nvme_index)
			 (*is_mt_ramon) = PMC_TRUE;
	}
	
	return (P_STATUS_OK);
	
}

#else/*Linux OS case*/
static int Pmc_is_mt_ramon_card(const uint32_t nvme_index, p_bool_t * is_mt_ramon)
{
    FILE *f_vendor,*f_device;
    char str[MAX_LINE_LEN],str1[MAX_PCI_FIELD_LEN],str2[MAX_PCI_FIELD_LEN];
    char mt_ramon_dev_lowercase[MAX_LINE_LEN];
    char vendor_path[MAX_PATH_LEN],device_path[MAX_PATH_LEN];
    
    (*is_mt_ramon) = PMC_FALSE;

    sprintf(vendor_path, "/sys/block/nvme%un1/device/vendor", nvme_index);
    sprintf(device_path, "/sys/block/nvme%un1/device/device", nvme_index);
    
    f_vendor = fopen(vendor_path, "r");
    if (f_vendor == NULL)
    {
    	/*change path in case of Linux OS 4.x and try again*/
    	sprintf(vendor_path, "/sys/block/nvme%un1/device/device/vendor", nvme_index);
    	sprintf(device_path, "/sys/block/nvme%un1/device/device/device", nvme_index);
    	f_vendor = fopen(vendor_path, "r");
    	 if (f_vendor == NULL)
    	 {
    		 PMCLOG_3(PMCLOG_ERROR, "Failed to open '%s', errno %d (%s).\n", vendor_path, errno, strerror(errno));
    		 return (P_STATUS_HOST_OS_ERROR);
    	 }
    }
    f_device = fopen(device_path, "r");
    if (f_device == NULL)
    {
       	PMCLOG_3(PMCLOG_ERROR, "Failed to open '%s', errno %d (%s).\n", device_path, errno, strerror(errno));
       	fclose(f_vendor);
       	return (P_STATUS_HOST_OS_ERROR);
    }
    /*creating a string to match the format of PMC_PCI_MT_RAMON_DEVICE*/
    fgets(str1, sizeof(str1), f_vendor);
    fgets(str2, sizeof(str2), f_device);
    sscanf(str1,"0x%s",str1);
    sscanf(str2,"0x%s",str2);
    sprintf(str,"%s:%s",str1,str2);
   
    /* convert PMC_PCI_MT_RAMON_DEVICE to lower */
    strcpy(mt_ramon_dev_lowercase, PMC_PCI_MT_RAMON_DEVICE);
    Pmc_str_to_lower(mt_ramon_dev_lowercase);
    /* check if [venodr:device] equals to [PMC:Mt_Ramon] (check both lowercase and uppercase str)*/
    if (strstr(str, mt_ramon_dev_lowercase) || strstr(str, PMC_PCI_MT_RAMON_DEVICE))
    {
    	(*is_mt_ramon) = PMC_TRUE;
    }


    fclose(f_vendor);
    fclose(f_device);
    return (P_STATUS_OK);
}
#endif

/* This func retrieves list of NVRAM cards */
static int Pmc_card_list_get(Pmc_card_s card_list[],
                             const uint16_t card_list_size, uint16_t * card_list_count, uint16_t * card_total_count)
{
    int res;
    DIR *d;
    struct dirent *dir;
    int dev, ns;
    Pmc_card_s card;
    p_bool_t is_mt_ramon;

   
    (*card_list_count) = 0;
    (*card_total_count) = 0;

    memset(card_list, 0, card_list_size * sizeof(Pmc_card_s));

    d = opendir(DEV_PATH);
    if (d)
    {
        while ((dir = readdir(d)) != NULL)
        {
#ifdef OS_FREE_BSD
        	if (sscanf(dir->d_name, "nvme%dns%d", &dev, &ns) == 2)   /* try to parse as block device file 'nvmeXnsY' */
#else /*Linux OS case*/
            if (sscanf(dir->d_name, "nvme%dn%d", &dev, &ns) == 2)   /* try to parse as block device file 'nvmeXnY' */
#endif/*End Linux OS case*/   
            {
                /* 
                   we parse "nvme%dn%d" FIRST otherwise the below parsing of "nvme%d" will parse it! and we will get a
                   wrong virtual file. */
            	PMCLOG_3(PMCLOG_DEBUG, "%s: dev=%d  ns=%d.\n", dir->d_name, dev, ns);
            }
            else if (sscanf(dir->d_name, "nvme%d", &dev) == 1)  /* try to parse as character device file 'nvmeX' */
            {
                PMCLOG_2(PMCLOG_DEBUG, "%s: dev=%d.\n", dir->d_name, dev);

                card.nvme_index = (uint32_t) dev;
                sprintf(card.dev_path, "%s%s", DEV_PATH, dir->d_name);
                /*setting the block device path - only one name space is supported*/
#ifdef OS_FREE_BSD
                sprintf(card.block_dev_path, "%snvme%dns1", DEV_PATH,card.nvme_index);
#else
                sprintf(card.block_dev_path, "%snvme%dn1", DEV_PATH,card.nvme_index);
#endif
                
                /* check if the nvme card is Mt Ramon */
                res = Pmc_is_mt_ramon_card(card.nvme_index, &is_mt_ramon);
                if (res != P_STATUS_OK)
                {
                    PMCLOG_2(PMCLOG_ERROR, "Pmc_is_mt_ramon_card for dev '%s' has failed, error 0x%04x.\n",
                             card.dev_path, res);
                    return res;
                }

                if (is_mt_ramon)
                {
                    res = Pmc_card_uid_get(&card);
                    if (res != P_STATUS_OK)
                    {
                        PMCLOG_2(PMCLOG_ERROR, "Pmc_card_uid_get for dev file '%s' has failed, error 0x%04x.\n",
                                 card.dev_path, res);
                        return res;
                    }

                    (*card_total_count)++;  /* increase total count */

                    if ((*card_list_count) < card_list_size)    /* if there is more free entries in output buffer */
                    {
                        memcpy(&card_list[(*card_list_count)++], &card, sizeof(Pmc_card_s));
                    }
                }
            }
        }

        closedir(d);
    }

    return (P_STATUS_OK);
}


static int Pmc_config_get(const dev_handle_t dev_handle,
                          const NVRAM_config_type_e config_type, NVRAM_config_data_u * config_data)
{
    int res;
    char prp[PRP_SIZE];
    nvme_arg_s arg;


    /* check params */
    CHECK_BOUNDS(config_type, NVRAM_CONFIG_TYPE_FIRST, NVRAM_CONFIG_TYPE_NUM - 1);
    CHECK_PTR(config_data);



    bzero(prp, sizeof(prp));
    bzero(config_data, sizeof(NVRAM_config_data_u));

    bzero(&arg, sizeof(nvme_arg_s));
    arg.nvme_cmd.header.opCode = ADMIN_OPCODE_VENDOR_NVRAM; /* NVRAM command */
    arg.nvme_cmd.header.namespaceID = 0;    /* Namespace ID not in use */
    arg.nvme_cmd.header.prp1 = (uint64_t) prp;  /* PRP 1 (Output data) */
    arg.data_length = sizeof(prp);  /* PRP data len in Dwords */
    arg.nvme_cmd.cmd.vendorSpecific.vndrCDW12 = NVRAM_VENDOR_SUBCMD_CONFIG_GET; /* Vendor Specific Config Get command */
    arg.nvme_cmd.cmd.vendorSpecific.vndrCDW13 = (uint32_t) config_type; /* Vendor Specific Config Type */


    /* send command to device */
    res = pmc_ioctl(dev_handle, NVME_IOCTL_ADMIN_CMD, &arg);
    if (res != 0)
    {
        PMCLOG_1(PMCLOG_ERROR, "pmc_ioctl failed, error 0x%04x.\n", res);
        return (res);
    }

    /* copy the returned PRP data to user struct */
    memcpy(config_data, prp, sizeof(NVRAM_config_data_u));

    return (P_STATUS_OK);
}


static int Pmc_fw_image_activate(const dev_handle_t devHandle, uint32_t fw_slot, NVRAM_fw_activate_action_e action)
{
    int res;
    nvme_arg_s arg;

    bzero(&arg, sizeof(nvme_arg_s));
    arg.nvme_cmd.header.opCode = ADMIN_OPCODE_FW_ACTIVATE;  /* NVRAM command */
    arg.nvme_cmd.header.namespaceID = 0;    /* Namespace ID not in use */
    arg.nvme_cmd.dw[10] = (fw_slot & 0x7) | ((action & 0x3) << 3);; /* Set data size and make it 0's based to compliant 
                                                                       with 1.0c */

    /* send command to device */
    res = pmc_ioctl(devHandle, NVME_IOCTL_ADMIN_CMD, &arg);
#if 0
    // Check to ignore the error like:
    // SCT = 1 (Command Specific Error)
    // SC = 0xB (Firmware Application Requires Conventional Reset)
    if (((res & CPL_SCT_MASK) >> 25) == SF_SCT_CMD_SPC_ERR &&
        ((res & CPL_SC_MASK) >> 17) == SC_CMD_SPC_ERR_FW_ACT_REQ_RESET)
    {
        res = res & ~(CPL_SC_MASK | CPL_SCT_MASK);
    }
#endif

    if (res != 0)
    {
        PMCLOG_1(PMCLOG_ERROR, "pmc_ioctl failed, error 0x%04x.\n", res);
        return (res);
    }

    return (P_STATUS_OK);

}

static int Pmc_fw_image_download_chunk(const dev_handle_t devHandle, uint32_t byteSize, uint32_t byteOffset, void *data)
{
    int res;
    char prp[PRP_SIZE];
    nvme_arg_s arg;
    uint32_t dataTxSize = 0;

    /* Sanity test on byteSize */
    if (byteSize > PRP_SIZE)
    {
        return (P_STATUS_LIB_TOO_BIG);
    }


    /* Need to round up to number of dwords */
    dataTxSize = byteSize;
    if (byteSize & 3)
    {
        dataTxSize += 4;
        dataTxSize &= ~(3);
    }

    /* Clear PRP before usage */
    bzero(prp, PRP_SIZE);

    /* Copy data to PRP */
    memcpy(prp, data, dataTxSize);

    bzero(&arg, sizeof(nvme_arg_s));
    arg.nvme_cmd.header.opCode = ADMIN_OPCODE_FW_DOWNLOAD;  /* NVRAM command */
    arg.nvme_cmd.header.namespaceID = 0;    /* Namespace ID not in use */
    arg.nvme_cmd.header.prp1 = (uint64_t) prp;  /* PRP 1 (Input data) */
    arg.data_length = dataTxSize;   /* Set data length */
    arg.nvme_cmd.dw[10] = (dataTxSize >> 2) - 1;    /* Set data size and make it 0's based to compliant with 1.0c */
    arg.nvme_cmd.dw[11] = byteOffset >> 2;  /* Set data offset */

    /* send command to device */
    res = pmc_ioctl(devHandle, NVME_IOCTL_ADMIN_CMD, &arg);
    if (res != 0)
    {
        PMCLOG_1(PMCLOG_ERROR, "pmc_ioctl failed, error 0x%04x.\n", res);
        return (res);
    }

    return (P_STATUS_OK);

}


static int Pmc_sbl_image_download_chunk(const dev_handle_t devHandle, uint32_t byteSize, uint32_t byteOffset, void *data)
{
    int res;
    char prp[PRP_SIZE];
    nvme_arg_s arg;
    uint32_t dataTxSize = 0;

    /* Sanity test on byteSize */
    if (byteSize > PRP_SIZE)
    {
        return (P_STATUS_LIB_TOO_BIG);
    }

    /* Need to round up to number of dwords */
    dataTxSize = byteSize;
    if (byteSize & 3)
    {
        dataTxSize += 4;
        dataTxSize &= ~(3);
    }

    /* Clear PRP before usage */
    bzero(prp, PRP_SIZE);

    /* Copy data to PRP */
    memcpy(prp, data, dataTxSize);

    bzero(&arg, sizeof(nvme_arg_s));
    arg.nvme_cmd.header.opCode = ADMIN_OPCODE_VENDOR_SYS_CONFIG;  /* NVRAM command */
    arg.nvme_cmd.header.namespaceID = 0;    /* Namespace ID not in use */
    arg.nvme_cmd.header.prp1 = (uint64_t) prp;  /* PRP 1 (Output data) */
    arg.data_length = sizeof(prp);  /* PRP data len in Dwords */
    arg.nvme_cmd.cmd.vendorSpecific.vndrCDW12 = SYS_CONFIG_DW_EEPROM_IMAGE;  /* Download Eeprom image command */
    arg.nvme_cmd.dw[10] = (dataTxSize >> 2) - 1;    /* Set data size and make it 0's based to compliant with 1.0c */
    arg.nvme_cmd.dw[13] = byteOffset >> 2;  /* Set data offset */


    /* send command to device */
    res = pmc_ioctl(devHandle, NVME_IOCTL_ADMIN_CMD, &arg);
    if (res != 0)
    {
        PMCLOG_1(PMCLOG_ERROR, "pmc_ioctl failed, error 0x%04x.\n", res);
        return (res);
    }

    return (P_STATUS_OK);

}



P_STATUS Pmc_image_download_from_file(const dev_handle_t dev_handle, Pmc_image_type_e image_type, const void *image_path)
{
    FILE *inFile;
    uint8_t buffer[PRP_SIZE];
    uint32_t size = 0;
    uint32_t offset = 0;
    P_STATUS status = P_STATUS_OK;


    /* open file for read */
    if ((inFile = fopen(image_path, "r")) == NULL)
    {
        PMCLOG_1(PMCLOG_ERROR, "ERROR: Could not open file %s!\n", image_path);
        return (P_STATUS_LIB_GEN_ERROR);
    }

    /* Send fw download chunk by chunk */
    while ((size = fread(buffer, 1, PRP_SIZE, inFile)))
    {
        // PMCLOG_2(PMCLOG_ERROR, "len %d, offset %d\n",size, offset);
        // printf("%s\n",buffer);

        // Issue the command now
        if(image_type == PMC_IMAGE_TYPE_FW)
            status = Pmc_fw_image_download_chunk(dev_handle, size, offset, (void *)buffer);
        else if(image_type == PMC_IMAGE_TYPE_SBL)
            status = Pmc_sbl_image_download_chunk(dev_handle, size, offset, (void *)buffer);
        else
        {
            PMCLOG_1(PMCLOG_ERROR, "Wrong image type %d\n", image_type);
            return (P_STATUS_LIB_BAD_PARAM);
        }
        if (status != P_STATUS_OK)
        {
            PMCLOG_1(PMCLOG_ERROR, "Image Download command fails, library returns error=%d\n", status);
            fclose(inFile);
            return (status);
        }
        offset = offset + size;
    }

    fclose(inFile);

    return (P_STATUS_OK);
}



P_STATUS Pmc_image_download_from_mem(const dev_handle_t dev_handle, Pmc_image_type_e image_type,
                                        const void *image, const uint32_t image_size)
{
    uint32_t size = PRP_SIZE;
    uint32_t offset = 0;
    P_STATUS status = P_STATUS_OK;

    /* Download FW chunk by chunk */
    for (offset = 0; offset < image_size; offset += PRP_SIZE)
    {
        /* In case it is the last chunk */
        if ((offset + PRP_SIZE) > image_size)
        {
            size = image_size - offset;
        }

        // Issue the command now
        if(image_type == PMC_IMAGE_TYPE_FW)
            status = Pmc_fw_image_download_chunk(dev_handle, size, offset, (void *)(image + offset));
        else if(image_type == PMC_IMAGE_TYPE_SBL)
            status = Pmc_sbl_image_download_chunk(dev_handle, size, offset, (void *)(image + offset));
        else
        {
            PMCLOG_1(PMCLOG_ERROR, "Wrong image type %d\n", image_type);
            return (P_STATUS_LIB_BAD_PARAM);
        }
        if (status != P_STATUS_OK)
        {
            PMCLOG_1(PMCLOG_ERROR, "Image Download command fails, library returns error=%d\n", status);
        }
    }

    return (P_STATUS_OK);
}






/**********************************************************/
/* NVRAM APIs */
/**********************************************************/


/*====================================================*/
/* API: PMC_NVRAM_system_config_set */
/*====================================================*/
P_STATUS PMC_NVRAM_system_config_set(const NVRAM_system_config_type_e config_type, const NVRAM_system_config_data_u *config_data)
{
    /* check params */
    CHECK_BOUNDS(config_type, NVRAM_SYSTEM_CONFIG_TYPE_FIRST, NVRAM_SYSTEM_CONFIG_TYPE_NUM - 1);
    CHECK_PTR(config_data);

    switch ((NVRAM_system_config_type_e)config_type)
    {
    case NVRAM_SYSTEM_CONFIG_TYPE_THREAD_MODE:
        /* Check if any API already called */
        if (g_any_api_already_called)
        {
            PMCLOG_0(PMCLOG_ERROR, "THREAD_MODE must be set before any other API call.\n");
            return (P_STATUS_LIB_ALREADY_EXIST);
        }

        CHECK_BOUNDS(config_data->thread_mode, NVRAM_OPERATION_MODE_AUTO, NVRAM_OPERATION_MODE_USER);
        g_thread_mode = config_data->thread_mode;
        break;

    case NVRAM_SYSTEM_CONFIG_TYPE_SEMAPHORE_MODE:
        /* Check if any API already called */
        if (g_any_api_already_called)
        {
            PMCLOG_0(PMCLOG_ERROR, "SEMAPHORE_MODE must be set before any other API call.\n");
            return (P_STATUS_LIB_ALREADY_EXIST);
        }

        CHECK_BOUNDS(config_data->semaphore_mode, NVRAM_OPERATION_MODE_AUTO, NVRAM_OPERATION_MODE_USER);
        g_semaphore_mode = config_data->semaphore_mode;
        break;

    default:
        PMCLOG_1(PMCLOG_ERROR, "Invalid config_type.\n", config_type);
        return (P_STATUS_LIB_BAD_PARAM);
    }

    return (P_STATUS_OK);
}


/*====================================================*/
/* API: PMC_NVRAM_system_config_get */
/*====================================================*/
P_STATUS PMC_NVRAM_system_config_get(const NVRAM_system_config_type_e config_type, NVRAM_system_config_data_u *config_data)
{
    /* check params */
    CHECK_BOUNDS(config_type, NVRAM_SYSTEM_CONFIG_TYPE_FIRST, NVRAM_SYSTEM_CONFIG_TYPE_NUM - 1);
    CHECK_PTR(config_data);

    switch ((NVRAM_system_config_type_e)config_type)
    {
    case NVRAM_SYSTEM_CONFIG_TYPE_THREAD_MODE:
        config_data->thread_mode = g_thread_mode;
        break;

    case NVRAM_SYSTEM_CONFIG_TYPE_SEMAPHORE_MODE:
        config_data->semaphore_mode = g_semaphore_mode;
        break;

    default:
        PMCLOG_1(PMCLOG_ERROR, "Invalid config_type.\n", config_type);
        return (P_STATUS_LIB_BAD_PARAM);
    }

    return (P_STATUS_OK);
}


/*====================================================*/
/* API: PMC_NVRAM_card_list_get */
/*====================================================*/
P_STATUS PMC_NVRAM_card_list_get(NVRAM_card_s card_list[],
                                 const uint16_t card_list_size, uint16_t * card_list_count, uint16_t * card_total_count)
{
    int res;
    int i;
    Pmc_card_s pmc_card_list[MAX_NUM_DEVICES];

    /* check params */
    CHECK_PTR(card_list_count);
    CHECK_PTR(card_total_count);

    if (card_list_count == card_total_count)
    {
        PMCLOG_0(PMCLOG_ERROR, "Error, card_list_count and card_total_count parameters both pointing to the same variable.\n");
        return (P_STATUS_LIB_BAD_PARAM);
    }

    /* Indicates that any API already called */
    g_any_api_already_called = PMC_TRUE;

    /* check if NVMe driver is installed */
    res = Pmc_check_module(NVME_DRIVER, PMC_TRUE);
    if (res == P_STATUS_LIB_NOT_EXIST)
    {
        PMCLOG_0(PMCLOG_ERROR, "NVMe driver is not installed.\n");
        return (res);
    }
    else if (res != P_STATUS_OK)
    {
        PMCLOG_1(PMCLOG_ERROR, "Failed to find NVMe driver, error 0x%04x.\n", res);
        return (res);
    }
	
    /*setting the type of nvme driver: PMC or community*/
    Pmc_nvme_driver_detect(SYS_NVME_MODULE_VER);
       
    if (card_list_size > MAX_NUM_DEVICES)
    {
        PMCLOG_2(PMCLOG_ERROR, "Error, card_list_size(%u) cannot be bigger than MAX_NUM_DEVICES(%u), if need please increase the value of MAX_NUM_DEVICES.\n", card_list_size, MAX_NUM_DEVICES);
        return (P_STATUS_LIB_BAD_PARAM);
    }

    res = Pmc_card_list_get(pmc_card_list, card_list_size, card_list_count, card_total_count);
    if (res != P_STATUS_OK)
    {
        PMCLOG_1(PMCLOG_ERROR, "Pmc_card_list_get failed, error 0x%04x.\n", res);
        return (res);
    }

    for (i = 0; i < (*card_list_count); i++)
    {
        memcpy(card_list[i].card_uid, pmc_card_list[i].card_uid, sizeof(card_list[i].card_uid));
        card_list[i].nvme_index = pmc_card_list[i].nvme_index;
    }

    return (P_STATUS_OK);
}


/*====================================================*/
/* API: PMC_NVRAM_init */
/*====================================================*/
P_STATUS PMC_NVRAM_init(const char card_uid[], dev_handle_t * dev_handle)
{
    int res;
    int i;
    p_bool_t found = PMC_FALSE;
    Pmc_card_s pmc_card_list[MAX_NUM_DEVICES];
    uint16_t card_list_count;
    uint16_t card_total_count;

    /* check params */
    CHECK_PTR(dev_handle);

    /* Indicates that any API already called */
    g_any_api_already_called = PMC_TRUE;


    dev_handle->nvme_fd = UNDEFINED_FD;
    dev_handle->mod_dev_id = UNDEFINED_DEV_ID;
    dev_handle->nvme_block_fd = UNDEFINED_FD;
      
    /*setting the type of nvme driver: PMC or community (need to be done in init function as well,
     * in case PMC_NVRAM_card_list_get was not called)*/
    Pmc_nvme_driver_detect(SYS_NVME_MODULE_VER);

    /* init API semaphore only once */
    if (g_thread_mode == NVRAM_OPERATION_MODE_AUTO) // AUTO thread mode
    {
        pmc_init_once_status = P_STATUS_OK;
        res = OSSRV_thread_once(Pmc_init_once);
        if (res < 0)
        {
            PMCLOG_1(PMCLOG_ERROR, "pthread_once failed, error %d.\n", res);
            return (P_STATUS_LIB_GEN_ERROR);
        }
    }
    else // USER thread mode
    {
        /* USER mode is not protected by thread_once mechanism, instead we use a static flag to run it only once */
        static p_bool_t firstTime = PMC_TRUE;
        if (firstTime)
        {
            firstTime = PMC_FALSE;
            Pmc_init_once();
        }
    }


	/* check the value which defined in Pmc_init_once/OSSRV_thread_once */
    if (pmc_init_once_status != P_STATUS_OK)
    {
        PMCLOG_1(PMCLOG_ERROR, "pmc_init_once failed, error %d.\n", pmc_init_once_status);
        return (pmc_init_once_status);
    }

    /* check if NVMe driver is installed */
    res = Pmc_check_module(NVME_DRIVER, PMC_TRUE);
    if (res == P_STATUS_LIB_NOT_EXIST)
    {
        PMCLOG_0(PMCLOG_ERROR, "NVMe driver is not installed.\n");
        return (P_STATUS_LIB_DRIVER_NOT_FOUND);
    }
    else if (res != P_STATUS_OK)
    {
        PMCLOG_1(PMCLOG_ERROR, "Failed to find NVMe driver, error 0x%04x.\n", res);
        return (res);
    }


    /* get card list */
    res = Pmc_card_list_get(pmc_card_list, MAX_NUM_DEVICES, &card_list_count, &card_total_count);
    if (res != P_STATUS_OK)
    {
        PMCLOG_1(PMCLOG_ERROR, "Pmc_card_list_get failed, error 0x%04x.\n", res);
        return (res);
    }

    if (card_total_count == 0)
    {
        PMCLOG_0(PMCLOG_ERROR, "No NVRAM card found.\n");
        return (P_STATUS_LIB_CARD_NOT_FOUND);
    }

    /* find our card in the card list */
    for (i = 0; i < card_list_count; i++)
    {
        if (strcasecmp(card_uid, pmc_card_list[i].card_uid) == 0)   /* card found */
        {
            /* open device virtual file */
            dev_handle->nvme_fd = open(pmc_card_list[i].dev_path, O_RDWR);
            if (dev_handle->nvme_fd < 0)
            {
                PMCLOG_3(PMCLOG_ERROR, "Failed to open '%s', errno %d (%s).\n", pmc_card_list[i].dev_path, errno, strerror(errno));
                return (P_STATUS_HOST_OS_ERROR);
            }

            dev_handle->nvme_block_fd = open(pmc_card_list[i].block_dev_path, O_RDWR);
            if (dev_handle->nvme_block_fd < 0)
            {
            	close(dev_handle->nvme_fd);
            	PMCLOG_3(PMCLOG_ERROR, "Failed to open '%s', errno %d (%s).\n", pmc_card_list[i].block_dev_path, errno, strerror(errno));
            	return (P_STATUS_HOST_OS_ERROR);
            }


            /* 
             * one need to update the blk_dev_stat to save there the major and minor numbers,
             * so later on one will be able to get sysfs entry that holds the logical block size
             *
             */
            if (lstat(pmc_card_list[i].block_dev_path, &(dev_handle->device_info.blk_dev_stat)) < 0)
            {
            	close(dev_handle->nvme_fd);
            	close(dev_handle->nvme_block_fd);     
            	PMCLOG_2(PMCLOG_ERROR, "lstat failed, error %d (%s).\n", errno, strerror(errno));
                return (P_STATUS_HOST_OS_ERROR);
            }

            found = PMC_TRUE;
            break;
        }
    }

    /* if not found */
    if (!found)
    {
        PMCLOG_1(PMCLOG_ERROR, "NVRAM card with UID '%s' not found.\n", card_uid);
        return (P_STATUS_LIB_CARD_NOT_FOUND);
    }

    /* Set Host events polling interval as FW */
    API_SEM_TAKE_RET_ERR(g_api_sem);
    if (g_is_polling_interval_initialized == PMC_FALSE) /* do only once */
    {
        NVRAM_internal_data_u internal_data;
        res = Pmc_sync_version(*dev_handle);
        if (res != 0)
        {
        	close(dev_handle->nvme_fd);
        	close(dev_handle->nvme_block_fd);  
            API_SEM_GIVE_RET_ERR(g_api_sem);
            PMCLOG_1(PMCLOG_ERROR, "FW and API versions are not compatible, error 0x%04x.\n", res);
            return (res);
        }
        res = Pmc_internal_data_get(*dev_handle, NVRAM_INTERNAL_DATA_GROUP_HEARTBEAT, &internal_data);
        if (res != 0)
        {
        	close(dev_handle->nvme_fd);
        	close(dev_handle->nvme_block_fd);  
            API_SEM_GIVE_RET_ERR(g_api_sem);
            PMCLOG_1(PMCLOG_ERROR, "Failed to read heartbit message interval, error 0x%04x.\n", res);
            return (res);
        }
        g_heartbeat_interval = internal_data.heartbeat.heartbeat_msg_interval;

        g_is_polling_interval_initialized = PMC_TRUE;
    }
    API_SEM_GIVE_RET_ERR(g_api_sem);

    return (P_STATUS_OK);
}


/*====================================================*/
/* API: PMC_NVRAM_release */
/*====================================================*/
P_STATUS PMC_NVRAM_release(const dev_handle_t dev_handle)
{
    int res;


    /* terminate events package */
    res = Pmc_event_db_dev_del(dev_handle);
    if (res != 0)
    {
        PMCLOG_1(PMCLOG_ERROR, "Pmc_event_db_dev_del failed.\n", res);
        return (res);
    }


    /* close device virtual file */
    res = close(dev_handle.nvme_fd);
    if (res < 0)
    {
        PMCLOG_2(PMCLOG_ERROR, "Failed to close '/dev/nvme...' file, errno %d (%s).\n", errno, strerror(errno));
        return (P_STATUS_HOST_OS_ERROR);
    }
    
    /* close device virtual file */
    res = close(dev_handle.nvme_block_fd);
    if (res < 0)
    {
    	PMCLOG_2(PMCLOG_ERROR, "Failed to close '/dev/nvme...' block device file, errno %d (%s).\n", errno, strerror(errno));
    	return (P_STATUS_HOST_OS_ERROR);
    }

    return (P_STATUS_OK);
}


/*====================================================*/
/* API: PMC_NVRAM_master_authenticate */
/*====================================================*/
P_STATUS PMC_NVRAM_master_authenticate(const dev_handle_t dev_handle, const char auth_key[])
{
    int res;
    char prp[PRP_SIZE];
    nvme_arg_s arg;


    bzero(prp, sizeof(prp));
    memcpy(prp, auth_key, NVRAM_AUTH_KEY_SIZE);

    bzero(&arg, sizeof(nvme_arg_s));
    arg.nvme_cmd.header.opCode = ADMIN_OPCODE_VENDOR_NVRAM; /* NVRAM command */
    arg.nvme_cmd.header.namespaceID = 0;    /* Namespace ID not in use */
    arg.nvme_cmd.header.prp1 = (uint64_t) prp;  /* PRP 1 (Input data) */
    arg.data_length = sizeof(prp);  /* PRP data len in Dwords */
    arg.nvme_cmd.cmd.vendorSpecific.vndrCDW12 = NVRAM_VENDOR_SUBCMD_AUTHENTICATE_MASTER;    /* Vendor Specific
                                                                                               Authenticate command */

    /* send command to device */
    res = pmc_ioctl(dev_handle, NVME_IOCTL_ADMIN_CMD, &arg);
    if (res != 0)
    {
        PMCLOG_1(PMCLOG_ERROR, "pmc_ioctl failed, error 0x%04x.\n", res);
        return (res);
    }

    return (P_STATUS_OK);
}


/*====================================================*/
/* API: PMC_NVRAM_admin_authenticate */
/*====================================================*/
P_STATUS PMC_NVRAM_admin_authenticate(const dev_handle_t dev_handle, const char auth_key[])
{
    int res;
    char prp[PRP_SIZE];
    nvme_arg_s arg;


    bzero(prp, sizeof(prp));
    memcpy(prp, auth_key, NVRAM_AUTH_KEY_SIZE);

    bzero(&arg, sizeof(nvme_arg_s));
    arg.nvme_cmd.header.opCode = ADMIN_OPCODE_VENDOR_NVRAM; /* NVRAM command */
    arg.nvme_cmd.header.namespaceID = 0;    /* Namespace ID not in use */
    arg.nvme_cmd.header.prp1 = (uint64_t) prp;  /* PRP 1 (Input data) */
    arg.data_length = sizeof(prp);  /* PRP data len in Dwords */
    arg.nvme_cmd.cmd.vendorSpecific.vndrCDW12 = NVRAM_VENDOR_SUBCMD_AUTHENTICATE_ADMIN; /* Vendor Specific Authenticate 
                                                                                           command */

    /* send command to device */
    res = pmc_ioctl(dev_handle, NVME_IOCTL_ADMIN_CMD, &arg);
    if (res != 0)
    {
        PMCLOG_1(PMCLOG_ERROR, "pmc_ioctl failed, error 0x%04x.\n", res);
        return (res);
    }

    return (P_STATUS_OK);
}


/*====================================================*/
/* API: PMC_NVRAM_info_get */
/*====================================================*/
P_STATUS PMC_NVRAM_info_get(const dev_handle_t dev_handle,
                            const NVRAM_info_group_e info_group, NVRAM_info_data_u * info_data)
{
    int res;

    /* check params */
    CHECK_BOUNDS(info_group, NVRAM_INFO_GROUP_FIRST, NVRAM_INFO_GROUP_NUMBER - 1);
    CHECK_PTR(info_data);

    /* 
     * Return this line (these lines when software patch of the watchdog mechanism will be implemented in FW
     */
    if (info_group == NVRAM_INFO_GROUP_DEBUG)
    {
        res = P_STATUS_LIB_UNSUPPORTED;
        PMCLOG_1(PMCLOG_ERROR, "Pmc_info_get NVRAM_INFO_GROUP_DEBUG is currently unsupported, error 0x%04x.\n", res);
        return (res);
    }


    res = Pmc_info_get(dev_handle, info_group, info_data);
    if (res != P_STATUS_OK)
    {
        PMCLOG_1(PMCLOG_ERROR, "Pmc_info_get failed, error 0x%04x.\n", res);
        return (res);
    }

    return (P_STATUS_OK);
}


/*====================================================*/
/* API: PMC_NVRAM_status_get */
/*====================================================*/
P_STATUS PMC_NVRAM_status_get(const dev_handle_t dev_handle,
                              const NVRAM_status_group_e status_group, NVRAM_status_data_u * status_data)
{
    int res;
    char prp[PRP_SIZE];
    nvme_arg_s arg;
    uint32_t repeat_count = 0;
    uint32_t i = 0;

    /* check params */
    CHECK_BOUNDS(status_group, NVRAM_STATUS_GROUP_FIRST, NVRAM_STATUS_GROUP_NUMBER - 1);
    CHECK_PTR(status_data);


    /* 
     * Return this line (these lines when software patch of the watchdog mechanism will be implemented in FW
     */
    if (status_group == NVRAM_STATUS_GROUP_DEBUG)
    {
        res = P_STATUS_LIB_UNSUPPORTED;
        PMCLOG_1(PMCLOG_ERROR, "Pmc_info_get NVRAM_STATUS_GROUP_DEBUG is currently unsupported, error 0x%04x.\n", res);
        return (res);
    }

    bzero(prp, sizeof(prp));
    bzero(status_data, sizeof(NVRAM_status_data_u));

    bzero(&arg, sizeof(nvme_arg_s));
    arg.nvme_cmd.header.opCode = ADMIN_OPCODE_VENDOR_NVRAM; /* NVRAM command */
    arg.nvme_cmd.header.namespaceID = 0;    /* Namespace ID not in use */
    arg.nvme_cmd.header.prp1 = (uint64_t) prp;  /* PRP 1 (Output data) */
    arg.data_length = sizeof(prp);  /* PRP data len in Dwords */
    arg.nvme_cmd.cmd.vendorSpecific.vndrCDW12 = NVRAM_VENDOR_SUBCMD_STATUS_GET; /* Vendor Specific Info Get command */
    arg.nvme_cmd.cmd.vendorSpecific.vndrCDW13 = (uint32_t) status_group;    /* Vendor Specific Info Group */

    repeat_count = GET_POWER_STATUS_REPEAT_COUNT;

    do
    {
        /* Supress the internal print of NVME error except the last retry */
        if( i < (repeat_count-1))
            arg.do_not_print_err = 1;
        else
            arg.do_not_print_err = 0;
			
        /* send command to device */
        res = pmc_ioctl(dev_handle, NVME_IOCTL_ADMIN_CMD, &arg);
        /* In case of BACKUP_POWER and failur - sleep and retry */
        if( res != 0)
		{
           OSSRV_wait(GET_POWER_STATUS_REPEAT_DELAY);
		}

    } while ((++i < repeat_count) && (res != 0));

    if (res != 0)
    {
        PMCLOG_1(PMCLOG_ERROR, "pmc_ioctl failed, error 0x%04x.\n", res);
        return (res);
    }

    /* copy the returned PRP to the user status_data structure */
    memcpy(status_data, prp, sizeof(NVRAM_status_data_u));

    return (P_STATUS_OK);
}



/*====================================================*/
/* API: PMC_NVRAM_mem_map */
/*====================================================*/
P_STATUS PMC_NVRAM_mem_map(const dev_handle_t dev_handle, const uint64_t size, const int flags, void **virtual_address)
{
    int res;
    void *user_addr;
    NVRAM_info_data_u info_data;
    int prot;


    /* check params */
    CHECK_PTR(virtual_address);
    CHECK_BOUNDS(flags, NVRAM_MEM_MAP_FLAGS_DATA_NONE,  /* min val */
                 NVRAM_MEM_MAP_FLAGS_DATA_NONE | NVRAM_MEM_MAP_FLAGS_DATA_READ | NVRAM_MEM_MAP_FLAGS_DATA_WRITE | NVRAM_MEM_MAP_FLAGS_DATA_EXEC);   /* max 
                                                                                                                                                       val 
                                                                                                                                                     */

    (*virtual_address) = NULL;


    /* set prot value */
    prot = 0;
    if (flags & NVRAM_MEM_MAP_FLAGS_DATA_NONE)
        prot |= PROT_NONE;

    if (flags & NVRAM_MEM_MAP_FLAGS_DATA_READ)
        prot |= PROT_READ;

    if (flags & NVRAM_MEM_MAP_FLAGS_DATA_WRITE)
        prot |= PROT_WRITE;

    if (flags & NVRAM_MEM_MAP_FLAGS_DATA_EXEC)
        prot |= PROT_EXEC;


    res = PMC_NVRAM_info_get(dev_handle, NVRAM_INFO_GROUP_DMI, &info_data);
    if (res != P_STATUS_OK)
    {
        PMCLOG_1(PMCLOG_ERROR, "PMC_NVRAM_info_get failed, error 0x%04x.\n", res);
        return (res);
    }


    /* check DMI size */
    if (size > info_data.dmi.size)
    {
        PMCLOG_2(PMCLOG_ERROR, "Error, Requested DMI size (%llu bytes) is too big, cannot be more than %llu bytes.\n",
                 size, info_data.dmi.size);
        return (P_STATUS_LIB_TOO_BIG);
    }

    /* Open PMC NVRAM module if not opened yet */
    /* (protect global variable, g_module_handle, with semaphore) */
    API_SEM_TAKE_RET_ERR(g_api_sem);
    if (g_module_handle < 0)
    {
        /* check if PMC NVRAM module is installed */
        res = Pmc_check_module(PMC_NVRAM_MOD, PMC_TRUE);
        if (res == P_STATUS_LIB_NOT_EXIST)
        {
            API_SEM_GIVE_RET_ERR(g_api_sem);
            PMCLOG_0(PMCLOG_ERROR, "pmc_nvram module is not installed.\n");
            return (P_STATUS_LIB_PMC_MODULE_NOT_FOUND);
        }
        else if (res != P_STATUS_OK)
        {
            API_SEM_GIVE_RET_ERR(g_api_sem);
            PMCLOG_1(PMCLOG_ERROR, "Failed to find pmc_nvram module, error 0x%04x.\n", res);
            return (res);
        }

        /* open the module */
        g_module_handle = open(PMC_NVRAM_MOD_PATH, O_RDWR);
        if (g_module_handle < 0)
        {
            API_SEM_GIVE_RET_ERR(g_api_sem);
            PMCLOG_3(PMCLOG_ERROR, "Failed to open '%s', errno %d (%s).\n", PMC_NVRAM_MOD_PATH, errno, strerror(errno));
            return (P_STATUS_HOST_OS_ERROR);
        }
    }
    API_SEM_GIVE_RET_ERR(g_api_sem);

    /* map memory */
    user_addr = mmap(NULL, size, prot, MAP_SHARED, g_module_handle, (off_t) (void *)(info_data.dmi.physical_addres));

    if (user_addr == MAP_FAILED)
    {
        PMCLOG_2(PMCLOG_ERROR, "mmap failed, errno %d (%s).\n", errno, strerror(errno));
        return (P_STATUS_HOST_OS_ERROR);
    }


    (*virtual_address) = user_addr;

    return (P_STATUS_OK);
}


/*====================================================*/
/* API: PMC_NVRAM_fw_download */
/*====================================================*/
P_STATUS PMC_NVRAM_fw_download(const dev_handle_t dev_handle,
                               const uint8_t fw_slot_index,
                               const NVRAM_fw_source_e fw_source, const void *fw_image, const uint32_t fw_image_size)
{
#define NVME_FW_SLOT(x) ((fw_slot_index  == NVRAM_FW_SLOT_0)?NVRAM_FW_SLOT_1 :fw_slot_index)
    NVRAM_fw_activate_action_e action = NVRAM_FW_ACTION_ACTIVATE;   /* Set default action to activate */
    P_STATUS status = P_STATUS_OK;


    /* =============================================== */
    /* ============== STEP 1 - FW Download =========== */
    /* =============================================== */

    /* Download FW if fw_image is not null */
    /* Note: if fw_image is null - only activate FW */
    if (fw_image != NULL)
    {
        // FW download can be performed on slot 2 and slot 3 only (slot 1 is a "read-only" slot)
        CHECK_BOUNDS(fw_slot_index, NVRAM_FW_SLOT_2, NVRAM_FW_SLOT_3);

        PMCLOG_2(PMCLOG_INFO, "Download FW to slot %d (type=%d).\n", fw_slot_index, fw_source);

        action = NVRAM_FW_ACTION_STORE_AND_ACTIVATE;    /* Set action to store and activate */
        switch (fw_source)
        {
        case NVRAM_FW_SOURCE_FILE: /* The FW image will be taken from a file */
            status = Pmc_image_download_from_file(dev_handle, PMC_IMAGE_TYPE_FW, fw_image);
            break;
        case NVRAM_FW_SOURCE_MEMORY:   /* The FW image will be taken from the memory */
            status = Pmc_image_download_from_mem(dev_handle, PMC_IMAGE_TYPE_FW, fw_image, fw_image_size);
            break;
        default:
            return (P_STATUS_LIB_BAD_PARAM);
        }

        /* If fw download failed return the error now */
        if (status != P_STATUS_OK)
        {
            PMCLOG_1(PMCLOG_INFO, "FW Download failed (status=%d).\n", status);
            return (status);
        }
    }

    /* =============================================== */
    /* ============== STEP 2 - FW Activate =========== */
    /* =============================================== */
    PMCLOG_1(PMCLOG_INFO, "Activate FW to slot %d.\n", NVME_FW_SLOT(fw_slot_index));

    status = Pmc_fw_image_activate(dev_handle, fw_slot_index, action);

    if (status != P_STATUS_OK)
    {
        PMCLOG_1(PMCLOG_ERROR, "Firmware Image Activate command fails, library returns error=%d\n", status);
        return (status);
    }

    if (fw_image != NULL)
    {
        PMCLOG_1(PMCLOG_INFO, "FW Download and activate completed (slot=%d).\n", NVME_FW_SLOT(fw_slot_index));
    }
    else
    {
        PMCLOG_1(PMCLOG_INFO, "FW activate completed (slot=%d).\n", NVME_FW_SLOT(fw_slot_index));
    }


    return (P_STATUS_OK);

}



/*====================================================*/
/* API: PMC_NVRAM_sbl_download */
/*====================================================*/
P_STATUS PMC_NVRAM_sbl_download(const dev_handle_t dev_handle,
                               const NVRAM_fw_source_e sbl_source, const void *sbl_image, const uint32_t sbl_image_size)
{
    P_STATUS status = P_STATUS_OK;
    nvme_arg_s arg;
    int res;

    /* Download SBL if sbl_image is not null */
    if (sbl_image == NULL)
    {
        PMCLOG_0(PMCLOG_INFO, "SBL Download:image is empty.\n");
        return (P_STATUS_LIB_BAD_PARAM);
    }

    /* =============================================== */
    /* ============== STEP 1 - SBL Download to DDR =========== */
    /* =============================================== */

    PMCLOG_1(PMCLOG_INFO, "Download SBL to (type=%d).\n", sbl_source);

    switch (sbl_source)
    {
    case NVRAM_FW_SOURCE_FILE: /* The SBL image will be taken from a file */
        status = Pmc_image_download_from_file(dev_handle, PMC_IMAGE_TYPE_SBL, sbl_image);
        break;
    case NVRAM_FW_SOURCE_MEMORY:   /* The SBL image will be taken from the memory */
        status = Pmc_image_download_from_mem(dev_handle, PMC_IMAGE_TYPE_SBL, sbl_image, sbl_image_size);
        break;
    default:
        return (P_STATUS_LIB_BAD_PARAM);
    }

    /* If fw download failed return the error now */
    if (status != P_STATUS_OK)
    {
        PMCLOG_1(PMCLOG_INFO, "SBL Download failed (status=%d).\n", status);
        return (status);
    }

    /* ================================================ */
    /* ============== STEP 2 - SBL commit: copy from DDR to Eeprom == */
    /* ================================================ */

    bzero(&arg, sizeof(nvme_arg_s));
    arg.nvme_cmd.header.opCode = ADMIN_OPCODE_VENDOR_SYS_CONFIG;  /* NVRAM command */
    arg.nvme_cmd.header.namespaceID = 0;    /* Namespace ID not in use */
    arg.nvme_cmd.cmd.vendorSpecific.vndrCDW12 = SYS_CONFIG_COMMIT_EEPROM_IMAGE;  /* Download Eeprom image command */
    arg.nvme_cmd.dw[13] = 0;  /* Set SPI device */

    /* send command to device */
    res = pmc_ioctl(dev_handle, NVME_IOCTL_ADMIN_CMD, &arg);
    if (res != 0)
    {
        PMCLOG_1(PMCLOG_ERROR, "pmc_ioctl failed, error 0x%04x.\n", res);
        return (res);
    }


    return (P_STATUS_OK);

}



/*====================================================*/
/* API: PMC_NVRAM_reboot */
/*====================================================*/
#ifdef OS_FREE_BSD
P_STATUS PMC_NVRAM_reboot(const dev_handle_t dev_handle)
{
  
    int res; 
   
    /* Using ioctl to trigger nvme reset*/
    res = ioctl(dev_handle.nvme_fd,NVME_RESET_CONTROLLER, NULL);
    if (res< 0)
    {
    	PMCLOG_1(PMCLOG_ERROR, "ioctl failed, error 0x%04x.\n", res);
    	return (P_STATUS_HOST_OS_ERROR);
    }
  
  
    return P_STATUS_OK;
}
#else/*Linux OS case*/
P_STATUS PMC_NVRAM_reboot(const dev_handle_t dev_handle)
{
    int res;
    /*reboot is supported only if pmc nvme driver is used*/
    if(Get_is_pmc_driver())
    {
    	res = pmc_ioctl(dev_handle, NVME_IOCTL_RESTART, NULL);
    	if (res != 0)
    	{
    		PMCLOG_1(PMCLOG_ERROR, "pmc_ioctl failed, error 0x%04x.\n", res);
    		return (res);
    	}
    	return (P_STATUS_OK);
    }
    
    PMCLOG_0(PMCLOG_ERROR, "Operation not supported\n");
    return (P_STATUS_HOST_OS_ERROR); 
}
#endif

/*====================================================*/
/* API: PMC_NVRAM_config_set */
/*====================================================*/
P_STATUS PMC_NVRAM_config_set(const dev_handle_t dev_handle,
                              const NVRAM_config_type_e config_type, const NVRAM_config_data_u * config_data)
{
    int res;
    char prp[PRP_SIZE];
    nvme_arg_s arg;

    /* check params */
    CHECK_BOUNDS(config_type, NVRAM_CONFIG_TYPE_FIRST, NVRAM_CONFIG_TYPE_NUM - 1);
    CHECK_PTR(config_data);

    // check config_data param
    switch (config_type)
    {
    case NVRAM_CONFIG_TYPE_AUTO_BACKUP_MODE_WHEN_POWER_LOST:
        CHECK_BOUNDS(config_data->auto_backup_mode_when_power_lost, AUTO_BACKUP_MODE_DISABLE, AUTO_BACKUP_MODE_ENABLE);
        break;
    case NVRAM_CONFIG_TYPE_AUTO_RESTORE_MODE:
        CHECK_BOUNDS(config_data->auto_restore_mode, AUTO_RESTORE_MODE_DISABLE, AUTO_RESTORE_FROM_BANK);
        break;
    case NVRAM_CONFIG_TYPE_AUTO_RESTORE_BANK:
        CHECK_BOUNDS(config_data->auto_restore_bank, NVRAM_FLASH_BANK_0, NVRAM_FLASH_BANK_1);
        break;

    case NVRAM_CONFIG_TYPE_AUTO_BACKUP_MODE_WHEN_HEARTBEAT_LOST:
        CHECK_BOUNDS(config_data->auto_backup_mode_when_heartbeat_lost, AUTO_BACKUP_MODE_DISABLE,
                     AUTO_BACKUP_MODE_ENABLE);
        break;

    case NVRAM_CONFIG_TYPE_AUTO_BACKUP_MODE_WHEN_CPU_OVER_TEMPERATURE:
        CHECK_BOUNDS(config_data->auto_backup_mode_when_cpu_over_temperature, AUTO_BACKUP_MODE_DISABLE,
                     AUTO_BACKUP_MODE_ENABLE);
        break;

    case NVRAM_CONFIG_TYPE_AUTO_BACKUP_MODE_WHEN_SYSTEM_OVER_TEMPERATURE:
        CHECK_BOUNDS(config_data->auto_backup_mode_when_system_over_temperature, AUTO_BACKUP_MODE_DISABLE,
                     AUTO_BACKUP_MODE_ENABLE);
        break;

    case NVRAM_CONFIG_TYPE_AUTO_BACKUP_MODE_WHEN_BACKUP_POWER_OVER_TEMPERATURE:
        CHECK_BOUNDS(config_data->auto_backup_mode_when_backup_power_over_temperature, AUTO_BACKUP_MODE_DISABLE,
                     AUTO_BACKUP_MODE_ENABLE);
        break;

    case NVRAM_CONFIG_TYPE_ACTIVE_FW_INDEX:
        /* activate FW (slot-0/1/2/3 can be choosen.In case of slot 0 FW will move automatically to slot 1 ) */
        CHECK_BOUNDS(config_data->active_fw_index, NVRAM_FW_SLOT_0, NVRAM_FW_SLOT_3);
        PMC_NVRAM_fw_download(dev_handle, config_data->active_fw_index, NVRAM_FW_SOURCE_FILE, NULL, 0);
        break;

    case NVRAM_CONFIG_TYPE_AUTO_BACKUP_BANK_WHEN_POWER_LOST:
        CHECK_BOUNDS(config_data->auto_backup_bank_when_power_lost, NVRAM_FLASH_BANK_0, NVRAM_FLASH_BANK_1);
        break;

    case NVRAM_CONFIG_TYPE_AUTO_BACKUP_BANK_WHEN_HEARTBEAT_LOST:
        CHECK_BOUNDS(config_data->auto_backup_bank_when_heartbeat_lost, NVRAM_FLASH_BANK_0, NVRAM_FLASH_BANK_1);
        break;

    case NVRAM_CONFIG_TYPE_AUTO_BACKUP_BANK_WHEN_CPU_OVER_TEMPERATURE:
        CHECK_BOUNDS(config_data->auto_backup_bank_when_cpu_over_temperature, NVRAM_FLASH_BANK_0, NVRAM_FLASH_BANK_1);
        break;

    case NVRAM_CONFIG_TYPE_AUTO_BACKUP_BANK_WHEN_SYSTEM_OVER_TEMPERATURE:
        CHECK_BOUNDS(config_data->auto_backup_bank_when_system_over_temperature, NVRAM_FLASH_BANK_0,
                     NVRAM_FLASH_BANK_1);
        break;

    case NVRAM_CONFIG_TYPE_AUTO_BACKUP_BANK_WHEN_BACKUP_POWER_OVER_TEMPERATURE:
        CHECK_BOUNDS(config_data->auto_backup_bank_when_backup_power_over_temperature, NVRAM_FLASH_BANK_0,
                     NVRAM_FLASH_BANK_1);
        break;

        // booleans and keys - nothing to check
    case NVRAM_CONFIG_TYPE_ENABLE_BIST:
    case NVRAM_CONFIG_TYPE_AUTHENTICATION_KEY_ADMIN:
    case NVRAM_CONFIG_TYPE_AUTHENTICATION_KEY_MASTER:
    case NVRAM_CONFIG_TYPE_ENABLE_RAMDISK_ENCRYPTION:
    case NVRAM_CONFIG_TYPE_ENABLE_RAMDISK_DMI_OVERLAP:
    case NVRAM_CONFIG_TYPE_BAD_BLOCK_SCAN_USE_VBBS:
    case NVRAM_CONFIG_TYPE_ENABLE_DEBUG:
        break;

    case NVRAM_CONFIG_TYPE_CPU_TEMPERATURE_THRESHOLD:
        CHECK_BOUNDS(config_data->cpu_temperature_threshold, NVRAM_CONFIG_TYPE_CPU_TEMPERATURE_THRESHOLD_MIN,
                     NVRAM_CONFIG_TYPE_CPU_TEMPERATURE_THRESHOLD_MAX);
        break;

    case NVRAM_CONFIG_TYPE_CPU_CRITICAL_TEMPERATURE_THRESHOLD:
        CHECK_BOUNDS(config_data->cpu_critical_temperature_threshold,
                     NVRAM_CONFIG_TYPE_CPU_CRITICAL_TEMPERATURE_THRESHOLD_MIN,
                     NVRAM_CONFIG_TYPE_CPU_CRITICAL_TEMPERATURE_THRESHOLD_MAX);
        break;

    case NVRAM_CONFIG_TYPE_SYSTEM_TEMPERATURE_THRESHOLD:
        CHECK_BOUNDS(config_data->system_temperature_threshold, NVRAM_CONFIG_TYPE_SYSTEM_TEMPERATURE_THRESHOLD_MIN,
                     NVRAM_CONFIG_TYPE_SYSTEM_TEMPERATURE_THRESHOLD_MAX);
        break;

    case NVRAM_CONFIG_TYPE_SYSTEM_CRITICAL_TEMPERATURE_THRESHOLD:
        CHECK_BOUNDS(config_data->system_critical_temperature_threshold,
                     NVRAM_CONFIG_TYPE_SYSTEM_CRITICAL_TEMPERATURE_THRESHOLD_MIN,
                     NVRAM_CONFIG_TYPE_SYSTEM_CRITICAL_TEMPERATURE_THRESHOLD_MAX);
        break;

    case NVRAM_CONFIG_TYPE_BACKUP_POWER_TEMPERATURE_THRESHOLD:
        CHECK_BOUNDS(config_data->backup_power_temperature_threshold,
                     NVRAM_CONFIG_TYPE_BACKUP_POWER_TEMPERATURE_THRESHOLD_MIN,
                     NVRAM_CONFIG_TYPE_BACKUP_POWER_TEMPERATURE_THRESHOLD_MAX);
        break;

    case NVRAM_CONFIG_TYPE_BACKUP_POWER_CRITICAL_TEMPERATURE_THRESHOLD:
        PMCLOG_0(PMCLOG_ERROR, "Invalid command, BACKUP_POWER_CRITICAL_TEMPERATURE_THRESHOLD is read only.\n");
        return (P_STATUS_LIB_UNSUPPORTED);

    case NVRAM_CONFIG_TYPE_BACKUP_POWER_CHARGE_LEVEL_THRESHOLD:
        PMCLOG_0(PMCLOG_ERROR, "Invalid command,  BACKUP_POWER_CHARGE_LEVEL_THRESHOLD is read only.\n");
        return (P_STATUS_LIB_UNSUPPORTED);

#if 0
    case NVRAM_CONFIG_TYPE_RAMDISK_DMI_OVERLAP_SIZE:
        // TBD
        break;
#endif
    case NVRAM_CONFIG_TYPE_DMI_SIZE:
        CHECK_BOUNDS(config_data->dmi_size, NVRAM_CONFIG_TYPE_DMI_SIZE_MIN, NVRAM_CONFIG_TYPE_DMI_SIZE_MAX);
        break;

    case NVRAM_CONFIG_TYPE_RAMDISK_SIZE:
        CHECK_BOUNDS(config_data->ramdisk_size, NVRAM_CONFIG_TYPE_RAMDISK_SIZE_MIN, NVRAM_CONFIG_TYPE_RAMDISK_SIZE_MAX);
        break;

    case NVRAM_CONFIG_TYPE_HEARTBEAT_MSG_INTERVAL:
        CHECK_BOUNDS(config_data->heartbeat_msg_interval, NVRAM_HEARTBEAT_INTERVAL_VAL_MIN,
                     NVRAM_HEARTBEAT_INTERVAL_VAL_MAX);
        g_heartbeat_interval = config_data->heartbeat_msg_interval;
        break;

    case NVRAM_CONFIG_TYPE_HEARTBEAT_MSG_NUMBER:
        CHECK_BOUNDS(config_data->heartbeat_msg_number, NVRAM_HEARTBEAT_LOST_MSGS_MIN, NVRAM_HEARTBEAT_LOST_MSGS_MAX);
        break;

    case NVRAM_CONFIG_TYPE_EVENTS_POLLING_INTERVAL:
        CHECK_BOUNDS(config_data->events_polling_interval, EVENTS_POLLING_INTERVAL_MIN, EVENTS_POLLING_INTERVAL_MAX);
        Pmc_polling_interval_set(config_data->events_polling_interval); // local operation, don't continue to ioctl
        return (P_STATUS_OK);

    default:
        PMCLOG_1(PMCLOG_ERROR, "Invalid config_type.\n", config_type);
        return (P_STATUS_LIB_BAD_PARAM);
    }


    bzero(prp, sizeof(prp));
    memcpy(prp, config_data, sizeof(NVRAM_config_data_u));

    bzero(&arg, sizeof(nvme_arg_s));
    arg.nvme_cmd.header.opCode = ADMIN_OPCODE_VENDOR_NVRAM; /* NVRAM command */
    arg.nvme_cmd.header.namespaceID = 0;    /* Namespace ID not in use */
    arg.nvme_cmd.header.prp1 = (uint64_t) prp;  /* PRP 1 (Input data) */
    arg.data_length = sizeof(prp);  /* PRP data len in Dwords */
    arg.nvme_cmd.cmd.vendorSpecific.vndrCDW12 = NVRAM_VENDOR_SUBCMD_CONFIG_SET; /* Vendor Specific Config Set command */
    arg.nvme_cmd.cmd.vendorSpecific.vndrCDW13 = (uint32_t) config_type; /* Vendor Specific Config Type */

    /* send command to device */
    res = pmc_ioctl(dev_handle, NVME_IOCTL_ADMIN_CMD, &arg);
    if (res != 0)
    {
        PMCLOG_1(PMCLOG_ERROR, "pmc_ioctl failed, error 0x%04x.\n", res);
        return (res);
    }

    return (P_STATUS_OK);
}


/*====================================================*/
/* API: PMC_NVRAM_config_get */
/*====================================================*/
P_STATUS PMC_NVRAM_config_get(const dev_handle_t dev_handle,
                              const NVRAM_config_type_e config_type, NVRAM_config_data_u * config_data)
{
    int res;

    /* check params */
    CHECK_BOUNDS(config_type, NVRAM_CONFIG_TYPE_FIRST, NVRAM_CONFIG_TYPE_NUM - 1);
    CHECK_PTR(config_data);

    switch (config_type)
    {
    case NVRAM_CONFIG_TYPE_EVENTS_POLLING_INTERVAL:
        config_data->events_polling_interval = Pmc_polling_interval_get();  // local operation, don't continue to ioctl
        return (P_STATUS_OK);

    default:
        break;
    }

    res = Pmc_config_get(dev_handle, config_type, config_data);
    if (res != P_STATUS_OK)
    {
        PMCLOG_1(PMCLOG_ERROR, "Pmc_config_get failed, error 0x%04x.\n", res);
        return (res);
    }

    return (P_STATUS_OK);
}


/*====================================================*/
/* API: PMC_NVRAM_event_handler_register */
/*====================================================*/
P_STATUS PMC_NVRAM_event_handler_register(const dev_handle_t dev_handle,
                                          const NVRAM_event_type_e event_type,
                                          const PMC_NVRAM_gen_event_handler_t event_handler,
                                          uint16_t * event_context_id)
{
    int res;


    CHECK_BOUNDS(event_type, NVRAM_EVENT_TYPE_FIRST, NVRAM_EVENT_TYPE_NUMBER - 1);
    CHECK_PTR(event_handler);
    CHECK_PTR(event_context_id);

    /* if first register than Init events package */
    API_SEM_TAKE_RET_ERR(g_api_sem);
    if (Pmc_is_events_pkg_init() == PMC_FALSE)  /* do only once */
    {
        Pmc_pre_init_events_pkg();
    }
    API_SEM_GIVE_RET_ERR(g_api_sem);

    res = Pmc_event_handler_register(dev_handle, event_type, event_handler, event_context_id);
    if (res == P_STATUS_OK)
    {
        /* if first registration than Init events package */
        API_SEM_TAKE_RET_ERR(g_api_sem);
        if (Pmc_is_events_pkg_init() == PMC_FALSE)  /* do only once */
        {
            res = Pmc_init_events_pkg();
            if (res != 0)
            {
                API_SEM_GIVE_RET_ERR(g_api_sem);
                PMCLOG_1(PMCLOG_ERROR, "Pmc_init_events_pkg failed.\n", res);
                return (res);
            }
        }
        API_SEM_GIVE_RET_ERR(g_api_sem);
    }

    return (res);
}


/*====================================================*/
/* API: PMC_NVRAM_event_handler_unregister */
/*====================================================*/
P_STATUS PMC_NVRAM_event_handler_unregister(const dev_handle_t dev_handle, const uint16_t event_context_id)
{
    return (Pmc_event_handler_unregister(dev_handle, event_context_id));
}


/*====================================================*/
/* API: PMC_NVRAM_event_poll */
/*====================================================*/
P_STATUS PMC_NVRAM_event_poll(void)
{
    /* check thread mode */
    if (g_thread_mode != NVRAM_OPERATION_MODE_USER) 
    {
        PMCLOG_0(PMCLOG_ERROR, "Error, this API is applicable only when thread mode = NVRAM_OPERATION_MODE_USER.\n");
        return P_STATUS_LIB_UNSUPPORTED;
    }

    /* Poll */
    return (Pmc_event_poll());
}


/*====================================================*/
/* API: PMC_NVRAM_bad_block_scan */
/*====================================================*/
P_STATUS PMC_NVRAM_bad_block_scan(const dev_handle_t dev_handle,    /* The device handle */
                                  const NVRAM_flash_bank_id_e bank_id)  /* The ID of the bank */
{
    int res;
    nvme_arg_s arg;


    /* check params */
    CHECK_BOUNDS(bank_id, NVRAM_FLASH_BANK_0, NVRAM_FLASH_BANK_1);



    bzero(&arg, sizeof(nvme_arg_s));
    arg.nvme_cmd.header.opCode = ADMIN_OPCODE_VENDOR_NVRAM; /* NVRAM command */
    arg.nvme_cmd.header.namespaceID = 0;    /* Namespace ID not in use */
    arg.nvme_cmd.cmd.vendorSpecific.vndrCDW12 = NVRAM_VENDOR_SUBCMD_SCAN_BAD_BLOCKS;    /* Vendor Specific bad block
                                                                                           scan command */
    arg.nvme_cmd.cmd.vendorSpecific.vndrCDW13 = (uint32_t) bank_id; /* Vendor Specific bank ID */

    /* send command to device */
    res = pmc_ioctl(dev_handle, NVME_IOCTL_ADMIN_CMD, &arg);
    if (res != 0)
    {
        PMCLOG_1(PMCLOG_ERROR, "pmc_ioctl failed, error 0x%04x.\n", res);
        return (res);
    }


    return (P_STATUS_OK);
}



/*====================================================*/
/* API: PMC_NVRAM_erase */
/*====================================================*/
P_STATUS PMC_NVRAM_erase(const dev_handle_t dev_handle,
                         const NVRAM_flash_bank_id_e bank_id, const NVRAM_flash_erase_type_e erase_type)
{
    int res;
    nvme_arg_s arg;


    /* check params */
    CHECK_BOUNDS(bank_id, NVRAM_FLASH_BANK_0, NVRAM_FLASH_BANK_1);
    CHECK_BOUNDS(erase_type, NVRAM_FLASH_ERASE_TYPE_STANDARD, NVRAM_FLASH_ERASE_TYPE_SECURE);



    bzero(&arg, sizeof(nvme_arg_s));
    arg.nvme_cmd.header.opCode = ADMIN_OPCODE_VENDOR_NVRAM; /* NVRAM command */
    arg.nvme_cmd.header.namespaceID = 0;    /* Namespace ID not in use */
    arg.nvme_cmd.cmd.vendorSpecific.vndrCDW12 = NVRAM_VENDOR_SUBCMD_ERASE;  /* Vendor Specific Erase command */
    arg.nvme_cmd.cmd.vendorSpecific.vndrCDW13 = (uint32_t) bank_id; /* Vendor Specific bank ID */
    arg.nvme_cmd.cmd.vendorSpecific.vndrCDW14 = (uint32_t) erase_type;  /* Vendor Specific erase type */

    /* send command to device */
    res = pmc_ioctl(dev_handle, NVME_IOCTL_ADMIN_CMD, &arg);
    if (res != 0)
    {
        PMCLOG_1(PMCLOG_ERROR, "pmc_ioctl failed, error 0x%04x.\n", res);
        return (res);
    }


    return (P_STATUS_OK);
}


/*====================================================*/
/* API: PMC_NVRAM_backup */
/*====================================================*/
P_STATUS PMC_NVRAM_backup(const dev_handle_t dev_handle, const NVRAM_flash_bank_id_e bank_id)
{
    int res;
    nvme_arg_s arg;


    /* check params */
    CHECK_BOUNDS(bank_id, NVRAM_FLASH_BANK_0, NVRAM_FLASH_BANK_1);


    bzero(&arg, sizeof(nvme_arg_s));
    arg.nvme_cmd.header.opCode = ADMIN_OPCODE_VENDOR_NVRAM; /* NVRAM command */
    arg.nvme_cmd.header.namespaceID = 0;    /* Namespace ID not in use */
    arg.nvme_cmd.cmd.vendorSpecific.vndrCDW12 = NVRAM_VENDOR_SUBCMD_BACKUP; /* Vendor Specific Backup command */
    arg.nvme_cmd.cmd.vendorSpecific.vndrCDW13 = (uint32_t) bank_id; /* Vendor Specific bank ID */

    /* send command to device */
    res = pmc_ioctl(dev_handle, NVME_IOCTL_ADMIN_CMD, &arg);
    if (res != 0)
    {
        PMCLOG_1(PMCLOG_ERROR, "pmc_ioctl failed, error 0x%04x.\n", res);
        return (res);
    }


    return (P_STATUS_OK);
}

/*====================================================*/
/* API: PMC_NVRAM_restore */
/*====================================================*/
P_STATUS PMC_NVRAM_restore(const dev_handle_t dev_handle, const NVRAM_flash_bank_id_e bank_id)
{
    int res;
    nvme_arg_s arg;

    /* check params */
    CHECK_BOUNDS(bank_id, NVRAM_FLASH_BANK_0, NVRAM_FLASH_BANK_1);


    bzero(&arg, sizeof(nvme_arg_s));
    arg.nvme_cmd.header.opCode = ADMIN_OPCODE_VENDOR_NVRAM; /* NVRAM command */
    arg.nvme_cmd.header.namespaceID = 0;    /* Namespace ID not in use */
    arg.nvme_cmd.cmd.vendorSpecific.vndrCDW12 = NVRAM_VENDOR_SUBCMD_RESTORE;    /* Vendor Specific Restore command */
    arg.nvme_cmd.cmd.vendorSpecific.vndrCDW13 = (uint32_t) bank_id; /* Vendor Specific bank ID */

    /* send command to device */
    res = pmc_ioctl(dev_handle, NVME_IOCTL_ADMIN_CMD, &arg);
    if (res != 0)
    {
        PMCLOG_1(PMCLOG_ERROR, "pmc_ioctl failed, error %d.\n", res);
        return (res);
    }


    return (P_STATUS_OK);
}

/*====================================================*/
/* API: PMC_NVRAM_restore_corrupted */
/*====================================================*/
P_STATUS PMC_NVRAM_restore_corrupted(const dev_handle_t dev_handle, const NVRAM_flash_bank_id_e bank_id)
{
    int res;
    nvme_arg_s arg;

    /* check params */
    CHECK_BOUNDS(bank_id, NVRAM_FLASH_BANK_0, NVRAM_FLASH_BANK_1);


    bzero(&arg, sizeof(nvme_arg_s));
    arg.nvme_cmd.header.opCode = ADMIN_OPCODE_VENDOR_NVRAM; /* NVRAM command */
    arg.nvme_cmd.header.namespaceID = 0;    /* Namespace ID not in use */
    arg.nvme_cmd.cmd.vendorSpecific.vndrCDW12 = NVRAM_VENDOR_SUBCMD_RESTORE_CORRUPTED;  /* Vendor Specific Restore
                                                                                           command */
    arg.nvme_cmd.cmd.vendorSpecific.vndrCDW13 = (uint32_t) bank_id; /* Vendor Specific bank ID */

    /* send command to device */
    res = pmc_ioctl(dev_handle, NVME_IOCTL_ADMIN_CMD, &arg);
    if (res != 0)
    {
        PMCLOG_1(PMCLOG_ERROR, "pmc_ioctl failed, error %d.\n", res);
        return (res);
    }


    return (P_STATUS_OK);
}


/*====================================================*/
/* API: PMC_NVRAM_flash_bank_info_get */
/*====================================================*/
P_STATUS PMC_NVRAM_flash_bank_info_get(const dev_handle_t dev_handle,
                                       const NVRAM_flash_bank_id_e bank_id, NVRAM_flash_bank_info_data_s * info_data)
{
    int res;
    char prp[PRP_SIZE];
    nvme_arg_s arg;

    /* check params */
    CHECK_BOUNDS(bank_id, NVRAM_FLASH_BANK_0, NVRAM_FLASH_BANK_1);
    CHECK_PTR(info_data);

    bzero(prp, sizeof(prp));
    bzero(info_data, sizeof(NVRAM_flash_bank_info_data_s));

    bzero(&arg, sizeof(nvme_arg_s));
    arg.nvme_cmd.header.opCode = ADMIN_OPCODE_VENDOR_NVRAM; /* NVRAM command */
    arg.nvme_cmd.header.namespaceID = 0;    /* Namespace ID not in use */
    arg.nvme_cmd.header.prp1 = (uint64_t) prp;  /* PRP 1 (Output data) */
    arg.data_length = sizeof(prp);  /* PRP data len in Dwords */
    arg.nvme_cmd.cmd.vendorSpecific.vndrCDW12 = NVRAM_VENDOR_SUBCMD_FLASH_BANK_INFO_GET;    /* Vendor Specific Flash
                                                                                               Info Get command */
    arg.nvme_cmd.cmd.vendorSpecific.vndrCDW13 = (uint32_t) bank_id; /* Vendor Specific bank ID */

    res = pmc_ioctl(dev_handle, NVME_IOCTL_ADMIN_CMD, &arg);
    if (res != 0)
    {
        PMCLOG_1(PMCLOG_ERROR, "pmc_ioctl failed, error 0x%04x.\n", res);
        return (res);
    }

    /* copy the returned PRP data to user struct */
    memcpy(info_data, prp, sizeof(NVRAM_flash_bank_info_data_s));

    return (P_STATUS_OK);
}



/*====================================================*/
/* API:  PMC_NVRAM_fw_metadata_get                    */
/*====================================================*/
P_STATUS PMC_NVRAM_fw_metadata_get(const dev_handle_t dev_handle,
                                   NVRAM_fw_metadata_s *fw_metadata)
{

    int res;
    char prp[PRP_SIZE];
//    uint8_t ch, target, lun;
    nvme_arg_s arg;

    /* check params */
    CHECK_PTR(fw_metadata);

    bzero(prp, sizeof(prp));
    bzero(fw_metadata, sizeof(NVRAM_fw_metadata_s));

    bzero(&arg, sizeof(nvme_arg_s));
    arg.nvme_cmd.header.opCode = ADMIN_OPCODE_VENDOR_NVRAM; /* NVRAM command */
    arg.nvme_cmd.header.namespaceID = 0;    /* Namespace ID not in use */
    arg.nvme_cmd.header.prp1 = (uint64_t) prp;  /* PRP 1 (Output data) */
    arg.data_length = sizeof(prp);  /* PRP data len in Dwords */
    arg.nvme_cmd.cmd.vendorSpecific.vndrCDW12 = NVRAM_VENDOR_SUBCMD_FW_BAD_BLOCK_GET;  /* Vendor Specific stats Get command */
    arg.nvme_cmd.cmd.vendorSpecific.vndrCDW13 = 0;

    /* send command to device */
    res = pmc_ioctl(dev_handle, NVME_IOCTL_ADMIN_CMD, &arg);
    if (res != 0)
    {
        PMCLOG_1(PMCLOG_ERROR, "pmc_ioctl failed, error 0x%04x.\n", res);
        return (res);
    }


    /* copy the returned PRP to the user statistics_data structure */
    memcpy(fw_metadata, prp, sizeof(NVRAM_fw_metadata_s));

#if 0 // Debug utility


    for(ch=0; ch<MAX_CHANNELS; ch++)
    {
        for(target=0; target<MAX_TARGETS; target++)
        {
            for(lun=0; lun<MAX_LUNS; lun++)
            {
                fw_metadata->fwAreaBadBlockArr[ch][target][lun] = fwAreaBadBlockArr[ch][target][lun];
            }
        }
    }

    for(slot=1; slot<=NUM_OF_FW_SLOTS; slot++)
    {
        for(ch=0; ch<MAX_CHANNELS; ch++)
        {
            for(target=0; target<MAX_TARGETS; target++)
            {
                for(lun=0; lun<MAX_LUNS; lun++)
                {
                        printf("CH: %u, TARGET: %u, LUN: %u, BLOCK: %u, (status: %u)\n", ch, target, lun, slot, getBit(fwAreaBadBlockArr[ch][target][lun], slot));
                }
            }
        }
    }
#endif

    return (P_STATUS_OK);
}





/*====================================================*/
/* API: PMC_NVRAM_statistics_get */
/*====================================================*/
P_STATUS PMC_NVRAM_statistics_get(const dev_handle_t dev_handle,
                                  const NVRAM_statistics_group_e statistics_group,
                                  NVRAM_statistics_data_u * statistics_data)
{



    int res;
    char prp[PRP_SIZE];
    nvme_arg_s arg;

    /* check params */
    CHECK_BOUNDS(statistics_group, NVRAM_STATISTICS_GROUP_START, NVRAM_STATISTICS_GROUP_NUMBER - 1);
    CHECK_PTR(statistics_data);

    bzero(prp, sizeof(prp));
    bzero(statistics_data, sizeof(NVRAM_statistics_data_u));

    bzero(&arg, sizeof(nvme_arg_s));
    arg.nvme_cmd.header.opCode = ADMIN_OPCODE_VENDOR_NVRAM; /* NVRAM command */
    arg.nvme_cmd.header.namespaceID = 0;    /* Namespace ID not in use */
    arg.nvme_cmd.header.prp1 = (uint64_t) prp;  /* PRP 1 (Output data) */
    arg.data_length = sizeof(prp);  /* PRP data len in Dwords */
    arg.nvme_cmd.cmd.vendorSpecific.vndrCDW12 = NVRAM_VENDOR_SUBCMD_STATS_GET;  /* Vendor Specific stats Get command */
    arg.nvme_cmd.cmd.vendorSpecific.vndrCDW13 = (uint32_t) statistics_group;    /* Vendor Specific stats Group */

    /* send command to device */
    res = pmc_ioctl(dev_handle, NVME_IOCTL_ADMIN_CMD, &arg);
    if (res != 0)
    {
        PMCLOG_1(PMCLOG_ERROR, "pmc_ioctl failed, error 0x%04x.\n", res);
        return (res);
    }



    /* copy the returned PRP to the user statistics_data structure */
    memcpy(statistics_data, prp, sizeof(NVRAM_statistics_data_u));

    return (P_STATUS_OK);


}


/*====================================================*/
/* API: PMC_NVRAM_statistics_reset */
/*====================================================*/
P_STATUS PMC_NVRAM_statistics_reset(const dev_handle_t dev_handle, const NVRAM_statistics_group_e statistics_group)
{
    int res;
    nvme_arg_s arg;

    /* check params */
    CHECK_BOUNDS(statistics_group, NVRAM_STATISTICS_GROUP_START, NVRAM_STATISTICS_GROUP_NUMBER - 1);


    bzero(&arg, sizeof(nvme_arg_s));
    arg.nvme_cmd.header.opCode = ADMIN_OPCODE_VENDOR_NVRAM; /* NVRAM command */
    arg.nvme_cmd.header.namespaceID = 0;    /* Namespace ID not in use */
    arg.nvme_cmd.cmd.vendorSpecific.vndrCDW12 = NVRAM_VENDOR_SUBCMD_STATS_RESET;    /* Vendor Specific stats Get
                                                                                       command */
    arg.nvme_cmd.cmd.vendorSpecific.vndrCDW13 = (uint32_t) statistics_group;    /* Vendor Specific stats Group */

    /* send command to device */
    res = pmc_ioctl(dev_handle, NVME_IOCTL_ADMIN_CMD, &arg);
    if (res != 0)
    {
        PMCLOG_1(PMCLOG_ERROR, "pmc_ioctl failed, error 0x%04x.\n", res);
        return (res);
    }

    return (P_STATUS_OK);

}

/*===============================================================*/
/* API: PMC_NVRAM_debug_get_log_page */
/* This function gets the log page from FW */
/* function assumes that */
/* PRP_SIZE = NVRAM_LOG_STRINGS_PER_PAGE * NVRAM_LOG_STRING_SIZE */
/*===============================================================*/
P_STATUS PMC_NVRAM_debug_get_log_page(const dev_handle_t dev_handle,    /* The device handle */
                                      const NVRAM_log_page_id_e log_page_id,    /* LOG page ID (manager #) */
                                      const uint32_t log_strings_num,   /* Number of LOG strings */
                                      const NVRAM_log_page_from_backup_e from_backup,   /* get backed-up log page */
                                      char *log_buf /* Output LOG buffer */
    )
{
    nvme_arg_s arg;
    int res;
    uint32_t num_of_dwords;
    uint32_t proc_id, min_log_offs;
    uint32_t ii, temp;
    uint32_t pages = PAGES_PER_CORE;

    CHECK_PTR(log_buf);

    CHECK_BOUNDS(log_page_id, LOG_PAGE_PROC_34, LOG_PAGE_STAT_HISTORY);
    CHECK_BOUNDS(log_strings_num, 1, PAGES_PER_CORE * PRP_SIZE / NVRAM_LOG_STRING_SIZE);
    CHECK_BOUNDS(from_backup, LOG_PAGE_NORMAL, LOG_PAGE_BACKED_UP);

    if (LOG_PAGE_STAT_HISTORY == log_page_id && log_strings_num != NVRAM_LOG_STRINGS_PER_PAGE)
    {
        PMCLOG_1(PMCLOG_ERROR, "for LOG_PAGE_STAT_HISTORY, must be buffer of %d lines\n", NVRAM_LOG_STRINGS_PER_PAGE);
        return P_STATUS_LIB_BAD_PARAM;
    }

    if (from_backup == LOG_PAGE_BACKED_UP)
        proc_id = (uint32_t) log_page_id + NVRAM_LOG_BACKED_UP_LOG_PAGE_PROC_0;
    else
    {
        /* 
         * since the LOG_PAGE_STAT_HISTORY isnt consecutive with the rest of the cores
         * need to use hardcoded value instead of the formula 
         */
        if (log_page_id != LOG_PAGE_STAT_HISTORY)
            proc_id = (uint32_t) log_page_id + NVRAM_LOG_LOG_PAGE_PROC_0;
        else
            proc_id = 0xD4;
    }

    bzero(&arg, sizeof(nvme_arg_s));
    bzero(&prp_inner, sizeof(prp_inner));
    bzero(log_buf, log_strings_num * NVRAM_LOG_STRING_SIZE);

    arg.nvme_cmd.header.opCode = ADMIN_OPCODE_GET_LOG_PAGE; /* Get Log Page command */
    arg.nvme_cmd.header.namespaceID = 0;    /* Namespace ID not in use */
    arg.nvme_cmd.header.prp1 = (uint64_t) prp_inner;    /* PRP 1 (Output data) */
    arg.data_length = PRP_SIZE; /* PRP data len in Dwords */

    num_of_dwords = NVRAM_LOG_STRINGS_PER_PAGE * NVRAM_LOG_STRING_SIZE / sizeof(unsigned int);
    arg.nvme_cmd.dw[10] = ((proc_id & 0xFF) | (((num_of_dwords - 1) & 0xFFF) << 16));

    if (log_page_id == LOG_PAGE_STAT_HISTORY)
    {
        pages = 1;
    }

    for (ii = 0; ii < pages; ii++)
    {
        arg.nvme_cmd.header.prp1 = (uint64_t) prp_inner + PRP_SIZE * ii;
        arg.nvme_cmd.dw[11] = ii;

        /* send command to device */
        res = pmc_ioctl(dev_handle, NVME_IOCTL_ADMIN_CMD, &arg);
        if (res != 0)
        {
            PMCLOG_1(PMCLOG_ERROR, "pmc_ioctl failed, error %d.\n", res);
            return (res);
        }
    }

    if (LOG_PAGE_STAT_HISTORY == log_page_id)
    {
        /* copy the returned PRP data to user buffer */
        memcpy(log_buf, prp_inner, NVRAM_LOG_STRINGS_PER_PAGE * NVRAM_LOG_STRING_SIZE);
        return P_STATUS_OK;
    }

    min_log_offs = find_first_entry_to_copy(prp_inner, log_strings_num);

    /* copy the returned PRP data to user buffer */
    if (min_log_offs + log_strings_num <= NVRAM_LOG_STRINGS_PER_PAGE * PAGES_PER_CORE)
    {
        memcpy(log_buf, prp_inner + min_log_offs * NVRAM_LOG_STRING_SIZE, log_strings_num * NVRAM_LOG_STRING_SIZE);
    }
    else
    {
        temp = NVRAM_LOG_STRINGS_PER_PAGE * PAGES_PER_CORE - min_log_offs;
        memcpy(log_buf, prp_inner + min_log_offs * NVRAM_LOG_STRING_SIZE, temp * NVRAM_LOG_STRING_SIZE);
        memcpy(log_buf + temp * NVRAM_LOG_STRING_SIZE, prp_inner, (log_strings_num - temp) * NVRAM_LOG_STRING_SIZE);
    }

    return P_STATUS_OK;
}



/*============================================================*/
/* API: PMC_NVRAM_debug_block_write */
/* Description: This DEBUG API writes to a specific block */
/*============================================================*/
P_STATUS PMC_NVRAM_debug_block_write(const dev_handle_t dev_handle, /* The device handle */
                                     const uint32_t channel,    /* channel number */
                                     const uint32_t target, /* target number */
                                     const uint32_t lun,    /* lun number */
                                     const uint32_t block,  /* block number */
                                     const uint32_t page,   /* page number */
                                     const uint32_t page_size,  /* page size */
                                     char *in_buf   /* The buffer holds the data to write to the block */
    )
{
    nvme_arg_s arg;
    int res;
    char prp[PRP_SIZE];
    uint32_t prp_num;
    char *errorMsgFlashToDDr = "moving from Flash to DDR failed.";
    char *errorMsgChTgtLunBlkOutOfRange =
        "Out of range. Check the correctness of Channel, Target, Lun, Block, Page number.";
    char *errorMsg = ".";
    const int SF_SC_INT_DEV_ERR = 0x6;
    const int SF_SC_INV_FIELD = 0x2;

    CHECK_PTR(in_buf);

    if (page_size % PRP_SIZE != 0)
    {
        PMCLOG_1(PMCLOG_ERROR, "%s failed this API desn't support page sizes that are not a multiple of PRP_SIZE.\n",
                 __FUNCTION__);
        return P_STATUS_LIB_UNSUPPORTED;
    }

    if (page_size < PRP_SIZE)
    {
        PMCLOG_1(PMCLOG_ERROR, "%s failed this API desn't support page sizes that are smaller then PRP_SIZE.\n",
                 __FUNCTION__);
        return P_STATUS_LIB_UNSUPPORTED;
    }


    for (prp_num = 0; prp_num < page_size / PRP_SIZE; prp_num++)
    {
        /* copy the PRP data from user buffer */
        memcpy(prp, in_buf + (prp_num * PRP_SIZE), PRP_SIZE);

        bzero(&arg, sizeof(nvme_arg_s));
        arg.nvme_cmd.header.opCode = ADMIN_OPCODE_VENDOR_NVRAM; /* NVRAM command */
        arg.nvme_cmd.header.namespaceID = 0;    /* Namespace ID not in use */
        arg.nvme_cmd.header.prp1 = (uint64_t) prp;  /* PRP 1 (Output data) */
        arg.data_length = sizeof(prp);  /* PRP data len in Dwords */
        arg.nvme_cmd.cmd.vendorSpecific.buffNumDW = channel;    // dw[10]
        arg.nvme_cmd.cmd.vendorSpecific.metaNumDW = target; // dw[11]
        arg.nvme_cmd.cmd.vendorSpecific.vndrCDW12 = NVRAM_VENDOR_SUBCMD_BLOCK_CONTENT_SET;  /* Vendor Specific DDR
                                                                                               write Content command */
        arg.nvme_cmd.cmd.vendorSpecific.vndrCDW13 = lun;
        arg.nvme_cmd.cmd.vendorSpecific.vndrCDW14 = block;
        arg.nvme_cmd.cmd.vendorSpecific.vndrCDW15 = page | (prp_num << 16);


        /* send command to device */
        res = pmc_ioctl(dev_handle, NVME_IOCTL_ADMIN_CMD, &arg);
        if (res != 0)
        {
            /* for these errors see the function NvramGeneric.c/getPageContentPoll */
            if (SF_SC_INT_DEV_ERR == res)
                errorMsg = errorMsgFlashToDDr;
            if (SF_SC_INV_FIELD == res)
                errorMsg = errorMsgChTgtLunBlkOutOfRange;
            PMCLOG_2(PMCLOG_ERROR, "pmc_ioctl failed, error 0x%04x %s\n", res, errorMsg);
            return (res);
        }


        /* copy the returned PRP data to user buffer */
        memcpy(in_buf + (prp_num * PRP_SIZE), prp, PRP_SIZE);
    }
    return P_STATUS_OK;

}

/*============================================================*/
/* API: PMC_NVRAM_debug_page_of_block_get */
/* Description: This DEBUG API gets a page of specific block */
/*============================================================*/
P_STATUS PMC_NVRAM_debug_page_of_block_get(const dev_handle_t dev_handle,   /* The device handle */
                                           const uint32_t channel,  /* channel number */
                                           const uint32_t target,   /* target number */
                                           const uint32_t lun,  /* lun number */
                                           const uint32_t block,    /* block number */
                                           const uint32_t page, /* page number */
                                           const uint32_t page_size,    /* page size */
                                           char *out_buf    /* The buffer where the content is copied */
    )
{
    nvme_arg_s arg;
    int res;
    char prp[PRP_SIZE];
    uint32_t prp_num;
    char *errorMsgFlashToDDr = "moving from Flash to DDR failed.";
    char *errorMsgChTgtLunBlkOutOfRange =
        "Out of range. Check the correctness of Channel, Target, Lun, Block, Page number.";
    char *errorMsg = ".";
    const int SF_SC_INT_DEV_ERR = 0x6;
    const int SF_SC_INV_FIELD = 0x2;

    CHECK_PTR(out_buf);

    if (page_size % PRP_SIZE != 0)
    {
        PMCLOG_1(PMCLOG_ERROR, "%s failed this API desn't support page sizes that are not a multiple of PRP_SIZE.\n",
                 __FUNCTION__);
        return P_STATUS_LIB_UNSUPPORTED;
    }

    if (page_size < PRP_SIZE)
    {
        PMCLOG_1(PMCLOG_ERROR, "%s failed this API desn't support page sizes that are smaller then PRP_SIZE.\n",
                 __FUNCTION__);
        return P_STATUS_LIB_UNSUPPORTED;
    }


    for (prp_num = 0; prp_num < page_size / PRP_SIZE; prp_num++)
    {
        // #if 1
        // printf("retrieving page = %d, prp_num = %d\n", page, prp_num);
        // #endif
        bzero(&arg, sizeof(nvme_arg_s));
        arg.nvme_cmd.header.opCode = ADMIN_OPCODE_VENDOR_NVRAM; /* NVRAM command */
        arg.nvme_cmd.header.namespaceID = 0;    /* Namespace ID not in use */
        arg.nvme_cmd.header.prp1 = (uint64_t) prp;  /* PRP 1 (Output data) */
        arg.data_length = sizeof(prp);  /* PRP data len in Dwords */
        arg.nvme_cmd.cmd.vendorSpecific.buffNumDW = channel;    // dw[10]
        arg.nvme_cmd.cmd.vendorSpecific.metaNumDW = target; // dw[11]
        arg.nvme_cmd.cmd.vendorSpecific.vndrCDW12 = NVRAM_VENDOR_SUBCMD_PAGE_CONTENT_GET;   /* Vendor Specific DDR
                                                                                               Content command */
        arg.nvme_cmd.cmd.vendorSpecific.vndrCDW13 = lun;
        arg.nvme_cmd.cmd.vendorSpecific.vndrCDW14 = block;
        arg.nvme_cmd.cmd.vendorSpecific.vndrCDW15 = page | (prp_num << 16);


        /* send command to device */
        res = pmc_ioctl(dev_handle, NVME_IOCTL_ADMIN_CMD, &arg);
        if (res != 0)
        {
            /* for these errors see the function NvramGeneric.c/getPageContentPoll */
            if (SF_SC_INT_DEV_ERR == res)
                errorMsg = errorMsgFlashToDDr;
            if (SF_SC_INV_FIELD == res)
                errorMsg = errorMsgChTgtLunBlkOutOfRange;
            PMCLOG_2(PMCLOG_ERROR, "pmc_ioctl failed, error 0x%04x %s\n", res, errorMsg);
            return (res);
        }


        /* copy the returned PRP data to user buffer */
        memcpy(out_buf + (prp_num * PRP_SIZE), prp, PRP_SIZE);
    }
    return P_STATUS_OK;

}

/*============================================================*/
/* API: PMC_NVRAM_debug_inject_UC_errors */
/* Description: This DEBUG API to inject uncorrectable errors */
/*============================================================*/
P_STATUS PMC_NVRAM_debug_inject_UC_errors(const dev_handle_t dev_handle,    /* The device handle */
                                          NVRAM_debug_error_injection_rule * inject_UC_error /* A pointer to the inject
                                                                                           errors */
    )
{

    nvme_arg_s arg;
    int res;
    char prp[PRP_SIZE];

    memcpy(prp, inject_UC_error, sizeof(NVRAM_debug_error_injection_rule));

    bzero(&arg, sizeof(nvme_arg_s));
    arg.nvme_cmd.header.opCode = ADMIN_OPCODE_VENDOR_NVRAM; /* NVRAM command */
    arg.nvme_cmd.header.namespaceID = 0;    /* Namespace ID not in use */
    arg.nvme_cmd.header.prp1 = (uint64_t) prp;  /* PRP 1 (Output data) */
    arg.data_length = sizeof(prp);  /* PRP data len in Dwords */
    arg.nvme_cmd.cmd.vendorSpecific.vndrCDW12 = NVRAM_VENDOR_SUBCMD_INJECT_UC_ERRORS;   /* Vendor Specific DDR Content
                                                                                           command */


    /* send command to device */
    res = pmc_ioctl(dev_handle, NVME_IOCTL_ADMIN_CMD, &arg);
    if (res != 0)
    {
        PMCLOG_1(PMCLOG_ERROR, "pmc_ioctl failed, error 0x%04x.\n", res);
        return (res);
    }

    return P_STATUS_OK;
}


/*============================================================*/
/* API: PMC_NVRAM_debug_simulate_bad_block */
/* Description: This DEBUG API to inject uncorrectable errors */
/*============================================================*/
P_STATUS PMC_NVRAM_debug_simulate_bad_block(const dev_handle_t dev_handle,    /* The device handle */
                                            NVRAM_debug_error_injection_rule * simulate_bad_block /* A pointer to the simulate bad block config */ )
{

    nvme_arg_s arg;
    int res;
    char prp[PRP_SIZE];

    memcpy(prp, simulate_bad_block, sizeof(NVRAM_debug_error_injection_rule));

    bzero(&arg, sizeof(nvme_arg_s));
    arg.nvme_cmd.header.opCode = ADMIN_OPCODE_VENDOR_NVRAM; /* NVRAM command */
    arg.nvme_cmd.header.namespaceID = 0;    /* Namespace ID not in use */
    arg.nvme_cmd.header.prp1 = (uint64_t) prp;  /* PRP 1 (Output data) */
    arg.data_length = sizeof(prp);  /* PRP data len in Dwords */
    arg.nvme_cmd.cmd.vendorSpecific.vndrCDW12 = NVRAM_VENDOR_SUBCMD_SIMULATE_BAD_BLOCK;   /* Vendor Specific DDR Content
                                                                                           command */


    /* send command to device */
    res = pmc_ioctl(dev_handle, NVME_IOCTL_ADMIN_CMD, &arg);
    if (res != 0)
    {
        PMCLOG_1(PMCLOG_ERROR, "pmc_ioctl failed, error 0x%04x.\n", res);
        return (res);
    }

    return P_STATUS_OK;
}


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
    )
{

    nvme_arg_s arg;
    int res;
    char prp[PRP_SIZE];

    bzero(&arg, sizeof(nvme_arg_s));
    arg.nvme_cmd.header.opCode = ADMIN_OPCODE_VENDOR_NVRAM; /* NVRAM command */
    arg.nvme_cmd.header.namespaceID = 0;    /* Namespace ID not in use */
    arg.nvme_cmd.header.prp1 = (uint64_t) prp;  /* PRP 1 (Output data) */
    arg.data_length = sizeof(prp);  /* PRP data len in Dwords */

    arg.nvme_cmd.cmd.vendorSpecific.vndrCDW12 = NVRAM_VENDOR_SUBCMD_REGISTER_ACCESS; /* Vendor Specific register access command */
    arg.nvme_cmd.cmd.vendorSpecific.vndrCDW13 = address;
    arg.nvme_cmd.cmd.vendorSpecific.vndrCDW14 = value;
    arg.nvme_cmd.cmd.vendorSpecific.vndrCDW15 = (set_get) ? mask : 0;

    /* send command to device */
    res = pmc_ioctl(dev_handle, NVME_IOCTL_ADMIN_CMD, &arg);
    if (res != 0)
    {
        PMCLOG_1(PMCLOG_ERROR, "pmc_ioctl failed, error 0x%04x.\n", res);
        return (res);
    }

    memcpy(out,prp,sizeof(uint32_t));

    return P_STATUS_OK;
}

/*============================================================*/
/* API: PMC_NVRAM_self_test */
/* Description: This API to run self test */
/*============================================================*/
P_STATUS PMC_NVRAM_self_test(const dev_handle_t dev_handle  /* The device handle */
    )
{

    nvme_arg_s arg;
    int res;
    char prp[PRP_SIZE];

    bzero(&arg, sizeof(nvme_arg_s));
    arg.nvme_cmd.header.opCode = ADMIN_OPCODE_VENDOR_NVRAM; /* NVRAM command */
    arg.nvme_cmd.header.namespaceID = 0;    /* Namespace ID not in use */
    arg.nvme_cmd.header.prp1 = (uint64_t) prp;  /* PRP 1 (Output data) */
    arg.data_length = sizeof(prp);  /* PRP data len in Dwords */
    arg.nvme_cmd.cmd.vendorSpecific.vndrCDW12 = NVRAM_VENDOR_SUBCMD_SELF_TEST;  /* Vendor Specific run self test
                                                                                   command */

    /* send command to device */
    res = pmc_ioctl(dev_handle, NVME_IOCTL_ADMIN_CMD, &arg);
    if (res != 0)
    {
        PMCLOG_1(PMCLOG_ERROR, "pmc_ioctl failed, error 0x%04x.\n", res);
        return (res);
    }

    return P_STATUS_OK;
}


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
    )
{
	nvme_arg_s arg;
	int res;
	char prp[PRP_SIZE];
	uint32_t total_read_size;
    P_STATUS status;
    uint64_t ddr_addr;

	CHECK_PTR(out_buf);
    CHECK_PTR(geometry);

    bzero(geometry,sizeof(NVRAM_actual_geometry));
	bzero(&arg, sizeof(nvme_arg_s));

	arg.nvme_cmd.header.opCode = ADMIN_OPCODE_VENDOR_NVRAM; /* NVRAM command */
	arg.nvme_cmd.header.namespaceID = 0;    /* Namespace ID not in use */
	arg.nvme_cmd.header.prp1 = (uint64_t) prp;  /* PRP 1 (Output data) */
	arg.data_length = sizeof(prp);  /* PRP data len in Dwords */
	arg.nvme_cmd.cmd.vendorSpecific.vndrCDW12 = NVRAM_VENDOR_SUBCMD_FRIM_MAPPING_GET;

	/* first get the actual geometry */
	arg.nvme_cmd.cmd.vendorSpecific.vndrCDW13 = sizeof(NVRAM_actual_geometry);

	/* send command to device */
	res = pmc_ioctl(dev_handle, NVME_IOCTL_ADMIN_CMD, &arg);
	if (res != 0)
	{
		PMCLOG_1(PMCLOG_ERROR, "pmc_ioctl failed, error 0x%04x.\n", res);
		return (res);
	}

	memcpy(geometry, prp, sizeof(NVRAM_actual_geometry));

	/* assumption:
	 * out buffer is big enough in this case (according total_read_size calculation) */
	if (!get_all)
	{
		if (chs >= geometry->actual_channels || targets >= geometry->actual_targets || luns >= geometry->actual_luns)
		{
			PMCLOG_3(PMCLOG_ERROR, "invalid ch/target/lun (actual 0..%d 0..%d 0..%d)\n",
					geometry->actual_channels - 1,geometry->actual_targets - 1,geometry->actual_luns - 1);
			return (P_STATUS_LIB_BAD_PARAM);
		}

		total_read_size = geometry->actual_blocks*sizeof(short);
        ddr_addr = geometry->bank_address[bank_id];

        ddr_addr += (chs*MAX_TARGETS*MAX_LUNS*MAX_BLOCKS
        		     + targets*MAX_LUNS*MAX_BLOCKS + luns*MAX_BLOCKS) * sizeof(short);
	}
	else
	{
		total_read_size = MAX_CHANNELS*MAX_TARGETS*MAX_LUNS*MAX_BLOCKS*sizeof(short);
		ddr_addr = geometry->bank_address[bank_id];
	}

	/* get the ddr contents according the received geometry */
	status = PMC_NVRAM_debug_get_ddr(dev_handle,
			                         ddr_addr,
			                         total_read_size,
			                         (char *)out_buf);

	return status;
}

/*====================================================*/
/* API: PMC_NVRAM_clear_frim_mapping */
/* Description: This API instructs the firmware to clear the FRIM block mapping */
/*====================================================*/
P_STATUS PMC_NVRAM_clear_frim_mapping(const dev_handle_t dev_handle, const NVRAM_flash_bank_id_e bank_id)
{
    nvme_arg_s arg;
    int res;

    bzero(&arg, sizeof(nvme_arg_s));
    arg.nvme_cmd.header.opCode = ADMIN_OPCODE_VENDOR_NVRAM; /* NVRAM command */
    arg.nvme_cmd.header.namespaceID = 0;    /* Namespace ID not in use */
    arg.nvme_cmd.cmd.vendorSpecific.vndrCDW12 = NVRAM_VENDOR_SUBCMD_FRIM_MAPPING_CLEAR;    /* Vendor Specific time of day
                                                                                           set command */
    arg.nvme_cmd.cmd.vendorSpecific.vndrCDW13 = (uint32_t) bank_id; /* Vendor Specific bank ID */

    /* send command to device */
    res = pmc_ioctl(dev_handle, NVME_IOCTL_ADMIN_CMD, &arg);
    if (res != 0)
    {
        PMCLOG_1(PMCLOG_ERROR, "pmc_ioctl failed, error 0x%04x.\n", res);
        return (res);
    }


    return (P_STATUS_OK);

}


/*====================================================*/
/* API: PMC_NVRAM_debug_get_ddr */
/* Description: This DEBUG API gets the chunk of DDR by address and size.  */
/*====================================================*/
P_STATUS PMC_NVRAM_debug_get_ddr(const dev_handle_t dev_handle, /* The device handle */
                                 const uint64_t ddr_addr,   /* The address of ddr to be get */
                                 const uint32_t ddr_size,   /* The size of ddr to be get */
                                 char *out_buf  /* The buffer where DDR content is copied */
    )
{
    nvme_arg_s arg;
    int res;
    char prp[PRP_SIZE];
    uint32_t offset, read_size;
    uint64_t addr;

    CHECK_PTR(out_buf);

    bzero(&arg, sizeof(nvme_arg_s));
    arg.nvme_cmd.header.opCode = ADMIN_OPCODE_VENDOR_NVRAM; /* NVRAM command */
    arg.nvme_cmd.header.namespaceID = 0;    /* Namespace ID not in use */
    arg.nvme_cmd.header.prp1 = (uint64_t) prp;  /* PRP 1 (Output data) */
    arg.data_length = sizeof(prp);  /* PRP data len in Dwords */
    arg.nvme_cmd.cmd.vendorSpecific.vndrCDW12 = NVRAM_VENDOR_SUBCMD_DDR_CONTENT_GET;    /* Vendor Specific DDR Content
                                                                                           command */


    for (offset = 0; offset < ddr_size; offset += PRP_SIZE)
    {
        addr = ddr_addr + offset;
        if ((offset + PRP_SIZE) < ddr_size)
            read_size = PRP_SIZE;
        else
            read_size = ddr_size - offset;

        arg.nvme_cmd.cmd.vendorSpecific.vndrCDW13 = (addr >> 32) & 0xFFFFFFFF;
        arg.nvme_cmd.cmd.vendorSpecific.vndrCDW14 = addr & 0xFFFFFFFF;
        arg.nvme_cmd.cmd.vendorSpecific.vndrCDW15 = read_size;

        /* send command to device */
        res = pmc_ioctl(dev_handle, NVME_IOCTL_ADMIN_CMD, &arg);
        if (res != 0)
        {
            PMCLOG_1(PMCLOG_ERROR, "pmc_ioctl failed, error 0x%04x.\n", res);
            return (res);
        }

        /* copy the returned PRP data to user buffer */
        memcpy(out_buf + offset, prp, read_size);
    }


    return P_STATUS_OK;

}


/*====================================================*/
/* API: PMC_NVRAM_debug_bad_block_count_set */
/* Description: This DEBUG API sets the number of bad blocks */
/*====================================================*/
P_STATUS PMC_NVRAM_debug_bad_block_count_set(const dev_handle_t dev_handle, /* The device handle */
                                             const uint32_t block_count,    /* The number of blocks to set */
                                             const uint32_t value   /* The value to be set in the blocks */
    )
{
    nvme_arg_s arg;
    int res;


    bzero(&arg, sizeof(nvme_arg_s));
    arg.nvme_cmd.header.opCode = ADMIN_OPCODE_VENDOR_NVRAM; /* NVRAM command */
    arg.nvme_cmd.header.namespaceID = 0;    /* Namespace ID not in use */
    arg.nvme_cmd.cmd.vendorSpecific.vndrCDW12 = NVRAM_VENDOR_SUBCMD_DEBUG_BAD_BLOCK_SET;    /* Vendor Specific bad
                                                                                               block set command */
    arg.nvme_cmd.cmd.vendorSpecific.vndrCDW13 = block_count;
    arg.nvme_cmd.cmd.vendorSpecific.vndrCDW14 = value;

    /* send command to device */
    res = pmc_ioctl(dev_handle, NVME_IOCTL_ADMIN_CMD, &arg);
    if (res != 0)
    {
        PMCLOG_1(PMCLOG_ERROR, "pmc_ioctl failed, error 0x%04x.\n", res);
        return (res);
    }

    return P_STATUS_OK;

}


/*====================================================*/
/* API: PMC_NVRAM_debug_bad_block_set_by_lun */
/* Description: This DEBUG API sets blocks as bad/good by lun */
/*====================================================*/
P_STATUS PMC_NVRAM_debug_bad_block_set_by_lun(const dev_handle_t dev_handle,    /* The device handle */
                                              const uint32_t num_of_blocks, /* The number of blocks to set */
                                              const uint32_t channel,   /* channel number */
                                              const uint32_t target,    /* target number */
                                              const uint32_t lun,   /* lun number */
                                              const uint32_t first_block,   /* first block number */
                                              const NVRAM_flash_bank_id_e bank, /* bank number */
                                              const uint32_t set_as_bad /* The value to be set in the blocks (1 for
                                                                           bad, 0 for good) */
    )
{
    nvme_arg_s arg;
    int res;
    NVRAM_debug_bad_block_by_lun *params = (NVRAM_debug_bad_block_by_lun *) & arg.nvme_cmd.cmd.vendorSpecific.vndrCDW13;

    bzero(&arg, sizeof(nvme_arg_s));
    arg.nvme_cmd.header.opCode = ADMIN_OPCODE_VENDOR_NVRAM; /* NVRAM command */
    arg.nvme_cmd.header.namespaceID = 0;    /* Namespace ID not in use */
    arg.nvme_cmd.cmd.vendorSpecific.vndrCDW12 = NVRAM_VENDOR_SUBCMD_DEBUG_BAD_BLOCK_SET_LUN;    /* Vendor Specific bad
                                                                                                   block set by lun
                                                                                                   command */
    params->num_of_blocks = (uint16_t) num_of_blocks;
    params->start_block = (uint16_t) first_block;
    params->chan = (uint8_t) channel;
    params->target = (uint8_t) target;
    params->lun = (uint8_t) lun;
    params->bank = bank;
    params->is_bad = (uint8_t) set_as_bad;


    /* send command to device */
    res = pmc_ioctl(dev_handle, NVME_IOCTL_ADMIN_CMD, &arg);
    if (res != 0)
    {
        PMCLOG_1(PMCLOG_ERROR, "pmc_ioctl failed, error 0x%04x.\n", res);
        return (res);
    }

    return P_STATUS_OK;

}


/*====================================================*/
/* API: PMC_NVRAM_debug_fw_fail */
/* Description: This DEBUG API simulates the FW failure.  */
/*====================================================*/
P_STATUS PMC_NVRAM_debug_fw_fail(const dev_handle_t dev_handle, /* The device handle */
                                 const NVRAM_log_page_id_e proc_id, /* The id of processor to be failes */
                                 const NVRAM_fw_fail_test_op_id_e fail_op   /* The fail operation */
    )
{
    nvme_arg_s arg;
    int res;


    bzero(&arg, sizeof(nvme_arg_s));
    arg.nvme_cmd.header.opCode = ADMIN_OPCODE_VENDOR_NVRAM; /* NVRAM command */
    arg.nvme_cmd.header.namespaceID = 0;    /* Namespace ID not in use */
    arg.nvme_cmd.cmd.vendorSpecific.vndrCDW12 = NVRAM_VENDOR_SUBCMD_FW_FAIL_TEST;   /* Vendor Specific DDR Content
                                                                                       command */
    arg.nvme_cmd.cmd.vendorSpecific.vndrCDW13 = proc_id;
    arg.nvme_cmd.cmd.vendorSpecific.vndrCDW14 = fail_op;

    /* send command to device */
    res = pmc_ioctl(dev_handle, NVME_IOCTL_ADMIN_CMD, &arg);
    if (res != 0)
    {
        PMCLOG_1(PMCLOG_ERROR, "pmc_ioctl failed, error 0x%04x.\n", res);
        return (res);
    }

    return P_STATUS_OK;

}

/*====================================================*/
/* API: PMC_NVRAM_time_of_day_sync */
/* Description: This API sets the time of day in the controller based on the current system time */
/*====================================================*/
P_STATUS PMC_NVRAM_time_of_day_sync(const dev_handle_t dev_handle)
{
    nvme_arg_s arg;
    time_t current_time;
    int res;
    current_time = time(NULL);  /* getting the current time in seconds starting from 1/1/1970 */

    if (current_time == -1)
    {
        PMCLOG_1(PMCLOG_ERROR, "OS Time read failed, error %d.\n", current_time);
        return P_STATUS_HOST_OS_ERROR;
    }


    bzero(&arg, sizeof(nvme_arg_s));
    arg.nvme_cmd.header.opCode = ADMIN_OPCODE_VENDOR_NVRAM; /* NVRAM command */
    arg.nvme_cmd.header.namespaceID = 0;    /* Namespace ID not in use */
    arg.nvme_cmd.cmd.vendorSpecific.vndrCDW12 = NVRAM_VENDOR_SUBCMD_TIME_OF_DAY_SET;    /* Vendor Specific time of day
                                                                                           set command */
    arg.nvme_cmd.cmd.vendorSpecific.vndrCDW13 = current_time;   /* Vendor Specific time of day value */

    /* send command to device */
    res = pmc_ioctl(dev_handle, NVME_IOCTL_ADMIN_CMD, &arg);
    if (res != 0)
    {
        PMCLOG_1(PMCLOG_ERROR, "pmc_ioctl failed, error 0x%04x.\n", res);
        return (res);
    }


    return (P_STATUS_OK);

}


/*====================================================*/
/* API: PMC_NVRAM_heartbeat_listen */
/* Description: This API starts/stops the heartbeat listening by FW */
/*====================================================*/
P_STATUS PMC_NVRAM_heartbeat_listen(const dev_handle_t dev_handle,  /* The device handle */
                                    NVRAM_heartbeat_command_e heartbeat_command /* Heart beat command */
    )
{
    nvme_arg_s arg;
    int res;

    CHECK_BOUNDS(heartbeat_command, HEARTBEAT_COMMAND_START, HEARTBEAT_COMMAND_STOP);

    bzero(&arg, sizeof(nvme_arg_s));
    arg.nvme_cmd.header.opCode = ADMIN_OPCODE_VENDOR_NVRAM; /* NVRAM command */
    arg.nvme_cmd.header.namespaceID = 0;    /* Namespace ID not in use */
    arg.nvme_cmd.cmd.vendorSpecific.vndrCDW12 = NVRAM_VENDOR_SUBCMD_HEARTBEAT_COMMAND;  /* Vendor Specific heart beat
                                                                                           command */
    arg.nvme_cmd.cmd.vendorSpecific.vndrCDW13 = heartbeat_command;  /* Vendor Specific command value */

    /* send command to device */
    res = pmc_ioctl(dev_handle, NVME_IOCTL_ADMIN_CMD, &arg);
    if (res != 0)
    {
        PMCLOG_1(PMCLOG_ERROR, "pmc_ioctl failed, error 0x%04x.\n", res);
        return (res);
    }


    return (P_STATUS_OK);
}


/*========================================================================================
 * PMC_NVRAM_heartbeat
 * Description: sends heartbeat messages according to configuration
 * Argument: device handle
 * Returns: NULL
 *
 * DETAILS: While sitting in tight loop the thread performs the following:
 * 		    1) compose to the host the ioctl with heartbeat
 * 		    2) notifies that the thread is alive
 * 		    3) goes to sleep until the next heartbeat should be sent
========================================================================================*/
void *PMC_NVRAM_heartbeat(void *arg)
{
    nvme_arg_s nvme_args;
    P_STATUS res;
    dev_handle_t dev_handle = *(dev_handle_t *) arg;
    while (1)
    {

        /* compose ioctl to host */
        bzero(&nvme_args, sizeof(nvme_arg_s));
        nvme_args.nvme_cmd.header.opCode = ADMIN_OPCODE_VENDOR_NVRAM;   /* NVRAM command */
        nvme_args.nvme_cmd.header.namespaceID = 0;  /* Namespace ID not in use */
        nvme_args.nvme_cmd.cmd.vendorSpecific.vndrCDW12 = NVRAM_VENDOR_SUBCMD_HEARTBEAT_INDICATION; /* Vendor Specific
                                                                                                       time of day set
                                                                                                       command */

        /* send command to device */
        res = pmc_ioctl(dev_handle, NVME_IOCTL_ADMIN_CMD, &nvme_args);
        if (res != 0)
        {
            PMCLOG_1(PMCLOG_ERROR, "pmc_ioctl failed, error 0x%04x.\n", res);
	    return (NULL);
        }

        /* goes to sleep until the next heartbeat should be sent */
        OSSRV_wait(g_heartbeat_interval * 1000); // * 1000 to convert seconds to milliseconds
    }

    return (NULL);

}

/*========================================================================================
 * get_logical_block_size
 * Description: look in sysfs for logical block size of the corresponding namespace
 * Arguments:
 *  * device handle - handler to block device corresponding to the namespace
 *  * ponter to return the logical_block_size
 *
 * Returns:
 *   * P_STATUS_OK - on success
 *   * P_STATUS_HOST_OS_ERROR - on system call failure
 *
 * DETAILS: Goto sysfs entry corresponding to the (major, minor) couple of the device to read the
 *          logical block size
========================================================================================*/
#ifdef OS_FREE_BSD
/*The ioctl will work only if global argument is provided*/
uint64_t lbsize = 0;
P_STATUS get_logical_block_size(const dev_handle_t dev_handle, uint64_t * logical_block_size)
{
  
    int res; 
   
    CHECK_PTR(logical_block_size);
    /* Get the sector size from the nvme block driver using ioctl.FreeBSD does not export this infomration in sysfs*/
    res = ioctl(dev_handle.nvme_block_fd, DIOCGSECTORSIZE, &lbsize);
    if (res< 0)
    {
    	PMCLOG_2(PMCLOG_ERROR, "Failed to get Logical block size errno %d (%s)\n", errno, strerror(errno));
    	return (P_STATUS_HOST_OS_ERROR);
    }
  
    *logical_block_size = lbsize;
    return P_STATUS_OK;
}
#else/*Linux OS case*/
P_STATUS get_logical_block_size(const dev_handle_t dev_handle, uint64_t * logical_block_size)
{
    char sysfs_entry_LB_size[MAX_PATH_LEN];
    char buf[5];                /* to hold either "512" or "4096" */
    int fd, base = 10;
    char *endptr;

    CHECK_PTR(logical_block_size);

    sprintf(sysfs_entry_LB_size, "%s%d:%d%s", SYSFS_ENTRY_LB_SIZE_PREF,
            major(dev_handle.device_info.blk_dev_stat.st_rdev),
            minor(dev_handle.device_info.blk_dev_stat.st_rdev), SYSFS_ENTRY_LB_SIZE_SUFF);

    if ((fd = open(sysfs_entry_LB_size, S_IRUSR)) < 0)
    {
        PMCLOG_3(PMCLOG_ERROR, "Failed to open '%s', errno %d (%s).\n", sysfs_entry_LB_size, errno, strerror(errno));
        return (P_STATUS_HOST_OS_ERROR);
    }

    if (read(fd, buf, sizeof(buf)) < 0)
    {
        close(fd);
        PMCLOG_3(PMCLOG_ERROR, "Failed to read '%s', errno %d (%s).\n", sysfs_entry_LB_size, errno, strerror(errno));
        return (P_STATUS_HOST_OS_ERROR);
    }

    /* 
     * the read buffer is not a null terminated and thus one need to check the
     * if it is 512 or 4096 byte logical block. We need to check only the first
     * char since block sizes without metadata are 512 and 4096 and with
     * metadata 520 and 4160.
     */
    if (buf[0] == '5')
        buf[3] = '\0';
    else
        buf[4] = '\0';

    *logical_block_size = strtol(buf, &endptr, base);

    close(fd);
    return P_STATUS_OK;
}
#endif
/*========================================================================================
 * are_DMA_RAMDISK_overlapped
 * Description: checks if RAMDISK and DMI are overlapped
 *
 * Arguments:
 *  * device handle - handler to block device corresponding to the namespace
 *  * ponter to return the dmi_size (which is equal to ramdisk size when RAMDISK and DMI are overlapped)
 *
 * Returns:
 *   * P_STATUS_OK - on success
 *   * P_STATUS_HOST_OS_ERROR - on system call failure
 *
 * DETAILS:  in case of overlap ramdisk_size = dmi_size = ddr size
========================================================================================*/
P_STATUS are_DMA_RAMDISK_overlapped(const dev_handle_t dev_handle, uint64_t * size)
{
    NVRAM_info_data_u info_data;
    uint64_t dmi_size, ramdisk_size;
    P_STATUS status;

    /* Get info NVRAM_INFO_GROUP_DMI to check for overlap */
    status = PMC_NVRAM_info_get(dev_handle, NVRAM_INFO_GROUP_DMI, &info_data);
    if (status != P_STATUS_OK)
    {
        PMCLOG_1(PMCLOG_ERROR, "PMC_NVRAM_info_get was failed called with %s\n", "NVRAM_INFO_GROUP_DMI");
        return status;
    }

    dmi_size = info_data.dmi.size;

    /* Get info NVRAM_INFO_GROUP_RAMDISK to check for overlap */
    status = PMC_NVRAM_info_get(dev_handle, NVRAM_INFO_GROUP_RAMDISK, &info_data);
    if (status != P_STATUS_OK)
    {
        PMCLOG_1(PMCLOG_ERROR, "call to PMC_NVRAM_info_get was failed with %s\n", "NVRAM_INFO_GROUP_RAMDISK");
        return status;
    }

    ramdisk_size = info_data.ramdisk.size;


    /* Get info NVRAM_INFO_GROUP_DDR to check for overlap */
    status = PMC_NVRAM_info_get(dev_handle, NVRAM_INFO_GROUP_DDR, &info_data);
    if (status != P_STATUS_OK)
    {
        PMCLOG_1(PMCLOG_ERROR, "call to PMC_NVRAM_info_get was failed with %s\n", "NVRAM_INFO_GROUP_DDR");
        return status;
    }

    /* 
     * in case of overlap ramdisk_size = dmi_size = ddr size
     */
    if (info_data.ddr.size >= ramdisk_size + dmi_size)
    {
        PMCLOG_0(PMCLOG_ERROR, "DMI and RAMDISK overlap should be activated first.\n");
        return P_STATUS_LIB_UNSUPPORTED;
    }

    *size = ramdisk_size;

    return P_STATUS_OK;

}




/*====================================================*/
/* API: PMC_NVRAM_lba_to_addr_map_get */
/*====================================================*/
P_STATUS PMC_NVRAM_lba_to_addr_map_get(const dev_handle_t dev_handle,   /* The device handle */
                                       const uint32_t lba,  /* The LBA */
                                       uint64_t * addr) /* The DDR Adrress of the LBA */
{


    uint64_t logical_block_size;
    uint64_t dmi_size;          /* which should be equal to ramdisk size */
    P_STATUS res;

    CHECK_PTR(addr);

    if ((res = are_DMA_RAMDISK_overlapped(dev_handle, &dmi_size)) != P_STATUS_OK)
        return res;

    if ((res = get_logical_block_size(dev_handle, &logical_block_size)) != P_STATUS_OK)
        return res;

    *addr = logical_block_size * lba;

    if ((*addr) + logical_block_size > dmi_size)
    {
        PMCLOG_0(PMCLOG_ERROR, "Requested lba is beyond the allowable range.\n");
        return (P_STATUS_LIB_BAD_PARAM);
    }

    return (P_STATUS_OK);
}


/*====================================================*/
/* API: PMC_NVRAM_addr_to_lba_map_get */
/*====================================================*/
P_STATUS PMC_NVRAM_addr_to_lba_map_get(const dev_handle_t dev_handle,   /* The device handle */
                                       const uint64_t addr, /* The DDR Adrress */
                                       uint32_t * lba)  /* The LBA of the DDR Adrress */
{

    uint64_t logical_block_size;
    uint64_t dmi_size;          /* which should be equal to ramdisk size */
    P_STATUS res;

    /* check params */
    CHECK_PTR(lba);

    if ((res = are_DMA_RAMDISK_overlapped(dev_handle, &dmi_size)) != P_STATUS_OK)
        return res;

    if ((res = get_logical_block_size(dev_handle, &logical_block_size)) != P_STATUS_OK)
        return res;

    *lba = (addr / logical_block_size);

    if (addr >= dmi_size)
    {
        PMCLOG_0(PMCLOG_ERROR, "Requested address is beyond the allowable range.\n");
        return (P_STATUS_LIB_BAD_PARAM);
    }

    return (P_STATUS_OK);
}


/*====================================================*/
/* API: PMC_NVRAM_debug_config_set                    */
/*====================================================*/
P_STATUS PMC_NVRAM_debug_config_set(const dev_handle_t dev_handle,
                              const NVRAM_debug_config_type_e config_type, const NVRAM_debug_config_data_u * config_data)
{
    int res;
    char prp[PRP_SIZE];
    nvme_arg_s arg;

    /* check params */
    CHECK_BOUNDS(config_type, NVRAM_DEBUG_CONFIG_TYPE_FIRST, NVRAM_DEBUG_CONFIG_TYPE_NUM - 1);
    CHECK_PTR(config_data);

    // check config_data param
    switch (config_type)
    {
    case NVRAM_DEBUG_CONFIG_TYPE_IGNORE_BAD_BLOCK_THRESHOLD:
        // boolean - nothing to check
        break;


    default:
        PMCLOG_1(PMCLOG_ERROR, "Invalid config_type.\n", config_type);
        return (P_STATUS_LIB_BAD_PARAM);
    }

    bzero(prp, sizeof(prp));
    memcpy(prp, config_data, sizeof(NVRAM_debug_config_data_u));

    bzero(&arg, sizeof(nvme_arg_s));
    arg.nvme_cmd.header.opCode = ADMIN_OPCODE_VENDOR_NVRAM; /* NVRAM command */
    arg.nvme_cmd.header.namespaceID = 0;    /* Namespace ID not in use */
    arg.nvme_cmd.header.prp1 = (uint64_t) prp;  /* PRP 1 (Input data) */
    arg.data_length = sizeof(prp);  /* PRP data len in Dwords */
    arg.nvme_cmd.cmd.vendorSpecific.vndrCDW12 = NVRAM_VENDOR_SUBCMD_DEBUG_CONFIG_SET; /* Vendor Specific Config Set command */
    arg.nvme_cmd.cmd.vendorSpecific.vndrCDW13 = (uint32_t) config_type; /* Vendor Specific Config Type */

    /* send command to device */
    res = pmc_ioctl(dev_handle, NVME_IOCTL_ADMIN_CMD, &arg);
    if (res != 0)
    {
        PMCLOG_1(PMCLOG_ERROR, "pmc_ioctl failed, error 0x%04x.\n", res);
        return (res);
    }

    return (P_STATUS_OK);
}


/*====================================================*/
/* API: PMC_NVRAM_debug_config_get                    */
/*====================================================*/
P_STATUS PMC_NVRAM_debug_config_get(const dev_handle_t dev_handle,
                              const NVRAM_debug_config_type_e config_type, NVRAM_debug_config_data_u * config_data)
{
    int res;
    char prp[PRP_SIZE];
    nvme_arg_s arg;


    /* check params */
    CHECK_BOUNDS(config_type, NVRAM_DEBUG_CONFIG_TYPE_FIRST, NVRAM_DEBUG_CONFIG_TYPE_NUM - 1);
    CHECK_PTR(config_data);



    bzero(prp, sizeof(prp));
    bzero(config_data, sizeof(NVRAM_debug_config_data_u));

    bzero(&arg, sizeof(nvme_arg_s));
    arg.nvme_cmd.header.opCode = ADMIN_OPCODE_VENDOR_NVRAM; /* NVRAM command */
    arg.nvme_cmd.header.namespaceID = 0;    /* Namespace ID not in use */
    arg.nvme_cmd.header.prp1 = (uint64_t) prp;  /* PRP 1 (Output data) */
    arg.data_length = sizeof(prp);  /* PRP data len in Dwords */
    arg.nvme_cmd.cmd.vendorSpecific.vndrCDW12 = NVRAM_VENDOR_SUBCMD_DEBUG_CONFIG_GET; /* Vendor Specific Config Get command */
    arg.nvme_cmd.cmd.vendorSpecific.vndrCDW13 = (uint32_t) config_type; /* Vendor Specific Config Type */


    /* send command to device */
    res = pmc_ioctl(dev_handle, NVME_IOCTL_ADMIN_CMD, &arg);
    if (res != 0)
    {
        PMCLOG_1(PMCLOG_ERROR, "pmc_ioctl failed, error 0x%04x.\n", res);
        return (res);
    }

    /* copy the returned PRP data to user struct */
    memcpy(config_data, prp, sizeof(NVRAM_debug_config_data_u));

    return (P_STATUS_OK);
}


/*====================================================*/
/* API: PMC_NVRAM_pfi_get */
/* Description: This API retrieves Product Feature Info data from device */
/*====================================================*/
P_STATUS PMC_NVRAM_pfi_get(const dev_handle_t dev_handle,  uint32_t pfi_data_size,
                           char	 *pfi_data )
{
    int res;
    char prp[PRP_SIZE];
    nvme_arg_s arg;

    /* check params */
    CHECK_PTR(pfi_data);

    /* check the buffer size */
    if (pfi_data_size < PRP_SIZE)
    {
        PMCLOG_1(PMCLOG_ERROR, "buffer size %u bytes is not enough.\n", pfi_data_size);
        return (P_STATUS_LIB_TOO_BIG);
    }
		
    bzero(prp, sizeof(prp));

    bzero(&arg, sizeof(nvme_arg_s));
    arg.nvme_cmd.header.opCode = ADMIN_OPCODE_VENDOR_NVRAM; /* NVRAM command */
    arg.nvme_cmd.header.namespaceID = 0;    /* Namespace ID not in use */
    arg.nvme_cmd.header.prp1 = (uint64_t) prp;  /* PRP 1 (Output data) */
    arg.data_length = sizeof(prp);  /* PRP data len in Dwords */
    arg.nvme_cmd.cmd.vendorSpecific.vndrCDW12 = NVRAM_VENDOR_SUBCMD_PROD_FEATURE_INFO_GET;    /* Vendor Specific Product Feature
                                                                                               Info Get command */

    res = pmc_ioctl(dev_handle, NVME_IOCTL_ADMIN_CMD, &arg);
    if (res != 0)
    {
        PMCLOG_1(PMCLOG_ERROR, "pmc_ioctl failed, error 0x%04x.\n", res);
        return (res);
    }

    /* copy the returned PRP data to user buffer */
    memcpy(pfi_data, prp, PRP_SIZE);

    return (P_STATUS_OK);
}

/*====================================================*/
/* API: PMC_NVRAM_ramdisk_enc_key_set */
/* Description: This API sets RAM disk encription key to device */
/*====================================================*/
     P_STATUS PMC_NVRAM_ramdisk_enc_key_set(const dev_handle_t dev_handle, 
	                       const NVRAM_ramdisk_enc_key_s * ramdisk_enc_key)
{
    int res;
    char prp[PRP_SIZE];
    nvme_arg_s arg;

    /* check params */
    CHECK_PTR(ramdisk_enc_key);

    bzero(prp, sizeof(prp));
    memcpy(prp, ramdisk_enc_key, sizeof(NVRAM_ramdisk_enc_key_s));

    bzero(&arg, sizeof(nvme_arg_s));
    arg.nvme_cmd.header.opCode = ADMIN_OPCODE_VENDOR_NVRAM; /* NVRAM command */
    arg.nvme_cmd.header.namespaceID = 0;    /* Namespace ID not in use */
    arg.nvme_cmd.header.prp1 = (uint64_t) prp;  /* PRP 1 (Input data) */
    arg.data_length = sizeof(prp);  /* PRP data len in Dwords */
    arg.nvme_cmd.cmd.vendorSpecific.vndrCDW12 = NVRAM_VENDOR_SUBCMD_RAMDISK_ENC_KEY_SET; /* Vendor Specific Config Set command */

    /* send command to device */
    res = pmc_ioctl(dev_handle, NVME_IOCTL_ADMIN_CMD, &arg);
    if (res != 0)
    {
        PMCLOG_1(PMCLOG_ERROR, "pmc_ioctl failed, error 0x%04x.\n", res);
        return (res);
    }

    return (P_STATUS_OK);
}


