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
* Description        : Implementation for OS-dependent services for Linux OS
*
* $Revision: 1.1 $
* $Date: 2015/05/09 07:07:42 $
* $Author: shivatzi $
* Release $Name:  $
*
****************************************************************************/
//#ifdef LINUX
/*============================= Include Files ===============================*/
#include <errno.h> /* for -EBUSY */
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <stdarg.h>
#include <time.h>

#include "pmc_log.h"
#include "pmc_ossrv.h"


/*================================ Macros ===================================*/
/*=============================== Constants =================================*/
/*============================== Data Types =================================*/
typedef struct timespec timespec_t; /* timespec defined in time.h */
/*========================== External Variables =============================*/
extern int errno; 
/*=========================== Module Variables ==============================*/

/* pthread_once, used to open module only once */
static pthread_once_t g_once_control = PTHREAD_ONCE_INIT;

/*========================== External Functions =============================*/
/*==================== Internal Module Functions Prototype ==================*/
/*============================= Module Functions ============================*/
int OSSRV_wait(uint32_t  msec)
{
    timespec_t  req_time;

	if (msec > 999) /* more than 1 Sec */
	{

		req_time.tv_sec  = msec / 1000;
		req_time.tv_nsec = (msec * 1000 * 1000) - ((msec / 1000)*1000*1000*1000);
	} else
	{
		req_time.tv_sec  = 0; /* Zero seconds */
		req_time.tv_nsec = (msec * 1000 * 1000);
	}
	nanosleep (&req_time,NULL);

	/* usleep(msec * 1000); previous implementation which gave worse results */

	return (EXIT_OK);
}

OSSRV_thread_id_t OSSRV_thread_create_with_arg(void  *thread_function, void  *arg)
{
	int result = 0;
	OSSRV_thread_id_t  thread_id;  
	pthread_t      mythread;		

    result = pthread_create(&mythread, NULL, thread_function, arg);
	if (result != 0)
	{ /* Error creating thread */
		PMCLOG_1(PMCLOG_ERROR, "OSSRV Linux create_thread error - %d\n", result);
		thread_id = 0;
	} else
	{
		thread_id = (OSSRV_thread_id_t) mythread;
	}

	return (thread_id);
}


OSSRV_thread_id_t OSSRV_thread_create(void  *thread_function)
{
	return (OSSRV_thread_create_with_arg(thread_function, NULL));
}


void OSSRV_thread_terminate(void)
{
	void *return_param = NULL;
	pthread_exit(return_param);
}


OSSRV_thread_id_t OSSRV_thread_self(void)
{
	return (pthread_self());
}


int OSSRV_thread_once(OSSRV_once_func_t func)
{
    return (pthread_once(&g_once_control, func));
}


int OSSRV_counting_semaphore_init(OSSRV_counting_semaphore_t  *semaphore, 
								  const uint16_t     initial_value)
{
	int result=0;
	errno=0; 

	result = sem_init(semaphore, 0, initial_value);
	if (result == -1) 
	{
		PMCLOG_2(PMCLOG_ERROR, "OSSRV Linux counting_semaphore_init: Error, sem_init function failed: result %d, initial counting value %d\n", result, initial_value);
		return (EXIT_ERROR);
	}

	return (EXIT_OK);
}


int OSSRV_counting_semaphore_post(OSSRV_counting_semaphore_t  *semaphore)
{
	int result=0;

    errno=0; 
	result = sem_post(semaphore);
	if (result == -1) 
	{
		PMCLOG_1(PMCLOG_ERROR, "OSSRV Linux counting_semaphore_post: Error, sem_post function failed: result %d\n", result);
		return (EXIT_ERROR);
	}

	return (EXIT_OK);
}


int OSSRV_counting_semaphore_wait(OSSRV_counting_semaphore_t  *semaphore)
{
	errno=0; 
	sem_wait(semaphore); /* Always return 0 */

	return (EXIT_OK);

}


int OSSRV_counting_semaphore_trywait(OSSRV_counting_semaphore_t  *semaphore)
{
	int result=0;
	errno = 0;
	result = sem_trywait(semaphore);
	
	if (result == 0) 
		return (1);
	else
	{ 
		if (errno != EAGAIN) 
		{
			char *error_str; 
			error_str = strerror (errno);
			PMCLOG_3(PMCLOG_ERROR, "OSSRV Linux counting_semaphore_trywait: Error %d on trywait(result = %d): %s\n", errno,result,error_str);

			return (EXIT_ERROR);
		}
		else return (0);
	}

	return (EXIT_OK);
}


