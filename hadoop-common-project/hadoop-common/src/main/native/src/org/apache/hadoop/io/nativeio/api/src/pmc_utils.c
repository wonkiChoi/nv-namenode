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
* Description                  : PMC NVRAM Utilities
*
* $Revision: 1.1 $
* $Date: 2013/09/23 07:07:42 $
* $Author: shivatzi $
* Release $Name:  $
*
****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <sys/ioctl.h>
#ifdef OS_FREE_BSD
#include <dev/nvme/nvme.h>
#endif

#include "pmc_utils.h"
#include "pmc_log.h"
#include "pmc_ossrv.h"
#include "pmc_nvram_api_expo.h"

static p_bool_t is_pmc_driver = PMC_FALSE;

#define MILLIS						1000

/* Max ioctl retries when FW is busy */
#define MAX_IOCTL_BUSY_RETRIES		3

#define MAX_IOCTL_BUSY_DELAY		10

/* This func sets the the pmc driver flag */
void Set_is_pmc_driver(p_bool_t driver)
{
	is_pmc_driver = driver;
}

/* This func returns the the pmc driver flag */
p_bool_t Get_is_pmc_driver(void)
{
	return is_pmc_driver;
}

/* This func trim the right side of the str (remove spaces) */
void Pmc_strtrim(char *str)
{
    int i;

    for (i = strlen(str) - 1; i >= 0; i--)
    {
        if (str[i] == ' ' || str[i] == '\t' || !isprint(str[i]))
            str[i] = '\0';
    }
}


/* retrive token #tok_idc from a string */
void Pmc_str_tok(char *str, char *toks[], const int toks_size, int *count)
{
    int i = 0;
    char *p;

    (*count) = 0;

    p = strtok(str, " \t");
    while (p != NULL)
    {
        if (i < toks_size)
        {
            toks[i++] = p;
            (*count)++;
        }

        p = strtok(NULL, " \t");
    }
}

/* This func convert str to lowercase */
void Pmc_str_to_lower(char *str)
{
    int i;

    for (i = 0; str[i]; i++)
    {
        str[i] = tolower(str[i]);
    }
}


/* convert in_data from binary data to ascii */
uint32_t Pmc_to_ascii(const char *in_data, const uint8_t in_data_start, const uint8_t in_data_len, char *out_buff,
                      const uint8_t out_buff_size)
{
#define IN_DATA_SIZE    128

    int i;
    unsigned char in_data_saved[IN_DATA_SIZE];


    /* save in_buffer since the user can use same buffer as in and out buffer */
    if (in_data_len > IN_DATA_SIZE)
    {
        PMCLOG_1(PMCLOG_ERROR, "Error, in_data_len cannot be bigger than %d.\n", IN_DATA_SIZE);
        return (P_STATUS_LIB_NO_MEMORY);
    }

    /* check that the result string have enough place in the out buffer, i.e. 2 chars for each byte + place for '\0' */
    if ((in_data_len * 2 + 1) > out_buff_size)
    {
        PMCLOG_2(PMCLOG_ERROR, "out_buff_size %u is too small, must be at least %u.\n", out_buff_size,
                 (in_data_len * 2 + 1));
        return (P_STATUS_LIB_TOO_BIG);
    }

    /* save the original buff data since we are going to overwrite it */
    memcpy(in_data_saved, &in_data[in_data_start], in_data_len);

    /* convert to ascii and reverse the order (LSB bytes in the right side of the ascii string) */
    for (i = 0; i < in_data_len; i++)
    {
        sprintf(&out_buff[i * 2], "%02X", in_data_saved[in_data_len - i - 1]);
    }

    return (P_STATUS_OK);

#undef IN_DATA_SIZE
}


/* copy u8 to prp, convert to little endian */
void local_to_prp8(char *prp, const uint8_t val, const int offset)
{
    prp[offset] = val;
}

/* copy u16 to prp, convert to little endian */
void local_to_prp16(char *prp, const uint16_t val, const int offset)
{
    prp[offset + 0] = (val >> 0) & 0x00FF;
    prp[offset + 1] = (val >> 8) & 0x00FF;
}

