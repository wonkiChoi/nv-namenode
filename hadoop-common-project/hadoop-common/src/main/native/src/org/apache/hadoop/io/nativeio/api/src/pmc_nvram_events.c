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
* Description                  : PMC NVRAM Events
*
* $Revision: 1.1 $
* $Date: 2013/09/23 07:07:42 $
* $Author: shivatzi $
* Release $Name:  $
*
****************************************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/ioctl.h>
#include "pmc_nvram_api.h"
#include "pmc_nvram_api_expo.h"
#include "pmc_log.h"
#include "pmc_nvme.h"
#include "pmc_nvram_events.h"
#include "pmc_utils.h"
#include "pmc_ossrv.h"


/**********************************************************/
/* Internal Defines */
/**********************************************************/
#define EVENT_FROM_NOW					0

#define EVENTS_POLLING_INTERVAL_DEF		1000    /* milliseconds */

#define UNDEFINED_FD				-1


/**********************************************************/
/* Internal Types */
/**********************************************************/
/* Event entry in device events list */
typedef struct
{
    NVRAM_event_type_e event_type;  /* Event type */
    PMC_NVRAM_gen_event_handler_t event_handler;    /* Event hook handler */

} Pmc_event_entry_s;


/* Device entry in events database */
typedef struct
{
    p_bool_t in_use;            /* Is device entry in use */
    dev_handle_t dev_handle;    /* Device handle */
    uint64_t first_event_index; /* First event to poll */
    Pmc_event_entry_s events[MAX_EVENTS_PER_DEV];   /* List of events */

} Pmc_dev_entry_s;

/* Pointer to event get function */
typedef int (*pmc_event_get_fn_t) (char *prp,   /* PRP to copy from */
                                   uint32_t subtype,    /* Subtype bitmask */
                                   NVRAM_event_data_u * data_ptr,   /* Pointer to data to ber filled */
                                   int *offset);    /* Offset in PRP - in/out */



/**********************************************************/
/* Internal Variables */
/**********************************************************/
/* Events database semaphore */
static OSSRV_counting_semaphore_t g_events_sem;

/* Indicate whether events package already initialized */
static p_bool_t is_event_pkg_init = PMC_FALSE;

/* Events polling thread */
static OSSRV_thread_id_t g_events_thread;

/* Events database */
static Pmc_dev_entry_s events_db[MAX_NUM_DEVICES];


static uint32_t g_events_polling_interval = EVENTS_POLLING_INTERVAL_DEF;

static p_bool_t g_thread_alive = PMC_FALSE;

/* event thread id */
static OSSRV_thread_id_t g_event_thread_id;

/**********************************************************/
/* Internal Functions */
/**********************************************************/

#define COPY_PRP_TO_LOCAL(field, size, prp, offs) \
{		\
   prp_to_local##size(&field, prp, offs);    \
   offs +=sizeof(field);            \
}

