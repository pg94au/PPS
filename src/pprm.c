/*
 *  pprm.c
 *
 *  Client to delete queued jobs from spool system.
 *
 *  (c)1997-1999 Paul Grebenc
 *
 *  $Header: /usr/local/PPS_RCS/src/pprm.c,v 1.2 2000/05/09 01:05:00 paul Exp $
 */


#include <config.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>


#include "pps.h"
#include "locking.h"
#include "database.h"


int main(int argc, char *argv[]);
int parse_arguments(int argc, char *argv[], int *queue_idx, struct pps_queue *queue, int *all);
void remove_all_jobs(int queue_idx, struct pps_queue *queue);
void remove_selected_jobs(int argc, char *argv[], int queue_idx, struct pps_queue *queue);
int remove_jobnum(int queue_idx, struct pps_queue *queue, int jobnum);
int check_access(int id, struct inf *inf, int keyval, struct pps_queue *queue);
void notify_queue_manager_deleted(int id, struct inf *inf, struct pps_queue *queue);
void notify_queue_manager_delete_requested(int id, struct inf *inf, int keyval);
int notify_ppd_delete_job(int id, int keyval);
int signal_ppd(int keyval);
void sigpipe_handler(int type);
void ack_handler(int type);
int lookup_queue(char *name, struct pps_queue *queue);

int calculate_head(int queue_index);
int calculate_tail(int queue_index);
void get_head_tail(int queue_index, int *head, int *tail);


int broken_pipe = 0;
int ack;	/* used as a flag that acknowledgement has been received from a process */


int main(int argc, char *argv[])
{
	int all;
	int queue_idx;
	struct pps_queue queue;
	int x;


	/* parse our command line arguments */
	if (parse_arguments(argc, argv, &queue_idx, &queue, &all) == -1)
		exit(0);
	/* */

	/* delete whichever jobs were specified to be deleted */
	if (all == 1)
		remove_all_jobs(queue_idx, &queue);
	else
		remove_selected_jobs(argc, argv, queue_idx, &queue);
	/* */
}


int parse_arguments(int argc, char *argv[], int *queue_idx, struct pps_queue *queue, int *all)
{
	int i, j;
	char queuename[33];
	int jobnum;
	int job_specified;


	/* initialize */
	queuename[0] = '\0';
	*all = 0;
	job_specified = 0;
	/* */

	/* go through each argument, parsing it out */
	for (i=1; i<argc; i++)
	{
		/* check switches which start with a dash and verify job numbers that don't */
		if (argv[i][0] == '-')
		{
			switch(argv[i][1])
			{
				case 'q':
				case 'P':	/* for lprm compatibility */
					if (queuename[0] == '\0')
						strncpy(queuename, &argv[i][2], 33);
					else
					{
						fprintf(stderr, "pprm: multiple queue specification not allowed\n");
						exit(0);
					}
					break;
				case 0:		/* just a dash, on it's own, means delete all jobs */
					if (*all == 0)
					{
						*all = 1;
						job_specified = 1;
					}
					else
					{
						fprintf(stderr, "pprm: multiple deletion of all jobs specification not allowed\n");
						exit(0);
					}
					break;
				default:
					fprintf(stderr, "pprm: urecognized command \"%s\"\n", argv[i]);
					exit(0);

					break;
			}
		}
		else
		{
			/* this has to be a job number, so we verify it is all numeric */
			for (j=0; j < strlen(argv[i]); j++)
			{
				if (!isdigit(argv[i][j]))
				{
					fprintf(stderr, "pprm: unrecognized command \"%s\"\n", argv[i]);
					exit(0);
				}
			}
			/* */

			/* while we're at it, let's verify that it's within a valid range */
			if ((sscanf(argv[i], "%d", &jobnum) != 1) || (jobnum < 0) || (jobnum >= 1000))
			{
				fprintf(stderr, "pprm: invalid job number \"%s\"\n", argv[i]);
				exit(0);
			}
			/* */

			/* a valid job was specified */
			job_specified = 1;
			/* */
		}
		/* */
	}
	/* */

	/* check whether this queue exists */
	*queue_idx = lookup_queue(queuename, queue);
	if (*queue_idx == -1)
	{
		/* this queue does not exist, or database lookup failed */
		fprintf(stderr, "pprm: queue [%s] does not exist\n", queuename);

		exit(0);
	}
	/* */

	/* ensure that at least one job to be removed was specified */
	if (job_specified == 0)
	{
		fprintf(stderr, "pprm: must specify at least one job to be removed\n");

		exit(0);
	}
	/* */

	return 0;
}



