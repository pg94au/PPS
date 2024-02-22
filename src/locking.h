/*
 *  locking.h
 *
 *  Functions to provide necessary file locking.
 *
 *  (c)1997 Paul Grebenc
 *
 *  $Header: /usr/local/PPS_RCS/src/locking.h,v 1.1 1999/12/10 03:35:08 paul Exp $
 */


#ifndef LOCKING_H
#define LOCKING_H

#include <stdio.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

extern int unlock_file(int fd);
extern int lock_file(int fd);
extern int wait_lock_file(int fd);
extern int test_lock(int fd, int *l_type);
int create_and_lock(char *name);
int open_and_test_lock(char *name, int *lock_status);

#endif