/* This func retrieves vault event from NVRAM cards */
static int Pmc_event_vault_get(char *prp,   /* PRP to copy from */
                               uint32_t subtype,    /* Subtype bitmask */
                               NVRAM_event_data_u * data_ptr,   /* Pointer to data to ber filled */
                               int *offset) /* Offset in PRP - in/out */
{
    /* check params */
    CHECK_PTR(prp);
    CHECK_PTR(data_ptr);
    CHECK_PTR(offset);

    /* TODO: modify it someway */
    /* COPY_PRP_TO_LOCAL(data_ptr->vault_data.backup.status, 32, prp, *offset); */

    /* Retrieve BACKUP event data from PRP */
    prp_to_local32(&data_ptr->vault_data.backup.status, prp, *offset);
    *offset += 4;
    prp_to_local64(&data_ptr->vault_data.backup.size, prp, *offset);
    *offset += 8;
#if 0                           // TBD
    prp_to_local32(&data_ptr->vault_data.backup.bank_id, prp, *offset);
    *offset += 4;
    prp_to_local32(&data_ptr->vault_data.backup.reason, prp, *offset);
    *offset += 4;
#endif

    /* Retrieve RESTORE event data from PRP */
    prp_to_local32(&data_ptr->vault_data.restore.status, prp, *offset);
    *offset += 4;
    prp_to_local64(&data_ptr->vault_data.restore.size, prp, *offset);
    *offset += 8;
#if 0                           // TBD
    prp_to_local32(&data_ptr->vault_data.restore.bank_id, prp, *offset);
    *offset += 4;
#endif

    /* Retrieve ERASE event data from PRP */
    prp_to_local32(&data_ptr->vault_data.erase.status, prp, *offset);
    *offset += 4;
    prp_to_local64(&data_ptr->vault_data.erase.size, prp, *offset);
    *offset += 8;
#if 0                           // TBD
    prp_to_local32(&data_ptr->vault_data.erase.bank_id, prp, *offset);
    *offset += 4;
#endif

    /* Retrieve BAD_BLOCK_SCAN event data from PRP */
    prp_to_local32(&data_ptr->vault_data.bad_block_scan.status, prp, *offset);
    *offset += 4;
    prp_to_local64(&data_ptr->vault_data.bad_block_scan.size, prp, *offset);
    *offset += 8;
#if 0                           // TBD
    prp_to_local32(&data_ptr->vault_data.bad_block_scan.bank_id, prp, *offset);
    *offset += 4;
#endif

    return (P_STATUS_OK);
}

/* This func retrieves cpu event from NVRAM cards */
static int Pmc_event_cpu_get(char *prp, /* PRP to copy from */
                             uint32_t subtype,  /* Subtype bitmask */
                             NVRAM_event_data_u * data_ptr, /* Pointer to data to ber filled */
                             int *offset)   /* Offset in PRP - in/out */
{
    /* check params */
    CHECK_PTR(prp);
    CHECK_PTR(data_ptr);
    CHECK_PTR(offset);

    prp_to_local8(&data_ptr->cpu_data.temperature.above_threshold, prp, *offset);
    *offset += 4;

    prp_to_local32(&data_ptr->cpu_data.temperature.value, prp, *offset);
    *offset += 4;

    prp_to_local8(&data_ptr->cpu_data.critical_temperature.above_threshold, prp, *offset);
    *offset += 4;

    prp_to_local32(&data_ptr->cpu_data.critical_temperature.value, prp, *offset);
    *offset += 4;

    return (P_STATUS_OK);
}

/* This func retrieves system event from NVRAM cards */
static int Pmc_event_system_get(char *prp,  /* PRP to copy from */
                                uint32_t subtype,   /* Subtype bitmask */
                                NVRAM_event_data_u * data_ptr,  /* Pointer to data to ber filled */
                                int *offset)    /* Offset in PRP - in/out */
{
    /* check params */
    CHECK_PTR(prp);
    CHECK_PTR(data_ptr);
    CHECK_PTR(offset);

    prp_to_local8(&data_ptr->system_data.temperature.above_threshold, prp, *offset);
    *offset += 4;

    prp_to_local32(&data_ptr->system_data.temperature.value, prp, *offset);
    *offset += 4;

    prp_to_local8(&data_ptr->system_data.critical_temperature.above_threshold, prp, *offset);
    *offset += 4;

    prp_to_local32(&data_ptr->system_data.critical_temperature.value, prp, *offset);
    *offset += 4;

    return (P_STATUS_OK);
}