void remove_all_jobs(int queue_idx, struct pps_queue *queue)
{
	int head, tail, jobnum;


	/* get the range of jobs we need to remove */
	get_head_tail(queue_idx, &head, &tail);
	/* */

	/* go through, trying to delete each of them */
	jobnum = tail;
	printf("\n");
	for(;;)
	{
		/* remove this job */
		remove_jobnum(queue_idx, queue, jobnum);
		/* */

		if (jobnum == head)
			return;
		jobnum = ((jobnum + 1) % (QUEUE_LENGTH * 2));
	}
	/* */
}



void remove_selected_jobs(int argc, char *argv[], int queue_idx, struct pps_queue *queue)
{
	int i, j, jobnum, isnum;


	/* go through command line arguments, attempting to delete any job for which a number was sent */
	for (i=1; i<argc; i++)
	{
		/* check if this is a numeric argument */
		isnum = 1;
		for (j=0; j < strlen(argv[i]); j++)
		{
			if (!isdigit(argv[i][j]))
			{
				isnum = 0;
			}
		}
		/* */

		/* delete job if one was specified */
		if (isnum == 1)
		{
			/* get the actual job number */
			if (sscanf(argv[i], "%d", &jobnum) != 1)
			{
				fprintf(stderr, "pprm: error reading argument \"%s\"\n", argv[i]);
				exit(0);
			}
			/* */

			/* remove this job */
			remove_jobnum(queue_idx, queue, jobnum);
			/* */
		}
		/* */
	}
	/* */
}



int remove_jobnum(int queue_idx, struct pps_queue *queue, int jobnum)
{
	int fd, inf_fd, result;
	char path[1024], line[1024];
	struct inf inf;


	/*** attempt to lock .lock file in our queue directory ***/

	/* first open the file if we can */
	sprintf(path, "%s/spool/%d/.lock", ROOT, queue_idx);
	fd = open(path, O_CREAT | O_RDWR, S_IRWXU);
	/* */

	if (fd == -1)
	{
		/* cannot open or create .lock file */
		sprintf(line, "pprm: cannot open or create [%s]", path);
		perror(line);
		exit(0);
		/* */
	}

	/* opened .lock file no problem, so we write lock it */
	result = wait_lock_file(fd);
	/* */

	/* quit on error */
	if (result == -1)
	{
		/* couldn't write lock the .lock file... curious, */
		/* since we are blocking on our lock attempt */
		sprintf(line, "pprm: cannot lock [%s]", path);
		perror(line);
		exit(0);
		/* */
	}
	/* */

	/* check to see whether user has the right to remove this file */
	if (check_access(jobnum, &inf, queue_idx, queue))
	{
		/* try to get a write lock on the inf file, if we can, then we have the */
		/* right to delete it, because it's not currently being printed */
		sprintf(path, "%s/spool/%d/inf%d", ROOT, queue_idx, jobnum);
		inf_fd = open(path, O_RDWR);
		/* */

		/* if we cannot open the file due to error other than no such file, report */
		if ((inf_fd == -1) && (errno != ENOENT))
		{
			perror("pprm: cannot open requested job's inf file");
			exit(0);
		}
		/* */

		/* try to get a lock on the inf file to see if we're allowed to delete it */
		if (lock_file(inf_fd) != -1)
		{
			/*** got the lock, so delete the job ***/

			/* remove the inf file */
			if (unlink(path) == -1)
			{
				sprintf(line, "pprm: cannot delete [%s]", path);
				perror(line);

				exit(0);
			}
			/* */
	
			/* remove the dat file */
			sprintf(path, "%s/spool/%d/dat%d", ROOT, queue_idx, jobnum);
			if (unlink(path) == -1)
			{
				sprintf(line, "pprm: cannot delete [%s]", path);
				perror(line);

				exit(0);
			}
			/* */

			/* inform user of successful deletion */
			printf("Job %d deleted from queue %s.\n", jobnum, queue->name);
			/* */
	
			/* notify queue manager of deleted job */
			/* notify_queue_manager_deleted(jobnum, &inf, queue);	HPUX dies */
			/* */
		}
		else
		{
			/* notify queue manager of delete request sent, if ppd notices our message */
			if (notify_ppd_delete_job(jobnum, queue_idx) == 0)
			{
				printf("Job %d is currently printing.  Message to delete job sent.\n", jobnum);

				/* notify_queue_manager_delete_requested(jobnum, &inf, queue_idx);	HPUX dies */
			}
			else
			{
				printf("Job %d is currently printing.  Message to delete job failed.\n", jobnum);
			}
			/* */
		}

		/* close inf file, releasing lock (if we got it) */
		close(inf_fd);
		/* */
	}
	else
	{
		/* access denied */
		fprintf(stderr, "Cannot delete job %d from queue %s: Permission denied.\n", jobnum, queue->name);
		close(fd);
		exit(0);
		/* */
	}
	/* */

	/* close lock file, release the lock we have on it as we do */
	close(fd);
	/* */
}



