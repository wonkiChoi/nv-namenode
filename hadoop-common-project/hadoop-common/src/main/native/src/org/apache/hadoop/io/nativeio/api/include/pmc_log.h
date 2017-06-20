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
* Description                  : PMC Log header
*
* $Revision: 1.1 $
* $Date: 2013/09/23 07:04:23 $
* $Author: dvoryyos $
* Release $Name:  $
*
****************************************************************************/

#ifndef __PMCLOG_H__
#define __PMCLOG_H__


/* Log severities */
typedef enum
{
    PMCLOG_DEBUG = 0,
    PMCLOG_INFO = 1,
    PMCLOG_ERROR = 2
} PMCLOG_severity_e;


/* Set the minimum severity to be displayed */
void PMCLOG_min_severity_set(PMCLOG_severity_e min_severity);


/* Print log function */
void PMCLOG_print(const char *file, const unsigned long line, const char *func, const PMCLOG_severity_e severity,
                  const char *format, ...);


/* log macros */
#define PMCLOG_0(_severity_, _format_) \
	PMCLOG_print(__FILE__, __LINE__, __func__, _severity_, _format_);

#define PMCLOG_1(_severity_, _format_, _arg1_)\
	PMCLOG_print(__FILE__, __LINE__, __func__, _severity_, _format_, _arg1_);

#define PMCLOG_2(_severity_, _format_, _arg1_, _arg2_)\
	PMCLOG_print(__FILE__, __LINE__, __func__, _severity_, _format_, _arg1_, _arg2_);

#define PMCLOG_3(_severity_, _format_, _arg1_, _arg2_, _arg3_)\
	PMCLOG_print(__FILE__, __LINE__, __func__, _severity_, _format_, _arg1_, _arg2_, _arg3_);

#define PMCLOG_4(_severity_, _format_, _arg1_, _arg2_, _arg3_, _arg4_)\
	PMCLOG_print(__FILE__, __LINE__, __func__, _severity_, _format_, _arg1_, _arg2_, _arg3_, _arg4_);

#define PMCLOG_5(_severity_, _format_, _arg1_, _arg2_, _arg3_, _arg4_, _arg5_)\
	PMCLOG_print(__FILE__, __LINE__, __func__, _severity_, _format_, _arg1_, _arg2_, _arg3_, _arg4_, _arg5_);


#endif /* __PMCLOG_H__ */