/* This func retrieves backup power events from NVRAM cards */
static int Pmc_event_backup_power_get(char *prp,    /* PRP to copy from */
                                      uint32_t subtype, /* Subtype bitmask */
                                      NVRAM_event_data_u * data_ptr,    /* Pointer to data to ber filled */
                                      int *offset)  /* Offset in PRP - in/out */
{
    /* check params */
    CHECK_PTR(prp);
    CHECK_PTR(data_ptr);
    CHECK_PTR(offset);

    prp_to_local8(&data_ptr->backup_power_data.temperature.above_threshold, prp, *offset);
    *offset += 4;
    prp_to_local32(&data_ptr->backup_power_data.temperature.value, prp, *offset);
    *offset += 4;

    prp_to_local8(&data_ptr->backup_power_data.critical_temperature.above_threshold, prp, *offset);
    *offset += 4;
    prp_to_local32(&data_ptr->backup_power_data.critical_temperature.value, prp, *offset);
    *offset += 4;

    prp_to_local8(&data_ptr->backup_power_data.charge_level.fully_charged, prp, *offset);
    *offset += 4;
    prp_to_local32(&data_ptr->backup_power_data.charge_level.value, prp, *offset);
    *offset += 4;

    prp_to_local8(&data_ptr->backup_power_data.voltage.above_threshold, prp, *offset);
    *offset += 4;
    prp_to_local32(&data_ptr->backup_power_data.voltage.value, prp, *offset);
    *offset += 4;

    prp_to_local8(&data_ptr->backup_power_data.current.above_threshold, prp, *offset);
    *offset += 4;
    prp_to_local32(&data_ptr->backup_power_data.current.value, prp, *offset);
    *offset += 4;

    prp_to_local8(&data_ptr->backup_power_data.balance.imbalance, prp, *offset);
    *offset += 4;
    prp_to_local32(&data_ptr->backup_power_data.balance.voltage_cap1, prp, *offset);
    *offset += 4;
    prp_to_local32(&data_ptr->backup_power_data.balance.voltage_cap2, prp, *offset);
    *offset += 4;

    prp_to_local8(&data_ptr->backup_power_data.learning.failed, prp, *offset);
    *offset += 4;

    prp_to_local32(&data_ptr->backup_power_data.health.state, prp, *offset);
    *offset += 4;

    prp_to_local32(&data_ptr->backup_power_data.power_src.new_power_src, prp, *offset);
    *offset += 4;

    return (P_STATUS_OK);
}

/* This func retrieves ddr ecc events from NVRAM cards */
static int Pmc_event_ddr_ecc_get(char *prp, /* PRP to copy from */
                                 uint32_t subtype,  /* Subtype bitmask */
                                 NVRAM_event_data_u * data_ptr, /* Pointer to data to ber filled */
                                 int *offset)   /* Offset in PRP - in/out */
{
    /* check params */
    CHECK_PTR(prp);
    CHECK_PTR(data_ptr);
    CHECK_PTR(offset);

    prp_to_local64(&data_ptr->ddr_ecc_data.address, prp, *offset);
    *offset += 8;

    return (P_STATUS_OK);
}

/* This func retrieves general error events from NVRAM cards */
static int Pmc_event_general_get(char *prp, /* PRP to copy from */
                                 uint32_t subtype,  /* Subtype bitmask */
                                 NVRAM_event_data_u * data_ptr, /* Pointer to data to ber filled */
                                 int *offset)   /* Offset in PRP - in/out */
{
    /* check params */
    CHECK_PTR(prp);
    CHECK_PTR(data_ptr);
    CHECK_PTR(offset);

    prp_to_local32(&data_ptr->general_data.bist.status, prp, *offset);
    *offset += 4;
    prp_to_local32(&data_ptr->general_data.bist.num_of_errors, prp, *offset);
    *offset += 4;

    return (P_STATUS_OK);
}

/* This func retrieves device health events from NVRAM cards */
static int Pmc_event_dev_health_get(char *prp, /* PRP to copy from */
                                 uint32_t subtype,  /* Subtype bitmask */
                                 NVRAM_event_data_u * data_ptr, /* Pointer to data to ber filled */
                                 int *offset)   /* Offset in PRP - in/out */
{
    /* check params */
    CHECK_PTR(prp);
    CHECK_PTR(data_ptr);
    CHECK_PTR(offset);

    prp_to_local32(&data_ptr->dev_health_data.device_health_bitmask, prp, *offset);
    *offset += 4;

    return (P_STATUS_OK);
}



static pmc_event_get_fn_t pmc_event_get_func[NVRAM_EVENT_TYPE_NUMBER] = {
    Pmc_event_vault_get, 
    Pmc_event_cpu_get, 
    Pmc_event_system_get,
    Pmc_event_backup_power_get, 
    Pmc_event_ddr_ecc_get, 
    Pmc_event_general_get,
    Pmc_event_dev_health_get
};


