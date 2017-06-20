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
* Description                  : PMC NVRAM Events header
*
* $Revision: 1.1 $
* $Date: 2013/09/23 07:04:23 $
* $Author: shivatzi $
* Release $Name:  $
*
****************************************************************************/

#ifndef __PMC_NVRAM_EVENTS_H__
#define __PMC_NVRAM_EVENTS_H__



#include "pmc_defines.h"
#include "pmc_nvram_api_expo.h"

/**********************************************************/
/* Defines */
/**********************************************************/
#define MAX_EVENTS_PER_DEV				64


/**********************************************************/
/* Types */
/**********************************************************/
/* Event data as passed in the PRP */
typedef struct
{
    uint32_t type;              /* Event type */
    uint32_t subtype;           /* Subtype bitmask */
    NVRAM_event_data_u data;    /* Event data */

} NVRAM_event_data_s;


/* The get events PRP */
typedef struct
{
    uint64_t last_event_index;  /* Index of the last event */
    uint32_t event_count;       /* Count of the events in events list */
    NVRAM_event_data_s events_data[MAX_EVENTS_PER_DEV]; /* Events list */

} Pmc_events_prp_s;



/**********************************************************/
/* Functions */
/**********************************************************/

int Pmc_pre_init_events_pkg(void);

/* This func initialize events package */
int Pmc_init_events_pkg(void);

/* This func terminate events package */
int Pmc_terminate_events_pkg(void);

/* This func indicate whether events package already initialized */
p_bool_t Pmc_is_events_pkg_init(void);

/* This func delete device entry from events database */
int Pmc_event_db_dev_del(const dev_handle_t dev_handle);


/* This func set the events polling interval */
void Pmc_polling_interval_set(uint32_t polling_interval);


/* This func get the events polling interval */
uint32_t Pmc_polling_interval_get(void);


/* This func register event to events database */
int Pmc_event_handler_register(const dev_handle_t dev_handle,   /* Device handle */
                               const NVRAM_event_type_e event_type, /* Event type */
                               const PMC_NVRAM_gen_event_handler_t event_handler,   /* Event hook handler */
                               uint16_t * event_context_id);    /* Event context ID */


/* This func unregister event from events database */
int Pmc_event_handler_unregister(const dev_handle_t dev_handle, /* Device handle */
                                 const uint16_t event_context_id);  /* Event context ID */

/* This API polls for events from all Mt Ramon devices in the system and executes the relevant registered event handlers (hook functions). */
int Pmc_event_poll(void);

/* This func send event register to the NVRAM card */
P_STATUS Pmc_event_fw_register(const dev_handle_t dev_handle, const NVRAM_event_type_e event_type, p_bool_t reg);



// ////////////////////////////////////////////////////////////////////////////////////////////////////////////
#endif /* __PMC_NVRAM_EVENTS_H__ */
