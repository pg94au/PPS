/*
 *  generic.h 
 *
 *  (c)1998 Paul Grebenc 
 *
 *  $Header: /usr/local/PPS_RCS/src/printer/generic.h,v 1.1 1999/12/10 03:26:21 paul Exp $
 */


#ifndef PPS_GENERIC
#define PPS_GENERIC

#include "pps.h"

int print_to_generic(struct inf *inf, struct eligible_printer *job_entry, int *pages);

#endif