/* copy u32 to prp, convert to little endian */
void local_to_prp32(char *prp, const uint32_t val, const int offset)
{
    prp[offset + 0] = (val >> 0) & 0x00FF;
    prp[offset + 1] = (val >> 8) & 0x00FF;
    prp[offset + 2] = (val >> 16) & 0x00FF;
    prp[offset + 3] = (val >> 24) & 0x00FF;
}

/* copy u64 to prp, convert to little endian */
void local_to_prp64(char *prp, const uint64_t val, const int offset)
{
    prp[offset + 0] = (val >> 0) & 0x00FF;
    prp[offset + 1] = (val >> 8) & 0x00FF;
    prp[offset + 2] = (val >> 16) & 0x00FF;
    prp[offset + 3] = (val >> 24) & 0x00FF;
    prp[offset + 4] = (val >> 32) & 0x00FF;
    prp[offset + 5] = (val >> 40) & 0x00FF;
    prp[offset + 6] = (val >> 48) & 0x00FF;
    prp[offset + 7] = (val >> 56) & 0x00FF;
}

/* copy buffer to prp */
void local_to_prp(char *prp, const char *buff, const int offset, const int len)
{
    memcpy(&prp[offset], buff, len);
}


/* copy from prp to u8, convert from little endian to cpu endian */
void prp_to_local8(uint8_t * val, const char *prp, const int offset)
{
    (*val) = prp[offset];
}

/* copy from prp to u16, convert from little endian to cpu endian */
void prp_to_local16(uint16_t * val, const char *prp, const int offset)
{
    (*val) = (prp[offset + 1] << 8) + (prp[offset + 0] << 0);
}

/* copy from prp to u32, convert from little endian to cpu endian */
void prp_to_local32(uint32_t * val, const char *prp, const int offset)
{
    int i;
    uint32_t tmp;

    (*val) = 0;

    for (i = 0; i < sizeof(uint32_t); i++)
    {
        tmp = (prp[offset + i] & 0xff);

        (*val) |= (tmp << (i * 8));
    }
}

/* copy from prp to u64, convert from little endian to cpu endian */
void prp_to_local64(uint64_t * val, const char *prp, const int offset)
{
    int i;
    uint64_t tmp;

    (*val) = 0;

    for (i = 0; i < sizeof(uint64_t); i++)
    {
        tmp = (prp[offset + i] & 0xff);

        (*val) |= (tmp << (i * 8));
    }
}

/* copy from prp to buffer */
void prp_to_local(char *buff, const char *prp, const int offset, const int len)
{
    memcpy(buff, &prp[offset], len);
}


/* Init random only once */
void Pmc_srand_once(void)
{
    static p_bool_t first_time = PMC_TRUE;

    if (first_time)
    {
        first_time = PMC_FALSE;
        srand(time(NULL));
    }
}

const char *pmc_err_to_str(P_STATUS err)
{
    switch (err)
    {
    case P_STATUS_NVME_AUTH_FAILED:
        return "AUTHENTICATION ERROR";

    case P_STATUS_NVME_BUSY:
        return "BUSY ERROR, TRY LATER";

    case P_STATUS_NVME_SUPER_CAP_NOT_EXIST:
        return "SUPER CAP NOT EXIST ERROR";

    case P_STATUS_NVME_FIRMWARE_REQUIRES_RESET:
        return "FIRMWARE REQUIRES RESET";

    case P_STATUS_NVME_FLASH_OPERATED:
        return "FLASH IS UNDER OPERATION, TRY LATER";

    case P_STATUS_NVME_BAD_PARAM:
        return "THE PARAMETER IS WRONG";

    case P_STATUS_NVME_UNSUPPORTED:
        return "OPERATION IS UNSUPPORTED";

    case P_STATUS_NVME_NOT_PERMITTED:
        return "OPERATION IS NOT PERMITTED";

    default:
        return "";
    }
}


