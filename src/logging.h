/*
 *  logging.h
 *
 *  Functions to provide for print and error logging.
 *
 *  (c)1997 Paul Grebenc
 *
 *  $Header: /usr/local/PPS_RCS/src/logging.h,v 1.1 1999/12/10 03:35:10 paul Exp $
 */


#ifndef LOGGING_H
#define LOGGING_H

int print_log(struct inf *inf, char *queue, char *printer, int pages, float charge, float new_balance, int result);
int admin_log(char *usercode, float *old_balance, float *new_balance);
int error_log(char *string);
char *get_username(int userid);

#endif

