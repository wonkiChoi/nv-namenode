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
* Description         :  Definitions/Declarations for OS-dependent services header
*
* $Revision: 1.1 $
* $Date: 2015/05/09 07:04:23 $
* $Author: shivatzi $
* Release $Name:  $
*
****************************************************************************/

#ifndef _OSSRV_EXPO_H__
#define _OSSRV_EXPO_H__

/*============================= Include Files ===============================*/
/* Semaphore type definition 
**
** Include files needed for defining OS-specific functions and semaphore data types 
** Counting semaphore - a semaphore type which supports value increment and decrement 
*/
#if defined WIN32
    #ifndef _WIN32_WINNT 
	#define _WIN32_WINNT 0x0400 /* Will not work on old NT versions */
    #endif 
	#ifndef RPC_NO_WINDOWS_H
	#define RPC_NO_WINDOWS_H /* to avoid warning C4115 when warning level is 4 */ 
	#endif

	#include <windows.h>
	#include <time.h>
	typedef HANDLE           OSSRV_counting_semaphore_t; 
#else //LINUX
    #include <pthread.h>
	#include <semaphore.h> /* Counting semaphore definitions header */
	#include <sys/time.h>
    #include <sys/uio.h>
    #include <sys/ioctl.h>
	typedef sem_t			 OSSRV_counting_semaphore_t; 
//#else
//    #error Unsupported OS

/* Note: Other OSs semaphore type definition should be placed here */
#endif

#include <stdint.h>


#define EXIT_OK 0
#define EXIT_ERROR -1
#define TIME_OUT -2

/*================================ Macros ===================================*/
/*=============================== Constants =================================*/
#define MAX_PATH_LEN				256

/*============================== Data Types =================================*/
typedef OSSRV_counting_semaphore_t OSSRV_semaphore_t;
typedef void (*OSSRV_once_func_t)(void);


/* OSSRV_semaphore_init
**
** Initialize a semaphore / mutex or other thread hot sync mechanism.
** Only after a semaphore has been init, it can be used (signal and release).
**
** Input parameter:
**				semaphore	: Semaphore id
**
** Return codes:
**				EXIT_OK		: Success
**				EXIT_ERROR	: Init error
*/
#define OSSRV_semaphore_init(semaphore)    OSSRV_counting_semaphore_init(semaphore, 1)


/* OSSRV_semaphore_signal
** 
** 'Signal' a semaphore.
** After a semaphore has been 'signaled', it can be either released by the signaling thread 
** (multi signaling is unspecified) or signaled by other threads (waiting threads).
**
** Input parameter:
**				semaphore	: Semaphore id
**
** Return codes:
**				EXIT_OK		: Success
**				EXIT_ERROR	: Signal error
*/
#define OSSRV_semaphore_signal(semaphore)  OSSRV_counting_semaphore_wait(semaphore)


/* OSSRV_semaphore_release
**
** 'Release' a semaphore.
** After the semaphore has been 'released', any thread may signal it again.
**
** Input parameter:
**				semaphore	: Semaphore id
**
** Return codes:
**				EXIT_OK		: Success
**				EXIT_ERROR	: Release error (e.g. semaphore is not signaled)
*/
#define OSSRV_semaphore_release(semaphore) OSSRV_counting_semaphore_post(semaphore)


/* OSSRV_semaphore_listen
** 
** 'Listen' to a semaphore and signal it if possible.
** The function attempts once to signal a semaphore without blocking. 
** It returns a code indicating whether the signal attempt succeeded. After a semaphore has been 
** 'signaled', it can be either released by the signaling thread or signaled by other threads (waiting
** threads). Multi listening (same thread listening a semaphore again and again after it signaled the  
** semaphore) is unspecified.
**
** Input parameter:
**				semaphore	: Semaphore id
**
** Return codes:
**				0			: Listen failed (another thread owns the semaphore)
**				1			: Listen success (semaphore is signaled by current thread)
**				EXIT_ERROR	: Listen error (e.g. semaphore is not valid)
*/
#define OSSRV_semaphore_listen(semaphore)  OSSRV_counting_semaphore_trywait(semaphore)