/* This func sends the ioctl request using the pmc nvme driver */
#ifdef OS_FREE_BSD
uint32_t pmc_ioctl_send(dev_handle_t dev_handle, int command, nvme_arg_s *arg)
{
	/*This function should not be called in FreeBSD*/
	PMCLOG_0(PMCLOG_ERROR, "PMC driver not supported in FreeBSD");
	return (P_STATUS_HOST_OS_ERROR);
}
#else
uint32_t pmc_ioctl_send(dev_handle_t dev_handle, int command, nvme_arg_s * arg)
{
    int res;
    uint32_t err;

    struct usr_io uio;          /* use PMC NVMe structure */

    /* reset the PMC NVMe structure */
    bzero(&uio, sizeof(struct usr_io));

    /* fill PMC NVMe structure fields */
    memcpy(&uio.cmd, &arg->nvme_cmd, sizeof(struct nvme_cmd));  /* Submission queue entry. */
    uio.namespace = arg->nvme_cmd.header.namespaceID;   /* Namespace ID, -1 for non-specific */
    uio.addr = arg->nvme_cmd.header.prp1;   /* Data address */
    uio.length = arg->data_length;  /* Data length in bytes */
    uio.timeout = IOCTL_TIMEOUT_SECS;   /* Timeout in seconds */


    /* send PMC NVMe structure to NVMe driver */
    res = ioctl(dev_handle.nvme_fd, command, &uio);
    if (res < 0)
    {
        PMCLOG_3(PMCLOG_ERROR, "PMC NVMe command (opCode 0x%X) ioctl failed, errno %d (%s).\n",
                 arg->nvme_cmd.header.opCode, errno, strerror(errno));
        //return (P_STATUS_HOST_OS_ERROR);
	return 12345;
    }


    /* check if NVMe driver return error */
    if (uio.status != 0)
    {
        err = uio.status;

        if (err == -ETIME)
        {
            err = P_STATUS_LIB_TIMEOUT;
            PMCLOG_2(PMCLOG_ERROR, "PMC NVMe command (opCode 0x%X) failed: uio.status 0x%08X (TIMEOUT).\n",
                     arg->nvme_cmd.header.opCode, uio.status);
        }
        else
        {
            PMCLOG_2(PMCLOG_ERROR, "PMC NVMe command (opCode 0x%X) failed: uio.status 0x%08X.\n",
                     arg->nvme_cmd.header.opCode, uio.status);
        }

        //return (err);           /* TBD - convert to P_STATUS value */
	return P_STATUS_TEMP;
    }


    /* check if FW return error */
    arg->cq_dword0 = uio.comp.param.cmdSpecific;    /* completion queue DW0 field (Commnad Specific field) */
    arg->sct = uio.comp.SCT;    /* completion queue SCT field */
    arg->sc = uio.comp.SC;      /* completion queue SC field */


    err = (arg->sct << 8) + arg->sc;    /* build return value according to SC and SCT fields */

    if (err != 0)
    {
        if ( (err == P_STATUS_NVME_BUSY) || (arg->do_not_print_err) )
        {
            // don't print error, need retries
        }
        else if (err == P_STATUS_NVME_FIRMWARE_REQUIRES_RESET)  // After FW activate the value 0x0107 is not an error
                                                                // but indication that FW should be reset */
        {
            if (arg->nvme_cmd.header.opCode == ADMIN_OPCODE_FW_ACTIVATE)    // Verify that the opcode is FW activate
            {
                err = P_STATUS_OK;  // set error as OK
            }
        }
        else
        {
            PMCLOG_4(PMCLOG_ERROR, "PMC NVMe command (opCode 0x%X) failed: SCT 0x%x  SC 0x%x - %s.\n",
                     arg->nvme_cmd.header.opCode, arg->sct, arg->sc, pmc_err_to_str(err));
        }

        return (err);           /* TBD - convert to P_STATUS value */
    }

    return res;
}
#endif