/* This func retrieves lateset events list from NVRAM cards */
int Pmc_events_get(const dev_handle_t dev_handle,
                   const uint64_t first_event_index, const uint32_t time_of_day, Pmc_events_prp_s * events_prp)
{
    int res;
    char prp[PRP_SIZE];
    nvme_arg_s arg;
    uint32_t i;
    int offset;

    /* check params */
    CHECK_PTR(events_prp);

    bzero(prp, sizeof(prp));

    bzero(&arg, sizeof(nvme_arg_s));
    arg.nvme_cmd.header.opCode = ADMIN_OPCODE_VENDOR_NVRAM; /* NVRAM command */
    arg.nvme_cmd.header.namespaceID = 0;    /* Namespace ID not in use */
    arg.nvme_cmd.header.prp1 = (uint64_t) prp;  /* PRP 1 (Output data) */
    arg.data_length = sizeof(prp);  /* PRP data len in Dwords */
    arg.nvme_cmd.cmd.vendorSpecific.vndrCDW12 = NVRAM_VENDOR_SUBCMD_EVENTS_GET; /* Vendor Specific Events Get command */
    arg.nvme_cmd.cmd.vendorSpecific.vndrCDW13 = (uint32_t) (first_event_index); /* First event index LSB */
    arg.nvme_cmd.cmd.vendorSpecific.vndrCDW14 = (uint32_t) (first_event_index >> 32);   /* First event index MSB */
    arg.nvme_cmd.cmd.vendorSpecific.vndrCDW15 = (uint32_t) time_of_day; /* ToD */


    /* send command to device */
    res = pmc_ioctl_ext(dev_handle, NVME_IOCTL_ADMIN_CMD, &arg, PMC_FALSE /* displayBusyError */ );
    if (res != 0)
    {
        if (res != P_STATUS_NVME_BUSY)  // For busy do not print error 
        {
            PMCLOG_1(PMCLOG_ERROR, "pmc_ioctl failed, error 0x%04x.\n", res);
        }

        return (res);
    }

    /* save the returned PRP data to */
    offset = 0;

    /* retrieve last_event_index */
    prp_to_local64(&events_prp->last_event_index, prp, offset);
    offset += 8;

    /* retrieve event_count */
    prp_to_local32(&events_prp->event_count, prp, offset);
    offset += 4;

    /* retrieve events list */
    for (i = 0; i < events_prp->event_count; i++)
    {
        /* retrieve events type */
        prp_to_local32(&events_prp->events_data[i].type, prp, offset);
        CHECK_BOUNDS(events_prp->events_data[i].type, NVRAM_EVENT_TYPE_FIRST, NVRAM_EVENT_TYPE_NUMBER - 1);
        offset += 4;

        /* retrieve events state */
        prp_to_local32(&events_prp->events_data[i].subtype, prp, offset);
        offset += 4;

        /* retrieve events data */
        res = pmc_event_get_func[events_prp->events_data[i].type] (prp,
                                                                   events_prp->events_data[i].subtype,
                                                                   &events_prp->events_data[i].data, &offset);

    }


    return (P_STATUS_OK);
}




/* This func send event register to the NVRAM card */
P_STATUS Pmc_event_fw_register(const dev_handle_t dev_handle, const NVRAM_event_type_e event_type, p_bool_t reg)
{
    int res;
    nvme_arg_s arg;

    CHECK_BOUNDS(event_type, NVRAM_EVENT_TYPE_FIRST, NVRAM_EVENT_TYPE_NUMBER - 1);

    bzero(&arg, sizeof(nvme_arg_s));
    arg.nvme_cmd.header.opCode = ADMIN_OPCODE_VENDOR_NVRAM; /* NVRAM command */
    arg.nvme_cmd.header.namespaceID = 0;    /* Namespace ID not in use */
    arg.nvme_cmd.cmd.vendorSpecific.vndrCDW12 = NVRAM_VENDOR_SUBCMD_EVENT_REGISTER; /* Vendor Specific Event Register
                                                                                       command */
    arg.nvme_cmd.cmd.vendorSpecific.vndrCDW13 = (uint32_t) event_type;  /* Vendor Specific Event Type */
    arg.nvme_cmd.cmd.vendorSpecific.vndrCDW14 = (uint32_t) reg; /* Register / Unregister */

    /* send command to device */
    res = pmc_ioctl(dev_handle, NVME_IOCTL_ADMIN_CMD, &arg);
    if (res != 0)
    {
        PMCLOG_1(PMCLOG_ERROR, "pmc_ioctl failed, error 0x%04x.\n", res);
        return (res);
    }

    return (P_STATUS_OK);
}