/* OSSRV_semaphore_close
**
** Close a semaphore.
** After a semaphore has been closed, the only function applicable upon it is OSSRV_semaphore_init.
**
** Input parameter:
**				semaphore	: Semaphore id
**
** Return codes:
**				EXIT_OK		: Success
**				EXIT_ERROR	: Close error
*/
#define OSSRV_semaphore_close(semaphore)   OSSRV_counting_semaphore_close(semaphore)



/*========================== External Variables =============================*/
/*========================== External Functions =============================*/
/*======================= Module Functions Prototype ========================*/


/* OSSRV_wait
** 
** Wait (do nothing) for the parametered time and then return.
**
** Assumption: the function has a thread resolution (only the calling thread is on hold) 
** and it is thread-hot (several threads can call it simultaneously without affecting each other).
**
** Input parameter:
**				msec	   : Time in milliseconds, range: type range (0 - 65535 (~1 min))
**
** Return codes:
**				EXIT_OK    : Success
**				EXIT_ERROR : Error 
*/
int OSSRV_wait(uint32_t  msec);


/* Thread function */
#if defined WIN32
	typedef uintptr_t OSSRV_thread_id_t;
#else // LINUX
	typedef pthread_t OSSRV_thread_id_t;
//#else
//    #error Unsupported OS
/* Note: Other OSs semaphore type definition should be placed here */
#endif


typedef void (*OSSRV_thread_function_t)(void *); 

/* OSSRV_thread_create
**
** Create a new thread, and initializes it with the parametered function. 
** The thread starts to run and the function returns.
** 					
** Input parameter:
**				thread_function	: Pointer to a thread function,
**								  function prototype: OSSRV_thread_function_t
** Return codes:
**				0				: Error / Thread id not available
**				other			: New thread id
*/
OSSRV_thread_id_t OSSRV_thread_create(void  *thread_function);


/* OSSRV_thread_create_with_arg
**
** Create a new thread, and initializes it with the parametered function and one argument. 
** The thread starts to run and the function returns.
** 					
** Input parameter:
**				thread_function	: Pointer to a thread function,
**								  function prototype: OSSRV_thread_function_t
**				arg          	: argument which will be passed to the thread function
** Return codes:
**				0				: Error / Thread id not available
**				other			: New thread id
*/
OSSRV_thread_id_t OSSRV_thread_create_with_arg(void  *thread_function, void  *arg);

/* OSSRV_thread_terminate
**
** This function is used by a thread to terminate itself
**
** Parameters:
**
** Return codes:
**
*/
void OSSRV_thread_terminate(void);

/* OSSRV_thread_self
**
** Get thread ID
**
**
** Return codes:
**				Thread ID
**
*/
OSSRV_thread_id_t OSSRV_thread_self(void);


/* OSSRV_thread_once
**
** Call OSSRV_once_func_t once and only once 
**
** Input parameters:
**				func	: The function to be called only once
**
** Return codes:
**				EXIT_OK		: Success
**				EXIT_ERROR	: Error
*/
int OSSRV_thread_once(OSSRV_once_func_t func);


/* OSSRV_counting_semaphore_init
** 
** Init a counting semaphore.
** Only after a counting semaphore has been init, it can be used (post and wait).
**
** Input parameters:
**				semaphore     : Counting semaphore id
**				initial_value : Initial count associated with the counting semaphore, range: 0 - MAXINT
**
** Return codes:
**				EXIT_OK		  : Success
**				EXIT_ERROR	  : Init error
*/
int OSSRV_counting_semaphore_init(OSSRV_counting_semaphore_t  *semaphore, 
								  const uint16_t     initial_value);


/* OSSRV_counting_semaphore_post
** 
** Atomically increase the count of a counting semaphore.  
** This function never blocks the semaphore and can safely be used in asynchronous signal handlers.
**
** Input parameter:
**				semaphore	: Counting semaphore id
**
** Return codes:
**				EXIT_OK		: Success
**				EXIT_ERROR	: Post error
*/
int OSSRV_counting_semaphore_post(OSSRV_counting_semaphore_t  *semaphore);