int OSSRV_counting_semaphore_wait_timeout(OSSRV_counting_semaphore_t  *semaphore,
                                          uint32_t                     milliseconds)
{
    struct timeval abs_timeout;
    struct timezone tz;
    timespec_t timeout;
    short int  result;


    /* Get current real time clock and add to it the waiting time */
    timeout.tv_sec  = 0;
    timeout.tv_nsec = 0;
    errno = 0; /* reset errno before error occurs */
    gettimeofday(&abs_timeout,&tz);

    /* [sem clock](sec) = [current clock](sec) + [user wait time, only sec] */
    timeout.tv_sec  = abs_timeout.tv_sec + (milliseconds/1000);
    /* [sem clock](nsec) = [current clock](nsec) + [user wait time, only milisec](nsec) */
    timeout.tv_nsec = (abs_timeout.tv_usec*1000) + ((milliseconds%1000)*1000000);

    /* [current clock](nsec) + [user wait time, less then sec](nsec) > 1sec(nsec)*/
    if ((timeout.tv_nsec) > 1000000000)
    {
        /* we have more then 1 sec in nano sec so inc the sec */
        timeout.tv_sec  += 1;
        timeout.tv_nsec -= 1000000000;
    }
    
	result = sem_timedwait (semaphore, &timeout);
    if (result == 0)
    {
        /* semaphore was signalled */
        return (EXIT_OK);
    }

    if (result > 0)
    {
        /* buggy glibc, copy the returned error code to errno, 
           where it should be */
        errno = result;
    }

    if (result < 0)
    {
        /* error code is in errno */
        if (errno == ETIMEDOUT) 
        {
            return (TIME_OUT);
        }
        
   		PMCLOG_1(PMCLOG_ERROR, "Error, sem_timedwait function failed: result %d\n", result);

		return (EXIT_ERROR);
    }

	return (EXIT_OK);

}


int OSSRV_counting_semaphore_close(OSSRV_counting_semaphore_t  *semaphore)
{
	int result=0;
	errno=0; 

	result = sem_destroy (semaphore);
	if (result != 0) 
	{
		char *error_str; 
		error_str = strerror (errno);
		PMCLOG_3(PMCLOG_ERROR, "OSSRV Linux counting_semaphore_close: Error %d on closing semaphore(result = %d): %s\n", errno,result,error_str);

		return (EXIT_ERROR);
	}

	return (EXIT_OK);
}


/* Old OSSRV EXT commands */
int OSSRV_writev(int filedes, const struct iovec *iov, int count)
{
	int nbytes;
	
	nbytes = writev(filedes, iov, count);
 
	if (nbytes < 0)
	{
		PMCLOG_1(PMCLOG_ERROR, "OSSRV EXT Linux error writev %d\n", nbytes);
	}
	return nbytes;
}


char *OSSRV_getcwd(char *buf, int len)
{
	char  *result;

	result = getcwd(buf, len);
	if (result < 0)
	{
		PMCLOG_0(PMCLOG_ERROR, "Failed to _etcwd\n");


	}
	return result;
}


long OSSRV_getpagesize(void)
{
    long mem_page_size = sysconf(_SC_PAGESIZE);
    if (mem_page_size <= 0)
    {
        PMCLOG_2(PMCLOG_ERROR, "OSSRV get memory page size: sysconf(_SC_PAGESIZE) failed, errno %d - %s\n", errno, strerror(errno));
    }

    return mem_page_size;
}


int OSSRV_snprintf(char *buf, size_t maxlen, const char *format, ...)
{
	int result;
	
	va_list ap;
	va_start (ap, format);
 	result = vsnprintf(buf, maxlen, format,ap); 
	va_end (ap);

    if (result > -1 && result < maxlen)
         return -1;

	return result;
}

char *OSSRV_strdup(const char *strSource)
{
	void *result;

	result = (char *)strdup(strSource);
	return result;
}


int OSSRV_gettimeofday(struct timeval * tv, struct timezone *tz)
{
 	return gettimeofday(tv, tz);
}


/*======================== Internal Module Functions ========================*/
/*=================================== End ===================================*/

//#endif /* LINUX */