int check_access(int id, struct inf *inf, int keyval, struct pps_queue *queue)
{
	FILE *fp;
	char path[256];


	/* try to open the specified job's info file */
	sprintf(path, "%s/spool/%d/inf%d", ROOT, keyval, id);
	fp = fopen(path, "r");
	/* */

	/* if we cannot open it because it, determine why */
	if (fp == NULL)
	{
		if (errno == ENOENT)
		{
			/* inf file does not exist, so job does not exist */
			fprintf(stderr, "Job %d does not exist in queue %s!\n", id, queue->name);
			exit(0);
			/* */
		}
		else
		{
			/* some other error, report it */
			perror("pprm: cannot open inf file");
			exit(0);
			/* */
		}
	}
	/* */

	/* file opened, read contents into inf structure to examine */
	if (fread((void *)inf, sizeof(struct inf), 1, fp) != 1)
	{
		/* were not able to read in one inf structure's worth from file */
		perror("pprm: could not read inf structure from inf file");
		exit(0);
		/* */
	}
	/* */

	/* close the inf file */
	fclose(fp);
	/* */

	/* determine whether user has right to remove this job */
	/* right now, you can remove it if:  - you submitted it */
	/*                                   - you are root     */
	/* NOTE:  will add a "queue manager's" unix group capability later */
	if ((inf->userid == getuid()) || (getuid() == 0) || (getgid() == OPERATOR_GID))
	{
		return 1;
	}
	else
	{
		return 0;
	}
	/* */
}