/* OSSRV_counting_semaphore_wait
**
** Suspend the calling thread until the counting semaphore has a non-zero count,  
** then atomically decreases the semaphore count.
**
** Input parameter:
**				semaphore	: Counting semaphore id
**
** Return codes:
**				EXIT_OK		: Success
**				EXIT_ERROR	: Wait error
*/
int OSSRV_counting_semaphore_wait(OSSRV_counting_semaphore_t  *semaphore);


/* OSSRV_counting_semaphore_trywait
**
** This is a non-blocking variant of OSSRV_counting_semaphore_wait.
** If the counting semaphore has a non-zero count, the count is atomically decreased and the function
** immediately returns 'success'. If the semaphore count is zero, the function immediately returns with
** the code 'failed'.
**
** Input parameter:
**				semaphore	: Counting semaphore id
**
** Return codes:
**				0			: Wait failed (semaphore count is zero)
**				1			: Wait success (semaphore count is non-zero)
**				EXIT_ERROR	: Wait error (e.g. counting semaphore is not valid)
*/
int OSSRV_counting_semaphore_trywait(OSSRV_counting_semaphore_t  *semaphore);


/* OSSRV_counting_semaphore_wait
**
** Suspend the calling thread until the counting semaphore has a non-zero count,  
** then atomically decreases the semaphore count.
**
** Input parameter:
**				semaphore	 : Counting semaphore id
**              milliseconds : time-out interval in milliseconds
** Return codes:
**				EXIT_OK		: Success
**				EXIT_ERROR	: Wait error
**              TIME_OUT    : Time out wating to the semaphore 
*/              
int OSSRV_counting_semaphore_wait_timeout(OSSRV_counting_semaphore_t  *semaphore,
                                          uint32_t                    milliseconds);


/* OSSRV_counting_semaphore_close
**
** Close a counting semaphore.
** After a counting semaphore has been closed, the only manipulation it supports is init.
**
** Input parameter:
**				semaphore	: Counting semaphore id
**
** Return codes:
**				EXIT_OK		: Success
**				EXIT_ERROR	: Close error
*/
int OSSRV_counting_semaphore_close(OSSRV_counting_semaphore_t  *semaphore);


/* OSSRV_getcwd
** 
** Get the current working directory
**
** Input parameters:
**				buf	   : Storage location for path
**              len	   : Maximum length of path in characters
**
** Output parameter
**				Pointer to buffer with the current working directory on success 
**				else return NULL
*/
char *OSSRV_getcwd(char *buf, int len);


/* OSSRV_getpagesize
** 
** The function returns the number of bytes in a memory page
**
** Input parameter:
**
** Return parameter:
**                          The page size on success
**                          else return value <= 0
**				
*/
long OSSRV_getpagesize(void);


/* OSSRV_snprintf
** 
** Write formatted data to a string
**
**
** Input parameter:
**              maxlen	   : Maximum number of characters to store.
**              format     : Format-control string    
**
** Output parameters: 
**				buf	       : Storage location for output.
** Return codes:
**				returns the number of bytes stored in buffer, not counting the terminating null character
**				If the number of bytes required to store the data exceeds maxlen, 
**              then count bytes of data are stored in buffer and a negative value is returned
*/
int OSSRV_snprintf(char *buf, size_t maxlen, const char *format, ...);


/* OSSRV_gettimeofday
** 
** The function gets the time as well as a timezone.
**
** Input parameter:
** Output parameter:
**              tv         : the number of seconds and microseconds since the Epoch 
**                           (00:00:00 UTC, January 1, 1970)   
**              tz         : Not available
** Return codes:
**				EXIT_OK    : Success
**				EXIT_ERROR : Error 
*/
int OSSRV_gettimeofday(struct timeval * tv,struct timezone *tz);



#endif /* _OSSRV_EXPO_H__*/