/* This func sends the ioctl request using the inbox nvme driver */
#ifdef OS_FREE_BSD
uint32_t inbox_ioctl_send(dev_handle_t dev_handle, int command, nvme_arg_s *arg)
{

	struct nvme_pt_command	pt;
	int			res;
	uint32_t		err;

	(void)command; /* all commands are administrative */


	/* Zero out the new data structure */
	memset(&pt, 0, sizeof(pt));

	/* Copy the raw NVMe command from the linux defined buffer to the native 
	 * FreeBSD buffer. */
	memcpy(&pt.cmd, &arg->nvme_cmd, sizeof(arg->nvme_cmd));

	/* 
	 * This is not exaclty how these fields were intended to be used, but 
	 * the linux driver appears to communicate this information here. 
	 */
	pt.buf = (void *)arg->nvme_cmd.header.prp1;
	pt.len = arg->data_length;

	/* 
	 * Bits 1:0 of the opcode appear to specify data transfer direction, 0x1 = 
	 * write, 0x2 = read, 0x0 none, 0x3 = undefined? See "Opcodes for Admin 
	 * Commands" in NVMe spec. This is inferred, as it is never explicitly 
	 * stated anywhere. This appears to work, but I'm not going to promise 
	 * anything.
	 */
	pt.is_read = (arg->nvme_cmd.header.opCode & 0x3) == 0x2 ? 1 : 0;

	/* Dispatch the request to the driver */
	res = ioctl(dev_handle.nvme_fd, NVME_PASSTHROUGH_CMD, &pt);

	if (res< 0)
	{
		PMCLOG_3(PMCLOG_ERROR, "PMC NVMe command (opCode 0x%X) ioctl failed, errno %d (%s).\n", arg->nvme_cmd.header.opCode, errno, strerror(errno));
		return (P_STATUS_HOST_OS_ERROR);
	}
	
	/* Extract the status and convert from native to emulated format */
	arg->sct = pt.cpl.status.sct;
	arg->sc = pt.cpl.status.sc;

	/* The rest of this is the original error handling code */
	err = (arg->sct << 8) + arg->sc;	/* build return value according to SC and SCT fields */
	if (err != 0)
	{
		if ((err == P_STATUS_NVME_BUSY) || (arg->do_not_print_err))
		{
			// don't print error, need retries
		}
		else if (err == P_STATUS_NVME_FIRMWARE_REQUIRES_RESET)  // After FW activate the value 0x0107 is not an error
			// but indication that FW should be reset */
		{
			if (arg->nvme_cmd.header.opCode == ADMIN_OPCODE_FW_ACTIVATE)    // Verify that the opcode is FW activate
			{
				err = P_STATUS_OK;  // set error as OK
			}
		}
		else
		{
			PMCLOG_4(PMCLOG_ERROR, "PMC NVMe command (opCode 0x%X) failed: SCT 0x%x  SC 0x%x - %s.\n",
					arg->nvme_cmd.header.opCode, arg->sct, arg->sc, pmc_err_to_str(err));
		}

		return (err);           /* TBD - convert to P_STATUS value */
	}

	return 0;
    	
}
#else/*Linux OS case*/
uint32_t inbox_ioctl_send(dev_handle_t dev_handle, int command, nvme_arg_s * arg)
{
    int res;
    uint32_t err;
    struct nvme_admin_cmd cmd;
    char* origPrpPtr = NULL;
    char* alignPrpPtr = NULL;
    
    bzero(&cmd, sizeof(struct nvme_admin_cmd));
    /*Only the first 64 bytes of cmd are being updated by memcpy*/
    memcpy(&cmd, &arg->nvme_cmd, sizeof(struct nvme_cmd));  /* Submission queue entry. */
    cmd.timeout_ms = IOCTL_TIMEOUT_SECS * 1000; /* Timeout in milliseconds */
    cmd.data_len = arg->data_length;

    /* if there is a PRP buffer */
    if (cmd.addr != 0 && cmd.data_len > 0)
    {
        /* get memory page size */
        long mem_page_size = OSSRV_getpagesize();
        if (mem_page_size <= 0)
        {
            // error is printed by OSSRV_getpagesize
            //return (P_STATUS_HOST_OS_ERROR);
	    return P_STATUS_TEMP4;
        }

        /* alocate a new page aligned PRP buffer */
        res = posix_memalign((void **)&alignPrpPtr, mem_page_size, PRP_SIZE);
        if (res != 0)
        {
            switch (res)
            {
            case EINVAL:
                PMCLOG_1(PMCLOG_ERROR, "PMC NVMe command (opCode 0x%X) posix_memalign failed: The value of the alignment parameter is not a power of two multiple of sizeof( void *).\n",
                         arg->nvme_cmd.header.opCode);
                break;

            case ENOMEM:
                PMCLOG_1(PMCLOG_ERROR, "PMC NVMe command (opCode 0x%X) posix_memalign failed: There is insufficient memory available with the requested alignment.\n",
                         arg->nvme_cmd.header.opCode);
                break;
        
            default:
                PMCLOG_1(PMCLOG_ERROR, "PMC NVMe command (opCode 0x%X) posix_memalign failed: Undefined error.\n",
                         arg->nvme_cmd.header.opCode);
                break;
            }

           // return (P_STATUS_HOST_OS_ERROR);
	      return P_STATUS_TEMP2;
        }

        /* Before ioctl: copy the original PRP data into the new page aligned PRP buffer and use it in the NVMe command */
        origPrpPtr = (char *)cmd.addr;
        cmd.addr = (uint64_t)alignPrpPtr;
        memcpy(alignPrpPtr, origPrpPtr, cmd.data_len);
    }

    /* Execute ioctl command */
    res = ioctl(dev_handle.nvme_fd, command, &cmd);
    if (res < 0)
    {
        PMCLOG_3(PMCLOG_ERROR, "PMC NVMe command (opCode 0x%X) ioctl failed, errno %d (%s).\n",
                 arg->nvme_cmd.header.opCode, errno, strerror(errno));

        if (alignPrpPtr != NULL)
        {
            free(alignPrpPtr);
        }
       // return (P_STATUS_HOST_OS_ERROR);
       return 12345;
    }

    /* if there is an PRP buffer */
    if (cmd.addr != 0 && cmd.data_len > 0)
    {
        /* After ioctl: copy the data from the align PRP buffer into original PRP buffer */
        memcpy(origPrpPtr, alignPrpPtr, cmd.data_len);
        cmd.addr = (uint64_t)origPrpPtr;
    }

    /* rextracting SCT and SC fields from res */
    arg->sct = res >> 8;
    arg->sc = res & 0xff;
    err = (arg->sct << 8) + arg->sc;    /* build return value according to SC and SCT fields */
    if (err != 0)
    {
        if ((err == P_STATUS_NVME_BUSY) || (arg->do_not_print_err))
        {
            // don't print error, need retries
        }
        else if (err == P_STATUS_NVME_FIRMWARE_REQUIRES_RESET)  // After FW activate the value 0x0107 is not an error
                                                                // but indication that FW should be reset */
        {
            if (arg->nvme_cmd.header.opCode == ADMIN_OPCODE_FW_ACTIVATE)    // Verify that the opcode is FW activate
            {
                err = P_STATUS_OK;  // set error as OK
            }
        }
        else
        {
            PMCLOG_4(PMCLOG_ERROR, "PMC NVMe command (opCode 0x%X) failed: SCT 0x%x  SC 0x%x - %s.\n",
                     arg->nvme_cmd.header.opCode, arg->sct, arg->sc, pmc_err_to_str(err));
        }

        if (alignPrpPtr != NULL)
        {
            free(alignPrpPtr);
        }
        return (err);           /* TBD - convert to P_STATUS value */
    }

    if (alignPrpPtr != NULL)
    {
        free(alignPrpPtr);
    }

    return res;
}
#endif

