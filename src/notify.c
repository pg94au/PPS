/*
 *  notify.c
 *
 *  Functions used for signalling between system components.
 *
 *  (c) 1997,1998 Paul Grebenc
 *
 *  $Header: /usr/local/PPS_RCS/src/notify.c,v 1.1 1999/12/10 03:35:23 paul Exp $
 */


#include <config.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <signal.h>

#include "locking.h"
#include "pps.h"

#define SIG_JOB_PENDING		SIGUSR1
#define SIG_RELOAD_DATABASE	SIGHUP

void notify_parent_job_queued(int keyval);
void notify_parent_reload_database(void);
void notify_queue_reload_database(int keyval);
void notify_queue_job_sent(int keyval);



void notify_parent_reload_database()
{
	struct flock lock;
	int dpid_fd;
	int result;
	char path[256];
	char buff[1024];
	pid_t pid;
	int lock_status;


	/* attempt to open the daemon's .dpid file */
	sprintf(path, "%s/.dpid", ROOT);
	dpid_fd = open(path, O_RDWR);
	/* */

	/* if file can't be opened, daemon isn't there */
	if (dpid_fd == -1)
	{
		/* daemon's not there, so we just return */
		return;
	}
	/* */

	/* opened .dpid, so we'll see now if there's a write lock on it */
	result = test_lock(dpid_fd, &lock_status);
	/* */

	/* if we can't even check the lock, we may as well quit */
	if (result == -1)
	{
		return;
	}
	/* */

	/* if the lock is set, let's read the daemon's pid and signal it */
	if (lock_status == F_WRLCK)
	{
		/* file is locked, so we read it's contents to get pid */
		if (read_line(dpid_fd, buff, 8) == -1)
		{
			/* couldn't read .dpid file */
			return;
			/* */
		}
		/* */

		/* strip the pid out of the line we read in */
		sscanf(buff, "%d", &pid);
		/* */

		/* signal daemon */
		kill(pid, SIG_RELOAD_DATABASE);	/*SIGHUP*/
		/* */
	}
	/* */

	/* close .dpid file */
	close(dpid_fd);
	/* */
}



void notify_queue_reload_database(int keyval)
{
	struct flock lock;
	int dpid_fd;
	int result;
	char path[256];
	char buff[1024];
	pid_t pid;
	int lock_status;


	/* attempt to open the daemon's .dpid file */
	sprintf(path, "%s/spool/%d/.dpid", ROOT, keyval);
	dpid_fd = open(path, O_RDWR);
	/* */

	/* if file can't be opened, daemon isn't there */
	if (dpid_fd == -1)
	{
		/* daemon's not there, so we just return */
		return;
	}
	/* */

	/* opened .dpid, so we'll see now if there's a write lock on it */
	result = test_lock(dpid_fd, &lock_status);
	/* */

	/* if we can't even check the lock, we may as well quit */
	if (result == -1)
	{
		return;
	}
	/* */

	/* if the lock is set, let's read the daemon's pid and signal it */
	if (lock_status == F_WRLCK)
	{
		/* file is locked, so we read it's contents to get pid */
		if (read_line(dpid_fd, buff, 8) == -1)
		{
			/* couldn't read .dpid file */
			return;
			/* */
		}
		/* */

		/* strip the pid out of the line we read in */
		sscanf(buff, "%d", &pid);
		/* */

		/* signal daemon */
		kill(pid, SIG_RELOAD_DATABASE);		/*SIGHUP*/
		/* */
	}
	/* */

	/* close .dpid file */
	close(dpid_fd);
	/* */
}