/* This func retrieve device entry from events database */
Pmc_dev_entry_s *Pmc_event_db_dev_get(const dev_handle_t dev_handle)
{
    int i;

    /* iterate all devices to find device entry by device handle */
    for (i = 0; i < MAX_NUM_DEVICES; i++)
    {
        if (events_db[i].in_use && events_db[i].dev_handle.nvme_fd == dev_handle.nvme_fd)
            return (&events_db[i]);
    }

    return (NULL);
}


/* This func retrieve event entry from device events list */
Pmc_event_entry_s *Pmc_event_db_event_get(Pmc_dev_entry_s * dev_entry,
                                          const NVRAM_event_type_e event_type,
                                          const PMC_NVRAM_gen_event_handler_t event_handler)
{
    int i;

    /* iterate all events in device entry to find event entry by event type and event handler */
    for (i = 0; i < MAX_EVENTS_PER_DEV; i++)
    {
        if (dev_entry->events[i].event_handler != NULL &&
            dev_entry->events[i].event_type == event_type && dev_entry->events[i].event_handler == event_handler)
        {
            return (&dev_entry->events[i]);
        }
    }

    return (NULL);
}


/* This func check if any handler is registered to event */
p_bool_t Pmc_event_is_registered(Pmc_dev_entry_s * dev_entry, const NVRAM_event_type_e event_type)
{
    int i;

    /* iterate all events in device entry to find event entry by event type and event handler */
    for (i = 0; i < MAX_EVENTS_PER_DEV; i++)
    {
        if (dev_entry->events[i].event_handler != NULL && dev_entry->events[i].event_type == event_type)
        {
            return PMC_TRUE;
        }
    }
    return PMC_FALSE;
}


/* This func check if device entry has no more events in its events list */
p_bool_t Pmc_event_db_entry_is_empty(const Pmc_dev_entry_s * dev_entry)
{
    int i;

    /* iterate all events in device entry */
    for (i = 0; i < MAX_EVENTS_PER_DEV; i++)
    {
        if (dev_entry->events[i].event_handler != NULL)
        {
            return (PMC_FALSE);     /* if any event exist return empty=false */
        }
    }

    return (PMC_TRUE);              /* no event found */
}

/* This func fire the events to clients */
void Pmc_event_fire(const Pmc_dev_entry_s * dev_entry, const Pmc_events_prp_s * events_prp)
{
    int i, j;

    /* iterate all registered event handlers */
    for (i = 0; i < MAX_EVENTS_PER_DEV; i++)
    {
        if (dev_entry->events[i].event_handler != NULL)
        {
            /* iterate all events in the PRP (run in reverse order since sent the order was reversed by FW) */
            for (j = events_prp->event_count - 1; j >= 0; j--)
            {
                /* if such event exist then call its hook handler */
                if (dev_entry->events[i].event_type == events_prp->events_data[j].type)
                {
                    dev_entry->events[i].event_handler(dev_entry->dev_handle,
                                                       events_prp->events_data[j].type,
                                                       events_prp->events_data[j].subtype,
                                                       &events_prp->events_data[j].data);
                }
            }
        }
    }
}

/* 
 *  ask FW what is the next event slot. 
 */