/* This func wrap ioctl_send func and do retries when BUSY */
uint32_t pmc_ioctl_ext(dev_handle_t dev_handle, int command, nvme_arg_s * arg, p_bool_t displayBusyError)
{
    uint32_t err;
    uint8_t retries = MAX_IOCTL_BUSY_RETRIES;
    /* ioctl semaphore */
    static OSSRV_counting_semaphore_t ioctl_sem;
    static p_bool_t init_ioctl_sem_first_time = PMC_TRUE;

    /* Init ioctl sem only once */
    if (init_ioctl_sem_first_time)
    {
        API_SEM_INIT_RET_ERR(ioctl_sem, 1);
        init_ioctl_sem_first_time = PMC_FALSE;
    }

    /* Init random only once */
    Pmc_srand_once();

    while (retries > 0)
    {
        /* 
         * Serialize ioctl sending e.g. in order to avoid sending other ioctls
         * while the NVME interface is rebooting
         */
        API_SEM_TAKE_RET_ERR(ioctl_sem);
        if (Get_is_pmc_driver())
        	err = pmc_ioctl_send(dev_handle, command, arg);
        else
        	err = inbox_ioctl_send(dev_handle, command, arg);
        API_SEM_GIVE_RET_ERR(ioctl_sem);

        if (err != P_STATUS_NVME_BUSY)
        {
            break;              // for any err break, for busy do retries
        }

        OSSRV_wait((rand() % MAX_IOCTL_BUSY_DELAY) + 1);
        retries--;
    }

    if ((err == P_STATUS_NVME_BUSY) && (displayBusyError))
    {
        PMCLOG_4(PMCLOG_ERROR, "PMC NVMe command (opCode 0x%X) failed: SCT 0x%x  SC 0x%x - %s.\n",
                 arg->nvme_cmd.header.opCode, arg->sct, arg->sc, pmc_err_to_str(err));
    }

    return (err);
}

