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
* Description                  : PMC Log
*
* $Revision: 1.1 $
* $Date: 2013/09/23 07:07:42 $
* $Author: dvoryyos $
* Release $Name:  $
*
****************************************************************************/

#include <stdio.h>
#include <stdarg.h>

#include "pmc_log.h"


/* Minimum severity to be displayed */
PMCLOG_severity_e g_min_severity = PMCLOG_DEBUG;


/* Set the minimum severity to be displayed */
void PMCLOG_min_severity_set(PMCLOG_severity_e min_severity)
{
    g_min_severity = min_severity;
}


/* Print log function */
void PMCLOG_print(const char *file, const unsigned long line, const char *func, const PMCLOG_severity_e severity,
                  const char *format, ...)
{
    va_list args;
    const char *sever = "UNKNOWN";

    if (severity < g_min_severity)
        return;

    switch (severity)
    {
    case PMCLOG_DEBUG:
        sever = "DEBUG";
        break;
    case PMCLOG_INFO:
        sever = "INFO";
        break;
    case PMCLOG_ERROR:
        sever = "ERROR";
        break;
    }


    va_start(args, format);
    printf("%-6s: %s(%lu) - %s: ", sever, file, line, func);
    vprintf(format, args);
    va_end(args);
}