int updateDevEntryEventIndex(int i)
{


    int res;
    uint32_t time_of_day = 0;
    Pmc_events_prp_s events_prp;

    memset(&events_prp, 0, sizeof(events_prp));

    /* poll the latest events list from the controller */
    res = Pmc_events_get(events_db[i].dev_handle, events_db[i].first_event_index, time_of_day, &events_prp);
    if (res == 0)
    {
        /* update the index of the first event to poll at the next polling */
        events_db[i].first_event_index = events_prp.last_event_index;
    }

    return res;


}


/* This func register event to events database */
int Pmc_event_handler_register(const dev_handle_t dev_handle,
                               const NVRAM_event_type_e event_type,
                               const PMC_NVRAM_gen_event_handler_t event_handler, uint16_t * event_context_id)
{
    int i, res;
    Pmc_dev_entry_s *dev_entry;
    Pmc_event_entry_s *event_entry;
    p_bool_t fw_register = PMC_FALSE;

    /* 
     * return error if this function is called from the event handler 
     * NOTE, we use pthread anly when g_thread_mode is AUTO 
     */
    if (g_thread_mode == NVRAM_OPERATION_MODE_AUTO) 
    {
        if (g_event_thread_id == OSSRV_thread_self())
        {
            PMCLOG_1(PMCLOG_ERROR, "Failed - The function %s is unsafe to call from event handler.\n", __FUNCTION__);
            return P_STATUS_LIB_GEN_ERROR;
        }
    }

    API_SEM_TAKE_RET_ERR(g_events_sem);

    /* check if device exist in the db */
    dev_entry = Pmc_event_db_dev_get(dev_handle);
    if (dev_entry == NULL)      // dev_handle not found, add the device to db
    {
        /* search db an find an unused entry */
        for (i = 0; i < MAX_NUM_DEVICES; i++)
        {
            if (!events_db[i].in_use)   /* first unused entry, use it */
            {
                dev_entry = &events_db[i];
                dev_entry->in_use = PMC_TRUE;
                dev_entry->dev_handle = dev_handle;
                dev_entry->first_event_index = EVENT_FROM_NOW;
                memset(&dev_entry->events, 0, sizeof(dev_entry->events));   /* reset handlers */
                if ((res = updateDevEntryEventIndex(i)) != P_STATUS_OK)
                {
                    API_SEM_GIVE_RET_ERR(g_events_sem);
                    PMCLOG_1(PMCLOG_ERROR, "Pmc_events_get failed, error 0x%04x.\n", res);
                    return (res);
                }
                break;
            }
        }
    }

    /* if db is full then dev_entry will be NULL */
    if (dev_entry == NULL)
    {
        API_SEM_GIVE_RET_ERR(g_events_sem);
        PMCLOG_1(PMCLOG_ERROR, "Events DB is full, cannot add more than %d devices.\n", MAX_NUM_DEVICES);
        return (P_STATUS_LIB_FULL);
    }

    /* check if same event type and hook alreay exist for this device */
    event_entry = Pmc_event_db_event_get(dev_entry, event_type, event_handler);
    if (event_entry != NULL)    /* same event type and hook alreay exist */
    {
        API_SEM_GIVE_RET_ERR(g_events_sem);
        PMCLOG_2(PMCLOG_ERROR, "Event type %d with same event_handler alreay exist for dev %d.\n", event_type,
                 dev_handle.nvme_fd);
        return (P_STATUS_OK);   // return OK quietly
    }

    /* Check if event registered for any handler */
    fw_register = Pmc_event_is_registered(dev_entry, event_type);

    /* event hook not exist, lets add it */
    for (i = 0; i < MAX_EVENTS_PER_DEV; i++)
    {
        if (dev_entry->events[i].event_handler == NULL) /* first empty place */
        {
            dev_entry->events[i].event_type = event_type;
            dev_entry->events[i].event_handler = event_handler;
            (*event_context_id) = (uint16_t) i; /* the index will be the event_context_id */

            API_SEM_GIVE_RET_ERR(g_events_sem);

            /* If event was not registered at all send register message to FW */
            if (fw_register == PMC_FALSE)
            {
                return Pmc_event_fw_register(dev_handle, event_type, PMC_TRUE);
            }

            return (P_STATUS_OK);
        }
    }

    /* we arrive here only if dev_entry->events[] is full */
    PMCLOG_2(PMCLOG_ERROR, "Events handlers list for dev %d is full, cannot add more than %d event handlers.\n",
             dev_handle.nvme_fd, MAX_EVENTS_PER_DEV);

    API_SEM_GIVE_RET_ERR(g_events_sem);
    return (P_STATUS_LIB_FULL);
}