/* This func wrap pmc_ioctl_ext func */
uint32_t pmc_ioctl(dev_handle_t dev_handle, int command, nvme_arg_s * arg)
{
    nvme_arg_s new_arg;

    if (arg != NULL)
    {
        return pmc_ioctl_ext(dev_handle, command, arg, PMC_TRUE /* displayBusyError */ );
    }
    else
    {
        memset(&new_arg, 0, sizeof(new_arg));
        return pmc_ioctl_ext(dev_handle, command, &new_arg, PMC_TRUE /* displayBusyError */ );
    }
}


/*==========================================================================================================*/
/* Name: is_buffer_full */
/* Description: This function checks if the log buffer is full */
/*==========================================================================================================*/
uint32_t is_buffer_full(char *buf)
{
    char *line_ptr;

    /* 
     * point to the last entry
     */
    line_ptr = buf + (PAGES_PER_CORE * NVRAM_LOG_STRINGS_PER_PAGE - 1) * NVRAM_LOG_STRING_SIZE;

    if (line_ptr[1] == 0)
        return 0;
    else
        return 1;
}

/*==========================================================================================================*/
/* Name: find_max_entry */
/* Description: This function finds the index of the max entry */
/*==========================================================================================================*/
uint32_t find_max_entry(char *buf)
{
    uint32_t i, n_line;
    uint32_t total_strings = PAGES_PER_CORE * NVRAM_LOG_STRINGS_PER_PAGE;
    uint32_t max = 0, max_offset = 0;

    max = get_line_from_offset(buf, 0);
    max_offset = 0;

    for (i = 1; i < total_strings; i++)
    {
        n_line = get_line_from_offset(buf, i);

        if (max < n_line)
        {
            max = n_line;
            max_offset = i;
        }
    }

    return max_offset;
}

/*==========================================================================================================*/
/* Name: get_line_from_offset */
/* Description: This function gets the line number from current offset.  */
/*==========================================================================================================*/
uint32_t get_line_from_offset(char *buf, uint32_t offset)
{
    char *line_ptr;
    uint32_t n_line;

    line_ptr = buf + offset * NVRAM_LOG_STRING_SIZE;

    if (line_ptr[1] == 0)
        n_line = 0;
    else
        n_line = ((line_ptr[1] - '0') * 100 + (line_ptr[2] - '0') * 10 + (line_ptr[3] - '0'));

    return n_line;
}