void notify_queue_manager_deleted(int id, struct inf *inf, struct pps_queue *queue)
{
	int fd;
	char path[256];
	char buff[1024];
	struct qm_job_deleted *mesg;
	struct sigaction action;


	/* catch SIGPIPE, so we don't terminate if queue manager exits */
	action.sa_handler = sigpipe_handler;
	sigemptyset(&action.sa_mask);
	action.sa_flags = 0;
	if (sigaction(SIGPIPE, &action, NULL) < 0)
		exit(0);
	/* */

	/* open the named pipe (fifo) for writing, return immediately if reader not present */
	sprintf(path, "%s/.qmpipe", ROOT);
	fd = open(path, O_WRONLY | O_NDELAY);
	if (fd == -1)
	{
		/* report error only if it is not simply a problem with the queue manager */
		/* not being there to read our notification */
		if (errno == ENXIO)
		{
			/* printf("DEBUG MESSAGE - Queue manager is not running\n"); */
		}
		else
		{
			perror("Cannot open FIFO .qmpipe");
			exit(0);
		}
		/* */
	}
	/* */

	/* build the message to send */
	buff[0] = PPS_JOB_CANCELLED;
	mesg = (struct qm_job_deleted *)(buff+1);
	strcpy(mesg->queue, queue->name);
	mesg->count = id;
	mesg->time = time(NULL);
	memcpy(&mesg->inf, inf, sizeof(struct inf));
	/* */

	/* try to send message                                        */
	/* - we don't really care if this message wasn't read because */
	/*   the queue manager stopped listening.  If it did, it did. */
	write(fd, buff, (1+sizeof(struct qm_job_deleted)));
	/* */

	/* close the named pipe (fifo) */
	close(fd);
	/* */
}



void notify_queue_manager_delete_requested(int id, struct inf *inf, int keyval)
{
	int fd;
	char path[256];
	char buff[1024];
	struct qm_job_delete *mesg;
	struct sigaction action;


	/* catch SIGPIPE, so we don't terminate if queue manager exits */
	action.sa_handler = sigpipe_handler;
	sigemptyset(&action.sa_mask);
	action.sa_flags = 0;
	if (sigaction(SIGPIPE, &action, NULL) < 0)
		exit(0);
	/* */

	/* open the named pipe (fifo) for writing, return immediately if reader not present */
	sprintf(path, "%s/.qmpipe", ROOT);
	fd = open(path, O_WRONLY | O_NDELAY);
	if (fd == -1)
	{
		/* report error only if it is not simply a problem with the queue manager */
		/* not being there to read our notification */
		if (errno == ENXIO)
		{
			/* printf("DEBUG MESSAGE - Queue manager is not running\n"); */
		}
		else
		{
			perror("Cannot open FIFO .qmpipe");
			exit(0);
		}
		/* */
	}
	/* */

	/* build the message to send */
	buff[0] = PPS_JOB_CANCEL_REQUESTED;
	mesg = (struct qm_job_delete *)(buff+1);
	mesg->keyval = keyval;
	mesg->count = id;
	memcpy(&mesg->inf, inf, sizeof(struct inf));
	/* */

	/* try to send message                                        */
	/* - we don't really care if this message wasn't read because */
	/*   the queue manager stopped listening.  If it did, it did. */
	write(fd, buff, (1+sizeof(struct qm_job_delete)));
	/* */

	/* close the named pipe (fifo) */
	close(fd);
	/* */
}



