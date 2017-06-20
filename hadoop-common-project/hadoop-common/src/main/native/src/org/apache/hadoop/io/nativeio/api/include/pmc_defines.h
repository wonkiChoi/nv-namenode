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
* Description                  : PMC Defines header
*
* $Revision: 1.1 $
* $Date: 2013/09/23 07:04:23 $
* $Author: shivatzi $
* Release $Name:  $
*
****************************************************************************/

#ifndef __PMC_DEFINES_H__
#define __PMC_DEFINES_H__

#ifdef PLATFORM_HOST            // Host
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#else // FW
// #include <stdtypes.h>
#endif



/* Boolean type */
typedef uint8_t p_bool_t;

/* time value type */
typedef uint32_t p_time_t;      /* Seconds from 1/1/1970 */


#ifdef PLATFORM_HOST

#define PMC_FALSE		0
#define PMC_TRUE		(!PMC_FALSE)

/* 
 * Holds all types of device info.
 * In future it will point to events DB
 *
 */
typedef struct device_info_s
{
    struct stat blk_dev_stat;   /* device file stats */
} device_info_t;


/* Handle which identifies the NVRAM device uniquely */
typedef struct
{
    int nvme_fd;                /* file descriptor of nvme driver */
    int nvme_block_fd;			/* file descriptor of nvme block driver */
    int mod_dev_id;             /* device id in pmc module db */
    device_info_t device_info;  /* this struct holds the all the device related information. */
} dev_handle_t;

#endif /* PLATFORM_HOST */

#endif /* __PMC_DEFINES_H__ */