/* This func unregister event from events database */
int Pmc_event_handler_unregister(const dev_handle_t dev_handle, const uint16_t event_context_id)
{
    Pmc_dev_entry_s *dev_entry;
    p_bool_t fw_register = PMC_FALSE;
    NVRAM_event_type_e event_type;

    if (!is_event_pkg_init)
    {
        PMCLOG_0(PMCLOG_ERROR, "Failed to unregister event handler, no event handler was registered.\n");
        return (P_STATUS_LIB_NOT_EXIST);
    }

    /* 
     * return error if this function is called from the event handler
     * NOTE, we use pthread anly when g_thread_mode is AUTO 
     */
    if (g_thread_mode == NVRAM_OPERATION_MODE_AUTO) 
    {
        if (g_event_thread_id == OSSRV_thread_self())
        {
            PMCLOG_1(PMCLOG_ERROR, "Failed - the function %s is unsafe to call from event handler.\n", __FUNCTION__);
            return P_STATUS_LIB_GEN_ERROR;
        }
    }


    API_SEM_TAKE_RET_ERR(g_events_sem);

    /* get device entry */
    dev_entry = Pmc_event_db_dev_get(dev_handle);
    if (dev_entry == NULL)      /* dev_handle not found */
    {
        API_SEM_GIVE_RET_ERR(g_events_sem);
        PMCLOG_1(PMCLOG_ERROR, "Failed to unregister event handler, dev_handle %d is not exist in events DB.\n",
                 dev_handle.nvme_fd);
        return (P_STATUS_LIB_NOT_EXIST);
    }


    /* check event_context_id */
    if (event_context_id >= MAX_EVENTS_PER_DEV) /* invalid event_context_id */
    {
        API_SEM_GIVE_RET_ERR(g_events_sem);
        PMCLOG_2(PMCLOG_ERROR, "Failed to unregister event handler, event_context_id %u is out of bounds (0..%u).\n",
                 event_context_id, MAX_EVENTS_PER_DEV);
        return (P_STATUS_LIB_NOT_EXIST);
    }

    /* Get event type */
    event_type = dev_entry->events[event_context_id].event_type;

    /* mark event entry as free */
    dev_entry->events[event_context_id].event_handler = NULL;

    /* if there is no handlers for this dev mark it as free */
    if (Pmc_event_db_entry_is_empty(dev_entry))
    {
        dev_entry->in_use = PMC_FALSE;
    }

    /* Check if event still registered for any handler */
    fw_register = Pmc_event_is_registered(dev_entry, event_type);

    API_SEM_GIVE_RET_ERR(g_events_sem);

    /* If event not registered at all send unregister message to FW */
    if (fw_register == PMC_FALSE)
    {
        return Pmc_event_fw_register(dev_handle, event_type, PMC_FALSE);
    }

    return (P_STATUS_OK);
}

/* This API polls for events from all devices in the system */
int Pmc_event_poll(void)
{
    int res;
    int i;
    uint32_t time_of_day = 0;
    Pmc_events_prp_s events_prp;


    /* Pmc_event_poll function is the only one which can be called while semaphore was not initialize, so let check it */
    if (is_event_pkg_init) 
    {
        API_SEM_TAKE_RET_ERR(g_events_sem);
    }

    /* iterate all devices */
    for (i = 0; i < MAX_NUM_DEVICES; i++)
    {
        if (events_db[i].in_use)    /* if device exist */
        {
            memset(&events_prp, 0, sizeof(events_prp));

            /* poll the latest events list from the controller */
            res = Pmc_events_get(events_db[i].dev_handle, events_db[i].first_event_index, time_of_day, &events_prp);
            if (res != 0)
            {
                if (res != P_STATUS_NVME_BUSY)  /* for busy do not print error */
                {
                    PMCLOG_1(PMCLOG_ERROR, "Pmc_events_get failed, error 0x%04x.\n", res);
                }
                continue;
            }

            /* update the index of the first event to poll at the next polling */
            events_db[i].first_event_index = events_prp.last_event_index;

            /* fire event to its registered hooks */
            Pmc_event_fire(&events_db[i], &events_prp);
        }
    }

    if (is_event_pkg_init) 
    {
        API_SEM_GIVE_RET_ERR(g_events_sem);
    }

    return (P_STATUS_OK);
}