int notify_ppd_delete_job(int id, int keyval)
{
	int cmd_fd;
	char path[256];
	struct command command;
	struct ppd_job_delete mesg;
	int timeout;
	struct sigaction action;
	

	/* open for append or create .command file in queue directory */
	sprintf(path, "%s/spool/%d/.command", ROOT, keyval);
	cmd_fd = open(path, O_CREAT | O_WRONLY, S_IRWXU);
	/* */

	if (cmd_fd == -1)
	{
		perror("Cannot create or append delete request to .command file");
		exit(0);
	}

	/* lock .command file */
	if (wait_lock_file(cmd_fd) == -1)
	{
		perror("Cannot write lock .command file");
		exit(0);
	}
	/* */

	/* truncate .command file */
	if (ftruncate(cmd_fd, 0) == -1)
	{
		perror("cannot truncate .command file");
		exit(0);
	}
	/* */

	/* fill in command block that will be written to file */
	command.pid = getpid();
	command.command_code = PPD_JOB_DELETE;
	mesg.count = id;
	memcpy((struct ppd_job_delete *)&command.mesg, &mesg, sizeof(struct ppd_job_delete));
	/* */

	/* append command message to .command file */
	if (write(cmd_fd, &command, sizeof(struct command)) != sizeof(struct command))
	{
		perror("Cannot write to .command file");
		exit(0);
	}
	/* */

	/* set up signal handler to catch acknowledgement from ppd */
	action.sa_handler = ack_handler;
	sigemptyset(&action.sa_mask);
	action.sa_flags = 0;
	if (sigaction(SIGUSR2, &action, NULL) < 0)
		exit(0);
	/* */

	/* wait for response after we signal ppd to let it know we've sent something */
	ack = 0;
	if (signal_ppd(keyval) != -1)
	{
		for(timeout=0; timeout<10; timeout++)
		{
			/* sleep for a second, check if we've been ack'ed, exit if we have */
			sleep(1);
			if (ack == 1)
				break;
			/* */
		}

		return 0;

		/* at this point, ack will equal 1 if we've been signalled back */
		/* We don't really care that much, though... do we? */

		/* WE SHOULD PROBABLY BE INFORMING THE QUEUE MANAGER THAT THE REQUEST TO */
		/* DELETE A JOB WAS SENT ***AT THIS POINT***, SINCE WE KNOW ONLY NOW THAT */
		/* THAT REQUEST WAS SENT... WHETHER PPD ACTS ON IT...? */

		/* NOTE:  EVEN IF WE DON'T REALLY NEED TO HAVE THE RESPONSE FOR OUR OWN PURPOSES, */
		/*        WE *MUST* WAIT FOR A WHILE, IN ORDER TO GIVE THE RECIPIENT A CHANCE */
		/*        TO RECEIVE OUR COMMAND MESSAGE BEFORE WE RELEASE OUR WRITE LOCK ON IT */
	}
	else
	{
		/* could not confirm that message was delivered */
		return -1;
		/* */
	}
	/* */

	/* close .command file, releasing lock */
	close(cmd_fd);
	/* */
}



int signal_ppd(int keyval)
{
	int pid_fd;
	FILE *pid_fp;
	char path[256];
	pid_t pid;


	/* open child ppd's .dpid file */
	sprintf(path, "%s/spool/%d/.dpid", ROOT, keyval);
	pid_fd = open(path, O_RDWR);
	/* */

	/* if any error other than .dpid not existing occured, report it */
	if ((pid_fd == -1) && (errno != ENOENT))
	{
		perror("pprm: Cannot open .dpid file of ppd child");
		exit(0);
	}
	/* */

	/* test the lock on .dpid file to see if daemon is running */
	if (lock_file(pid_fd) != -1)
	{
		return;
	}
	/* */

	/* convert file descriptor to a stream */
	pid_fp = fdopen(pid_fd, "r");
	/* */

	/* read in the daemon's pid */
	if (fscanf(pid_fp, "%d", (int *)&pid) != 1)
	{
		perror("pprm: Error reading pid value from .dpid");
		exit(0);
	}
	/* */

	/* close files */
	fclose(pid_fp);
	close(pid_fd);
	/* */

	/* signal daemon about sent command */
	return (kill(pid, SIGUSR1));
	/* */
}



void sigpipe_handler(int type)
{
	/* set flag indicating SIGPIPE occurred */
	broken_pipe = 1;
	/* */
}



void ack_handler(int type)
{
	if (type == SIGUSR2)
	{
		/* all we're here for is to let the main thread know we've been hit */
		ack = 1;
		/* */
	}
}