/* combination of add_pending and notify_daemon from ppr.c - can separate all this to another file some day */
void notify_parent_job_queued(int keyval)
{
	struct flock lock;
	int fd;
	int result;
	char pendingpath[256];
	int dpid_fd;
	char path[256];
	char buff[1024];
	pid_t pid;
	int lock_status;


	/* first open the file if we can */
	sprintf(pendingpath, "%s/.pending", ROOT);
	fd = open(pendingpath, O_RDWR | O_CREAT | O_APPEND, S_IRWXU);
	/* */

	if (fd == -1)
	{
		/* don't know why we couldn't open it */
		return;
		/* */
	}

	/* opened .pending file, so we write lock it */
	result = lock_file(fd);
	/* */

	/* quit on error, can't lock file */
	if (result == -1)
	{
		/* couldn't write lock .pending file... curious, */
		/* since we are blocking on our lock attempt */
		return;
		/* */
	}
	/* */

	/*** got the lock, appending our queue name ***/

	/* write out the queue name, and add a linefeed */
	sprintf(buff, "%d\n", keyval);
	if (write(fd, buff, strlen(buff)) == -1)
	{
		/* we'd have to be out of space, I think... */
		return;
		/* */
	}
	/* */

	/* close (unlocking) .pending file */
	close(fd);
	/* */

	/* attempt to open the daemon's .dpid file */
	sprintf(path, "%s/.dpid", ROOT);
	dpid_fd = open(path, O_RDWR);
	/* */

	/* if file can't be opened, daemon isn't there */
	if (dpid_fd == -1)
	{
		/* daemon's not there, so we just exit */
		/* and leave our job in the queue */
		return;
	}
	/* */

	/* opened .dpid, so we'll see now if there's a write lock on it */
	result = test_lock(dpid_fd, &lock_status);
	/* */

	/* if we can't even check the lock, we may as well quit */
	if (result == -1)
	{
		return;
	}
	/* */

	/* if the lock is set, let's read the daemon's pid and signal it */
	if (lock_status == F_WRLCK)
	{
		/* file is locked, so we read it's contents to get pid */
		if (read_line(dpid_fd, buff, 8) == -1)
		{
			/* couldn't read .dpid file */
			return;
			/* */
		}
		/* */

		/* strip the pid out of the line we read in */
		sscanf(buff, "%d", &pid);
		/* */

		/* signal daemon */
		kill(pid, SIG_JOB_PENDING);	/*SIGUSR1*/
		/* */
	}
	/* */

	/* close .dpid file */
	close(dpid_fd);
	/* */
}



int read_line(int fd, char *ptr, int maxlen)
{
        int n, rc;
        char c;

        for (n=1; n < maxlen; n++)
        {
                if ((rc = read(fd, &c, 1)) == 1)
                {
                        *ptr++ = c;
                        if (c == '\n')
                                break;
                }
                else
                {
                        if (rc == 0)
                        {
                                if (n == 1)
                                        return(0);      /* EOF, no data read */
                                else
                                        break;          /* EOF, some data read */
                        }
                        else
                        {
                                return (-1);    /* error */
                        }
                }
        }

        *ptr = 0;
        return(n);
}



void notify_queue_job_sent(int keyval)
{
	int dpid_fd;
	int result;
	char path[256], buff[1024];
	pid_t pid;
	int lock_status;


	/* attempt to open the .dpid file of the daemon servicing our queue */
	sprintf(path, "%s/spool/%d/.dpid", ROOT, keyval);
	dpid_fd = open(path, O_RDWR);
	/* */

	/* if file can't be opened, daemon isn't there (must have printed the file we were spawned to already, and exited) */
	if (dpid_fd == -1)
	{
		/* no problem, just exit */
		exit(0);
	}
	/* */

	/* opened .dpid, so we'll see if there's a write lock on it */
	result = test_lock(dpid_fd, &lock_status);
	/* */

	/* if we can't even check the lock, we may as well quit */
	if (result == -1)
	{
		sprintf(buff, "ppd: cannot test lock on queue %d's .dpid file", keyval);
		perror(buff);
		exit(0);
	}
	/* */

	/* if the lock is set, let's read the daemon's pid and signal it */
	if (lock_status == F_WRLCK)
	{
		/* file is locked, so we read it's contents to get pid */
		if (read_line(dpid_fd, buff, 8) == -1)
		{
			/* couldn't read .dpid file */
			sprintf(buff, "ppd: couldn't read locked .dpid file of queue %d", keyval);
			perror(buff);
			exit(0);
			/* */
		}
		/* */

		/* strip the pid out of the line we read in */
		sscanf(buff, "%d", &pid);
		/* */

		/* signal the daemon */
		kill(pid, SIGUSR2);
		/* */
	}
	/* */

	/* close .dpid file */
	close(dpid_fd);
	/* */
}

