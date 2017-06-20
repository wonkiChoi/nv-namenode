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
* Description                  : PMC NVRAM Utilities header
*
* $Revision: 1.1 $
* $Date: 2013/09/23 07:04:23 $
* $Author: shivatzi $
* Release $Name:  $
*
****************************************************************************/

#ifndef __PMC_NVRAM_UTILS_H__
#define __PMC_NVRAM_UTILS_H__


#include "pmc_defines.h"
#include "pmc_nvme.h"
#include "pmc_nvram_api_expo.h"


/**********************************************************/
/* External variables */
/**********************************************************/
extern NVRAM_operation_mode_e g_thread_mode;
extern NVRAM_operation_mode_e g_semaphore_mode;

/**********************************************************/
/* Macros */
/**********************************************************/

/* initialize semaphore */
#define API_SEM_INIT(_sem_, _sem_val_, _res_) \
{\
    _res_ = 0;\
    if (g_semaphore_mode == NVRAM_OPERATION_MODE_AUTO)\
    {\
        _res_ = OSSRV_counting_semaphore_init(&_sem_, (_sem_val_));\
    }\
	if (_res_ != 0)\
	{\
		PMCLOG_3(PMCLOG_ERROR, "API_SEM_INIT failed to initialize %s semaphore, errno %d (%s).\n", #_sem_, errno, strerror(errno));\
		_res_ = P_STATUS_HOST_OS_ERROR;\
		return;\
	}\
}

/* initialize semaphore */
#define API_SEM_INIT_RET_ERR(_sem_, _sem_val_) \
{\
    int res = 0;\
    if (g_semaphore_mode == NVRAM_OPERATION_MODE_AUTO)\
    {\
        res = OSSRV_counting_semaphore_init(&_sem_, (_sem_val_));\
    }\
	if (res != 0)\
	{\
		PMCLOG_3(PMCLOG_ERROR, "API_SEM_INIT failed to initialize %s semaphore, errno %d (%s).\n", #_sem_, errno, strerror(errno));\
		return (P_STATUS_HOST_OS_ERROR);\
	}\
}

/* destroy semaphore */
#define API_SEM_DESTROY_RET_ERR(_sem_)\
{\
    int res = 0;\
    if (g_semaphore_mode == NVRAM_OPERATION_MODE_AUTO)\
    {\
	    res = OSSRV_counting_semaphore_close(&_sem_);\
    }\
	if (res != 0)\
	{\
		PMCLOG_3(PMCLOG_ERROR, "API_SEM_DESTROY failed to destroy %s semaphore,  errno %d (%s).\n", #_sem_, errno, strerror(errno));\
		return (P_STATUS_HOST_OS_ERROR);\
	}\
}


/* take semaphore */
#define API_SEM_TAKE_RET_ERR(_sem_)\
{\
    int res = 0;\
    if (g_semaphore_mode == NVRAM_OPERATION_MODE_AUTO)\
    {\
	    res = OSSRV_counting_semaphore_wait(&_sem_);\
    }\
	if (res != 0)\
	{\
		PMCLOG_3(PMCLOG_ERROR, "API_SEM_TAKE failed to wait on %s semaphore, errno %d (%s).\n", #_sem_, errno, strerror(errno));\
		return (P_STATUS_HOST_OS_ERROR);\
	}\
}


/* take semaphore */
#define API_SEM_TAKE_RET_NULL(_sem_)\
{\
    int res = 0;\
    if (g_semaphore_mode == NVRAM_OPERATION_MODE_AUTO)\
    {\
	    res = OSSRV_counting_semaphore_wait(&_sem_);\
    }\
	if (res != 0)\
	{\
		PMCLOG_3(PMCLOG_ERROR, "API_SEM_TAKE failed to wait on %s semaphore, errno %d (%s).\n", #_sem_, errno, strerror(errno));\
		return (NULL);\
	}\
}

/* give semaphore */
#define API_SEM_GIVE_RET_ERR(_sem_)\
{\
    int res = 0;\
    if (g_semaphore_mode == NVRAM_OPERATION_MODE_AUTO)\
    {\
	    res = OSSRV_counting_semaphore_post(&_sem_);\
    }\
	if (res != 0)\
	{\
		PMCLOG_3(PMCLOG_ERROR, "API_SEM_GIVE failed to post %s semaphore, errno %d (%s).\n", #_sem_, errno, strerror(errno));\
		return (P_STATUS_HOST_OS_ERROR);\
	}\
}

/* give semaphore */
#define API_SEM_GIVE_RET_NULL(_sem_)\
{\
    int res = 0;\
    if (g_semaphore_mode == NVRAM_OPERATION_MODE_AUTO)\
    {\
	    res = OSSRV_counting_semaphore_post(&_sem_);\
    }\
	if (res != 0)\
	{\
		PMCLOG_3(PMCLOG_ERROR, "API_SEM_GIVE failed to post %s semaphore, errno %d (%s).\n", #_sem_, errno, strerror(errno));\
		return (NULL);\
	}\
}


/* return err if val not in bounds */
#define CHECK_BOUNDS(_val_, _min_val_, _max_val_)\
{\
	if ((_val_) < (_min_val_) || (_val_) > (_max_val_))\
	{\
		PMCLOG_4(PMCLOG_ERROR, "Error, %s=%d out of bounds (%d..%d).\n", #_val_, (_val_), (_min_val_), (_max_val_));\
		return (P_STATUS_LIB_BAD_PARAM);\
	}\
}

/* return err if pointer is NULL */
#define CHECK_PTR(_ptr_)\
{\
	if ((_ptr_) == NULL)\
	{\
		PMCLOG_1(PMCLOG_ERROR, "Error, pointer %s is NULL.\n", #_ptr_);\
		return (P_STATUS_LIB_BAD_PARAM);\
	}\
}



/**********************************************************/
/* Functions */
/**********************************************************/

/* This func trim the right side of the str (remove spaces) */
void Pmc_strtrim(char *str);

/* retrive token #tok_idc from a string */
void Pmc_str_tok(char *str, char *toks[], const int toks_size, int *count);

/* This func convert str to lowercase */
void Pmc_str_to_lower(char *str);

/* convert in_data from binary data to ascii */
uint32_t Pmc_to_ascii(const char *in_data, const uint8_t in_data_start, const uint8_t in_data_len, char *out_buff,
                      const uint8_t out_buff_size);

/* PRP functions */
/* copy u8 to prp, convert to little endian */
void local_to_prp8(char *prp, const uint8_t val, const int offset);

/* copy u16 to prp, convert to little endian */
void local_to_prp16(char *prp, const uint16_t val, const int offset);

/* copy u32 to prp, convert to little endian */
void local_to_prp32(char *prp, const uint32_t val, const int offset);

/* copy u64 to prp, convert to little endian */
void local_to_prp64(char *prp, const uint64_t val, const int offset);

/* copy buffer to prp */
void local_to_prp(char *prp, const char *buff, const int offset, const int len);

/* copy from prp to u8, convert from little endian to cpu endian */
void prp_to_local8(uint8_t * val, const char *prp, const int offset);

/* copy from prp to u16, convert from little endian to cpu endian */
void prp_to_local16(uint16_t * val, const char *prp, const int offset);

/* copy from prp to u32, convert from little endian to cpu endian */
void prp_to_local32(uint32_t * val, const char *prp, const int offset);

/* copy from prp to u64, convert from little endian to cpu endian */
void prp_to_local64(uint64_t * val, const char *prp, const int offset);

/* copy from prp to buffer */
void prp_to_local(char *buff, const char *prp, const int offset, const int len);

/* wrap system ioctl function */
uint32_t pmc_ioctl_ext(dev_handle_t dev_handle, int command, nvme_arg_s * arg, p_bool_t displayBusyError);

/* wrap pmc_ioctl_ext function */
uint32_t pmc_ioctl(dev_handle_t dev_handle, int command, nvme_arg_s * arg);

/* check if the log buffer is full (all entries occupied) */
uint32_t is_buffer_full(char *buf);

/* find the offset of the biggest entry */
uint32_t find_max_entry(char *buf);

/* find the oldest entry index */
uint32_t find_first_entry(char *buf);

/* find the entry that we need to start returning data from */
uint32_t find_first_entry_to_copy(char *buf, uint32_t n_strings);

/* check the number in a given entry */
uint32_t get_line_from_offset(char *buf, uint32_t offset);

/* This func sets the the pmc driver flag */
void Set_is_pmc_driver(p_bool_t driver);

/* This func returns the the pmc driver flag */
p_bool_t Get_is_pmc_driver(void);

/*
 * Methods to enable bit manipulation
 */
uint32_t getBit(uint32_t value, uint8_t bitIdx);
uint32_t setBit(uint32_t value, uint8_t bitIdx);
uint32_t clearBit(uint32_t value, uint8_t bitIdx);

// ////////////////////////////////////////////////////////////////////////////////////////////////////////////
#endif /* __PMC_NVRAM_UTILS_H__ */