int lookup_queue(char *name, struct pps_queue *queue)
{
	int sock;
	m_result *result;
	m_row row;
	int keyval;
	char string[1024];


	/* connect to MSQL */
	sock = mconnect();
	if (sock == -1)
	{
		fprintf(stderr, "Error communicating with Mini SQL.\n");
		exit(0);
	}
	/* */

	/* get the data for the queue which was specified by name */
	sprintf(string, "SELECT type,charge,name,description,status,keyval FROM queues WHERE name='%s'", name);
	result = send_query(sock, string);
	/* */

	/* return negative if this queue does not exist */
	if (msqlNumRows(result) == 0)
	{
		fprintf(stderr, "ppr: Queue \"%s\" does not exist\n", name);
		return -1;
	}
	/* */

	/* queue exists, so copy over all queue information */
	row = fetch_result(result);

	sscanf(row[0], "%d", &queue->type);
	sscanf(row[1], "%f", &queue->charge);
	strcpy(queue->name, row[2]);
	strcpy(queue->description, row[3]);
	sscanf(row[4], "%d", &queue->status);
	sscanf(row[5], "%d", &keyval);
	/* */

	return keyval;
}



int calculate_head(int queue_index)
{
	DIR *dirp;
	struct dirent *entry;
	int head;
	int val;
	char path[256];
	char buff[4];


	/* get a list of all directory entries (filenames) */
	sprintf(path, "%s/spool/%d", ROOT, queue_index);
	dirp = opendir(path);
	if (dirp == NULL)
		return -1;
	/* */

	/* initialize head to something it can't be */
	head = -1;
	/* */

	/* go through files in queue directory, looking for head value */
	while ((entry = readdir(dirp)) != NULL)
	{
		/* determine if this is an inf file */
		if (strncmp(entry->d_name, "inf", 3) == 0)
		{
			/* grab the count value from the filename */
			strncpy(buff, entry->d_name+3, 4);
			sscanf(buff, "%d", &val);
			/* */

			/* check if this is the new head value */
			if (head == -1)
				head = val;	/* first value we find */
			else
			{
				/* head is highest number, unless we are at a wrap-around point */
				if (((val > head) && (val-head < QUEUE_LENGTH)) || ((val < head) && (head-val > QUEUE_LENGTH)))
					head = val;
				/* */
			}
			/* */
		}
		/* */
	}
	closedir(dirp);
	/* */

	return head;
}



int calculate_tail(int queue_index)
{
	DIR *dirp;
	struct dirent *entry;
	int tail;
	int val;
	char path[256];
	char buff[4];


	/* get a list of all directory entries (filenames) */
	sprintf(path, "%s/spool/%d", ROOT, queue_index);
	dirp = opendir(path);
	if (dirp == NULL)
		return -1;
	/* */

	/* initialize tail to something it can't be */
	tail = -1;
	/* */

	/* go through files in queue directory, looking for tail value */
	while ((entry = readdir(dirp)) != NULL)
	{
		/* determine if this is an inf file */
		if (strncmp(entry->d_name, "inf", 3) == 0)
		{
			/* grab the count value from the filename */
			strncpy(buff, entry->d_name+3, 4);
			sscanf(buff, "%d", &val);
			/* */

			/* check if this is the new tail value */
			if (tail == -1)
				tail = val;	/* first value we find */
			else
			{
				/* tail is lowest number, unless we are at a wrap-around point */
				if (((val < tail) && (tail-val < QUEUE_LENGTH)) || ((val > tail) && (val-tail > QUEUE_LENGTH)))
					tail = val;
				/* */
			}
			/* */
		}
		/* */
	}
	closedir(dirp);
	/* */

	return tail;
}



void get_head_tail(int queue_index, int *head, int *tail)
{
	char path[256];
	char buff[1024];
	int result;


	/* calculate a tail value by examining the queue's contents */
	*tail = calculate_tail(queue_index);
	/* */

	/* check if queue is empty */
	if (*tail == -1)
	{
		/* head and tail will both be invalid if queue is empty */
		*head = -1;

		return;
	}
	/* */

	/* calculate head value... */
	/* we do know that it must exist, because we would already */
	/* have taken care of an empty queue situation and exited earlier */
	*head = calculate_head(queue_index);

	/* if head still turns out to be incalculable... */
	if (*head == -1)
	{
		fprintf(stderr, "ppq: error examining waiting jobs of this queue");
		exit(0);
	}
	/* */
}