/*==========================================================================================================*/
/* Name: find_first_entry */
/* Description: This function finds the line of the oldest entry.  */
/*==========================================================================================================*/
uint32_t find_first_entry(char *buf)
{
    uint32_t i, n_line;
    uint32_t total_strings = PAGES_PER_CORE * NVRAM_LOG_STRINGS_PER_PAGE;
    uint32_t prev_line = 0, min = 0, min_offset = 0;

    /* 
     * if buffer full,
     * the oldest entry is the first
     */
    if (!is_buffer_full(buf))
    {
        return 0;
    }

    prev_line = get_line_from_offset(buf, 0);
    min = prev_line;
    min_offset = 0;

    /* 
     * rest is for a full buffer,
     * search if there is a jump (entry[x+1] > entry[x] + 1),
     * need to check afterwards for 0 and last entry if wrap is there,
     * if there is a wrap, it means that the entry[x+1] is the oldest,
     * meanwhile also search for the minimum, 
     * because if there isnt a wrap still (wa = 999 plus 1),
     * the oldest is the minimum.   
     */
    for (i = 1; i < total_strings; i++)
    {
        n_line = get_line_from_offset(buf, i);

        /* 
         * record the minimum
         */
        if (min > n_line)
        {
            min = n_line;
            min_offset = i;
        }

        /* 
         * check if there is a jump
         */
        if (n_line > prev_line + 1)
        {
            return i;
        }

        prev_line = n_line;
    }

    /* 
     * check if jump is in the cyclic itself
     */
    n_line = get_line_from_offset(buf, total_strings - 1);
    prev_line = get_line_from_offset(buf, 0);

    if (prev_line > n_line + 1)
    {
        return 0;
    }

    /* 
     * no jump found, there wasnt WA yet,
     * print the smallest 
     */

    return min_offset;
}

/*==========================================================================================================*/
/* Name: find_first_entry_to_copy */
/* Description: This function finds the line we need to start fill the return buffer from.  */
/*==========================================================================================================*/
uint32_t find_first_entry_to_copy(char *buf, uint32_t n_strings)
{
    uint32_t max;
    uint32_t start_offset;

    if (n_strings == 0)
        return 0;

    start_offset = find_first_entry(buf);

    /* 
     * if the whole page log needs to be print,
     * the oldest is already the first one
     * else we need to calculate the first one
     */
    if (n_strings != NVRAM_LOG_STRINGS_PER_PAGE * PAGES_PER_CORE)
    {
        /* 
         * if the buffer is full,
         * we have the offset of the oldest, the previous of that is the newest
         * need to go back cyclic according n_strings.
         */
        if (is_buffer_full(buf))
        {
            if (start_offset >= n_strings)
                start_offset -= n_strings;
            else
                start_offset = NVRAM_LOG_STRINGS_PER_PAGE * PAGES_PER_CORE - (n_strings - start_offset);
        }
        else
        {
            /* 
             * if the buffer isnt full
             * need to find the max offset, and to go back from that one 
             * either n_strings, or if there arent enough lines still
             * to start from 0
             */
            max = find_max_entry(buf);
            if (max >= n_strings)
                start_offset = max + 1 - n_strings;
            else
                start_offset = 0;
        }
    }

    return start_offset;
}

/*==========================================================================================================*/
/* Name: getBit                                                                                             */
/* Description: Get the value of a specific bit in a uint32_tvariable                                       */
/*==========================================================================================================*/
uint32_t getBit(uint32_t value, uint8_t bitIdx)
{
    return((value >> bitIdx) & 1);
}

/*==========================================================================================================*/
/* Name: setBit                                                                                             */
/* Description: Set a specific bit in a uint32_t variable to 1                                              */
/*==========================================================================================================*/
uint32_t setBit(uint32_t value, uint8_t bitIdx)
{
    value |= (1 << bitIdx);
    return(value);
}

/*==========================================================================================================*/
/* Name: clearBit                                                                                           */
/* Description: Set a specific bit in a uint32_t variable to 0                                              */
/*==========================================================================================================*/
uint32_t clearBit(uint32_t value, uint8_t bitIdx)
{
    value &= ~(1 << bitIdx);
    return(value);
}


