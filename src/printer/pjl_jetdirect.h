/*
 *  pjl_jetdirect.h
 *
 *  (c)1998 Paul Grebenc
 *
 *  $Header: /usr/local/PPS_RCS/src/printer/pjl_jetdirect.h,v 1.1 1999/12/10 03:26:24 paul Exp $
 */


#ifndef PJL_JETDIRECT_H
#define PJL_JETDIRECT_H

#include "pps.h"


int print_to_jetdirect(struct inf *inf, struct eligible_printer *job_entry, int *pages);


#endif

