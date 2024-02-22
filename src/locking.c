/*
 *  locking.c
 *
 *  Functions to provide necessary file locking.
 *
 *  (c)1997 Paul Grebenc
 *
 *  $Header: /usr/local/PPS_RCS/src/locking.c,v 1.1 1999/12/10 03:34:55 paul Exp $
 */

#include <config.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>



int unlock_file(int fd);
int lock_file(int fd);
int wait_lock_file(int fd);
int test_lock(int fd, int *l_type);
int create_and_lock(char *name);
int open_and_test_lock(char *name, int *lock_status);


int unlock_file(int fd)
{
	struct flock lock;


	/* try to unlock file */
	lock.l_type = F_UNLCK;
	lock.l_start = 0;
	lock.l_whence = SEEK_SET;
	lock.l_len = 0;

	return (fcntl(fd, F_SETLK, &lock));
	/* */
}



int lock_file(int fd)
{
	struct flock lock;


	/* try to lock file */
	lock.l_type = F_WRLCK;
	lock.l_start = 0;
	lock.l_whence = SEEK_SET;
	lock.l_len = 0;

	return (fcntl(fd, F_SETLK, &lock));
	/* */
}



int wait_lock_file(int fd)
{
	struct flock lock;


	/* try to lock file */
	lock.l_type = F_WRLCK;
	lock.l_start = 0;
	lock.l_whence = SEEK_SET;
	lock.l_len = 0;

	return (fcntl(fd, F_SETLKW, &lock));
	/* */
}



int test_lock(int fd, int *l_type)
{
	struct flock lock;
	int result;


	/* check for a lock on this file */
	lock.l_type = F_WRLCK;
	lock.l_start = 0;
	lock.l_whence = SEEK_SET;
	lock.l_len = 0;

	result = fcntl(fd, F_GETLK, &lock);
	/* */

	/* copy lock status to l_type */
	*l_type = lock.l_type;
	/* */

	/* return result from test */
	return result;
	/* */
}



int create_and_lock(char *name)
{
	int result;
	char path[256];
	int fd;


	/* first create/open the file if we can */
	fd = open(name, O_CREAT | O_RDWR, S_IRWXU);

	if (fd == -1)
	{
		return -1;
	}
	/* */

	/* opened the file, so write lock it */
	result = lock_file(fd);

	if (result == -1)
	{
		/* someone else has the lock */
		close(fd);
		return -1;
	}
	/* */

	return fd;
}



int open_and_test_lock(char *name, int *lock_status)
{
	int result;
	char path[256];
	int fd;


	/* first open the file if we can */
	fd = open(name, O_RDWR, S_IRWXU);

	if (fd == -1)
	{
		return -1;
	}
	/* */

	/* opened the file, so test it's lock */
	result =  test_lock(fd, lock_status);
	/* */

	/* close file */
	close(fd);
	/* */

	return result;
}