/* This func is the events polling thread */
static void *Pmc_events_thread_function(void *arg)
{
    /* this thread-call is not wrapped with NVRAM_OPERATION_MODE_AUTO since this func is called only when g_thread_mode == NVRAM_OPERATION_MODE_AUTO */
    g_event_thread_id = OSSRV_thread_self();

    while (g_thread_alive)
    {
        /* Poll for events */
        Pmc_event_poll();

        /* wait before next polling */
        OSSRV_wait(g_events_polling_interval);
    }

    /* reset to 0 if thread is terminated */
    g_event_thread_id = 0;

    // thread finished due terminate so clear the DB
    memset(events_db, 0, sizeof(events_db));

    return (NULL);
}

/* init pkg only once !!! */
int Pmc_pre_init_events_pkg(void)
{
    API_SEM_INIT_RET_ERR(g_events_sem, 1);

    API_SEM_TAKE_RET_ERR(g_events_sem);

    memset(events_db, 0, sizeof(events_db));

    API_SEM_GIVE_RET_ERR(g_events_sem);

    return (P_STATUS_OK);
}

/* This func initialize events package */
int Pmc_init_events_pkg(void)
{
    API_SEM_TAKE_RET_ERR(g_events_sem);

    /* We create thread only for AUTO mode, for USER mode the user should do it */
    if (g_thread_mode == NVRAM_OPERATION_MODE_AUTO) 
    {
        /* create thread */
        g_thread_alive = PMC_TRUE;

        g_events_thread = OSSRV_thread_create(Pmc_events_thread_function);
        if (g_events_thread == 0)
        {
            API_SEM_GIVE_RET_ERR(g_events_sem);
            PMCLOG_2(PMCLOG_ERROR, "OSSRV_thread_create failed, errno %d (%s).\n", errno, strerror(errno));
            return (P_STATUS_HOST_OS_ERROR);
        }
    }

    is_event_pkg_init = PMC_TRUE;

    API_SEM_GIVE_RET_ERR(g_events_sem);

    return (P_STATUS_OK);
}

/* This func initialize events package */
int Pmc_terminate_events_pkg(void)
{
    // API_SEM_TAKE_RET_ERR(g_events_sem);

    g_thread_alive = PMC_FALSE;     // kill the event polling thread
    is_event_pkg_init = PMC_FALSE;

    // API_SEM_GIVE_RET_ERR(g_events_sem);

    API_SEM_DESTROY_RET_ERR(g_events_sem);

    return (P_STATUS_OK);
}


/* This func delete device entry from events database */
int Pmc_event_db_dev_del(const dev_handle_t dev_handle)
{
    Pmc_dev_entry_s *dev_entry;

    // API_SEM_TAKE_RET_ERR(g_events_sem);

    dev_entry = Pmc_event_db_dev_get(dev_handle);
    if (dev_entry != NULL)
    {
        dev_entry->in_use = PMC_FALSE;
    }


    // API_SEM_GIVE_RET_ERR(g_events_sem);

    return (P_STATUS_OK);
}


/* This func indicate whether events package already initialized */
p_bool_t Pmc_is_events_pkg_init(void)
{
    return (is_event_pkg_init);
}


/* This func set the events polling interval */
void Pmc_polling_interval_set(uint32_t polling_interval)
{
    g_events_polling_interval = polling_interval;
}


/* This func get the events polling interval */
uint32_t Pmc_polling_interval_get(void)
{
    return (g_events_polling_interval);
}
