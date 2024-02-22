/*
 *  ppr.c
 *
 *  Print client used to submit jobs into a queue.
 *
 *  (c)1997-1999 Paul Grebenc
 *
 *  $Header: /usr/local/PPS_RCS/src/ppr.c,v 1.2 2000/08/12 00:41:30 paul Exp $
 */


#include <config.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <dirent.h>
#include <pwd.h>
#include <errno.h>

#include "pps.h"
#include "locking.h"
#include "logging.h"
#include "notify.h"
#include "database.h"


int main(int argc, char *argv[]);
void parse_arguments(int argc, char *argv[], char *queuename, char *username, char *filename);
int check_access(char *filename);
int calculate_head(int keyval);
int calculate_tail(int keyval);
int read_count(int keyval);
int create_dat(char *filename, int keyval, int head);
int create_inf(char *filename, int keyval, int head, char *username);
void notify_queue_manager(int keyval, int head);
void sigpipe_handler(int type);
int lookup_queue(char *name, struct pps_queue *queue);


int broken_pipe = 0;	/* set by SIGPIPE handler */
struct inf inf;



int main(int argc, char *argv[])
{
	struct flock lock;
	int fd;
	int result;
	char path[256];
	int head, tail;
	struct pps_queue queue;
	int queue_index;
	char buff[1024];
	char queuename[33], username[9], filename[1024];


	/* parse our command line arguments */
	parse_arguments(argc, argv, queuename, username, filename);
	/* */

	/* determine whether user can read this file or not */
	if (!check_access(filename))
	{
		exit(0);
	}
	/* */

	/*** check whether this queue exists, and see whether it is up right now ***/

	queue_index = lookup_queue(queuename, &queue);
	if (queue_index != -1)
	{
		/* check out this queue's status */
		if (queue.status == DOWN)
		{
			fprintf(stderr, "ppr: Queue \"%s\" is down.  Cannot print a job to this queue.\n", queuename);

			exit(0);
		}
		/* */
	}
	else
	{
		/* this queue does not exist, or database lookup failed */

		fprintf(stderr, "ppr: Cannot print to queue \"%s\"\n", queuename);

		exit(0);
	}


	/*** file was opened successfully ***/

	/*** attempt to lock .lock file in our queue directory ***/
	/* first open the file if we can */
	sprintf(path, "%s/spool/%d/.lock",ROOT,queue_index);
	fd = open(path, O_CREAT | O_RDWR, S_IRWXU);
	/* */

	if (fd == -1)
	{
		/* cannot open or create .lock file */
		sprintf(buff, "ppr: cannot open %s/spool/%d/.lock", ROOT, queue_index);
		perror(buff);
		exit(0);
		/* */
	}

	/* opened .lock file no problem, so we write lock it */
	result = lock_file(fd);
	/* */

	/* quit on error */
	if (result == -1)
	{
		/* couldn't write lock the .lock file...  curious, */
		/* since we are blocking on our lock attempt */
		perror("ppr: cannot lock .lock file");
		exit(0);
		/* */
	}
	/* */

	/*** get head value ***/

	head = calculate_head(queue_index);
	tail = calculate_tail(queue_index);

	/* check if queue is full */
	/* remember, we are always leaving at least QUEUE_LENGTH slots in our indexing system free at any time, */
	/* so that we can determine head and tail values dynamically at any time, so our queue is full when */
	/* there are only QUEUE_LENGTH free spots left. */
	if (((head + QUEUE_LENGTH) % (2 * QUEUE_LENGTH)) == tail)
	{
		fprintf(stderr, "ppr: number of jobs already queued has reached the limit - new job not queued\n");

		exit(0);
	}
	/* */

	/*** prepare entry in queue directory for submitted job ***/

	/* copy spoolfile into dat<head> */
	if (create_dat(filename, queue_index, head) == ERROR)
		exit(0);
	/* */

	/* create the inf<head> file */
	if (create_inf(filename, queue_index, head, username) == ERROR)
	{
		/* should remove .dat file here before exiting */
		sprintf(buff, "%s/spool/%d/dat%d", ROOT, queue_index, head);
		if (unlink(buff) == -1)
		{
			fprintf(stderr, "ppr: error removing spool data file - errno %d\n", errno);
		}
		/* */

		exit(0);
	}
	/* */

	/* close .lock file, releasing lock as we do */
	close(fd);
	/* */

	/*** add the name of the queue we spooled to to .pending file list ***/
	/*** and notify daemon of job, if running ***/

	notify_parent_job_queued(queue_index);

	/*** notify queue manager of job, if running ***/

	/* This causes crashes on Solaris and HP-UX... */
	/* The queue manager does not exist, to for now I'm just commenting it out. */
/*	notify_queue_manager(queue_index, head);*/
}



/* parse_arguments
**
** Parse out the command line arguments given to this program, and determine their syntactic validity.
**
** Arguments:
**	argc		the argument count as passed to main()
**	argv		the argument strings as passed to main()
**	queuename	returns the name of the queue to be printed to
**	username	returns the name of the user who the job should be charged to, or null if to the user
**			who ran the command
**	filename	returns the name of the file to be printed
**
** Exit conditions:
**	On multiple queue, user or filename specification, reporting error to user.
**	On illegal/invalid option of switch, reporting error to user.
**	On failure to specify both a queue and a filename, reporting error to user.
**	On denied attempt to charge job to another user, reporting error to user.
*/
void parse_arguments(int argc, char *argv[], char *queuename, char *username, char *filename)
{
	int i;


	/* initialize */
	queuename[0] = '\0';
	username[0] = '\0';
	filename[0] = '\0';
	/* */

	/* go through each argument, parsing it out */
	for (i=1; i<argc; i++)
	{
		/* all valid arguments start with a dash */
		if (argv[i][0] == '-')
		{
			switch(argv[i][1])
			{
				/* queue specifier */
				case 'q':
				case 'P':	/* for lpr compatibility */
					if (queuename[0] == '\0')
						strncpy(queuename, &argv[i][2], 33);
					else
					{
						fprintf(stderr, "ppr: multiple queue specification not allowed\n");
						exit(0);
					}
					break;
				/* user to charge job to */
				case 'u':
					if (username[0] == '\0')
						strncpy(username, &argv[i][2], 9);
					else
					{
						fprintf(stderr, "ppr: multiple user specification not allowed\n");
						exit(0);
					}
					break;
				default:
					fprintf(stderr, "ppr: unrecognized switch \"-%c\"\n", argv[i][1]);
					exit(0);

					break;
			}
		}
		else
		{
			/* user the first unswitched argument as the file name */
			if (filename[0] == '\0')
				strncpy(filename, argv[i], 1024);
			else
			{
				fprintf(stderr, "ppr: unrecognized command \"%s\"\n", argv[i]);

				exit(0);
			}
			/* */
		}
		/* */
	}
	/* */

	/* we need the name of a queue to do anything */
	if (queuename[0] == '\0')
	{
		fprintf(stderr, "ppr: must specify a queue to print to\n");
		exit(0);
	}
	/* */

	/* we need the name of a file to do anything */
	if (filename[0] == '\0')
	{
		fprintf(stderr, "ppr: must specify name of file to print\n");
		exit(0);
	}
	/* */

	/* check if user expects to charge this job to someone else */
	if (username[0] != '\0')
	{
		if ((getuid() != PPS_UID) && (getuid() != 0))
		{
			fprintf(stderr, "ppr: user %s cannot charge job to another user\n", getlogin());
			exit(0);
		}
	}
	/* */
}


/* sigpipe_handler
**
** Handler for SIGPIPE signals which could occur while sending messages to the queue manager, if the queue manager
** unexpectedly dies, for example.
**
** Arguments:
**	type	The type of interrupt is passed by the system, but we don't need it because this signal handler was
**			specified only to handle SIGPIPE interrupts.
*/
void sigpipe_handler(int type)
{
	/* set flag indicating SIGPIPE occured */
	broken_pipe = 1;
	/* */
}



/* create_dat
**
** Create the dat file in the spool directory which contains the actual job to be sent to the printer.
**
** Arguments:
**	filename	contains the name of the file containing the job to be printed, which must be copied to the
**				dat file in the spool directory
**	keyval		the index value of the queue to which this job is to be queued
**	head		the job number which this job is to use (ie. dat### number)
**
** Returned value:
**	Success:  OK
**	Failure:  ERROR
*/
int create_dat(char *filename, int keyval, int head)
{
	FILE *s_fp, *d_fp;
	int c;
	char destfilename[256];
	uid_t uid;


	/* change userid to the user who called this program, in order to open the file for copying */
	uid = geteuid();
	setuid(getuid());
	/* */

	/* open source file to read from */
	s_fp = fopen(filename, "r");
	if (s_fp == NULL)
	{
		fprintf(stderr, "ppr: error opening source file - errno %d\n", errno);
		return ERROR;
	}
	/* */

	/* become the pps user again, now that we have the file open */
	setuid(uid);
	/* */

	/* open destination file to write to */
	sprintf(destfilename, "%s/spool/%d/dat%d", ROOT, keyval, head);
	d_fp = fopen(destfilename, "w");
	if (d_fp == NULL)
	{
		fclose(s_fp);
		fprintf(stderr, "ppr: error creating spool data file - errno %d\n", errno);
		return ERROR;
	}
	/* */

	/* copy contents of source to destination */
	while(!feof(s_fp))
	{
		c = fgetc(s_fp);
		if (c != EOF)
			fputc(c, d_fp);
	}
	/* */

	/* close opened files */
	fclose(s_fp);
	fclose(d_fp);
	/* */

	return OK;
}



/* create_inf
**
** Create the inf file in the spool directory which contains information pertaining to its associated dat file.
**
** Arguments:
**	filename	the name of the file to be printed, as given by the user
**	keyval		the index value in the database of the queue to which this job is being spooled
**	head		the job number which this job is to use (ie. inf## number)
**	username	the name of the user to whom this job is to be attributed or charged for
**
** Returned value:
**	Success:  TRUE
**	Failure:  FALSE
*/
int create_inf(char *filename, int keyval, int head, char *username)
{
	FILE *fp;
	char destfilename[256];
	struct passwd *pwd;

	/* open the inf file for writing */
	sprintf(destfilename, "%s/spool/%d/inf%d", ROOT, keyval, head);
	fp = fopen(destfilename, "w");
	if (fp == NULL)
	{
		fprintf(stderr, "ppr: error creating spool info file - %s\n", strerror(errno));
		return ERROR;
	}
	/* */

	/** write out information pertinent for this job **/
	bzero(&inf, sizeof(struct inf));
	/* figure out who to attribute this job to for charging */
	if (strlen(username) > 0)
	{
		pwd = getpwnam(username);

		if (pwd != NULL)
		{
			inf.userid = (int)pwd->pw_uid;
		}
		else
		{
			fprintf(stderr, "ppr: error looking up user '%s' - %s\n", username, strerror(errno));
			return ERROR;
		}
	}
	else
	{
		inf.userid = (int)getuid();
	}
	/* */
	strcpy(inf.filename, filename);

	fwrite((void *)&inf, sizeof(struct inf), 1, fp);
	/** will possibly add other stuff, like timestamp, or whatever **/
	/** **/

	/* close file */
	fclose(fp);
	/* */

	return OK;
}



/* check_access
**
** Check to verify that the user actually has the required system privileges to be reading the file he or she
** wishes to print.
**
** NOTE:  This check is important, because this program executes setuid as a predefined user, which may have the
**        priviledge of reading certain files a normal user cannot.  We wish to ensure that no user can obtain
**        access to files, through this program, which he or she would not normally be able to access.
**
** Arguments:
**	filename	the name of the file, given by the user, that is to be printed
**
** Returned value:
**	TRUE	file can be read by user
**	FALSE	file cannot be read by user
**
** Exit conditions:
**	On error creating pipe or forking, notifying user of condition.
*/
int check_access(char *filename)
{
	FILE *fp;
	pid_t pid;
	int pipe_fd[2];
	char result;

	/* create the pipe used to communicate from the child */
	/* process to the parent */
	if (pipe(pipe_fd) < 0)
	{
		fprintf(stderr, "ppr: pipe error\n");
		exit(0);
	}
	/* */

	/* fork a child process which will test file accessibility */
	if ((pid = fork()) < 0)
	{
		fprintf(stderr, "ppr: fork error\n");
	}
	else
	{
		/* fork successful - code for parent and child */
		if (pid == 0)
		{
			/*** child process ***/

			/* close the parent's end of the pipe */
			close(pipe_fd[0]);

			/* become our "normal" self (not setuid pps) */
			setuid(getuid());
			setgid(getgid());

			/* try to open the file, send result to parent */
			fp = fopen(filename, "r");
			if (fp != NULL)
			{
				result = OK;
				write(pipe_fd[1], &result, 1);
				fclose(fp);
			}
			else
			{
				result = ERROR;
				write(pipe_fd[1], &result, 1);
			}
			/* */

			/* close our end of the pipe too */
			close(pipe_fd[1]);

			/* don't need child anymore */
			exit(0);
		}
		else
		{
			/*** parent process ***/

			/* close the child's end of the pipe */
			close(pipe_fd[1]);

			/* read result of access check from child */
			read(pipe_fd[0], &result, 1);

			/* close our end of the pipe too */
			close(pipe_fd[0]);

			/* return value based on success */
			if (result == OK)
				return TRUE;
			else
			{
				fprintf(stderr, "ppr: cannot open source file - %s\n", strerror(errno));
				return FALSE;
			}
			/* */
		}
		/* */
	}
	/* */
}



/* calculate_head
**
** Determine the index value of the next job to be placed at the head of the queue.  (Yes, this is backwards, we
** should be adding to the tail and removing from the head, but it really makes no difference and we don't care
** anyway.)
**
** Arguments:
**	keyval		the index value in the database of the queue to be examined
**
** Returned value:
**	the index value of the next job to be placed at the head of the queue
*/
int calculate_head(int keyval)
{
	DIR *dirp;
	struct dirent *entry;
	int head;
	int val;
	char path[256];
	char buff[4];
	int not_empty;


	/* get a list of all directory entries (filenames) */
	sprintf(path, "%s/spool/%d", ROOT, keyval);
	dirp = opendir(path);
	if (dirp == NULL)
		return -1;
	/* */

	/* initialize head to something it can't be */
	head = -1;
	/* */

	/* set a flag to determine if we can't find the head value because the directory is empty */
	not_empty = 0;
	/* */

	/* go through files in queue directory, looking for head value */
	while ((entry = readdir(dirp)) != NULL)
	{
		/* determine if this is an inf file */
		if (strncmp(entry->d_name, "inf", 3) == 0)
		{
			/* we found at least one job in the queue */
			not_empty = 1;
			/* */

			/* grab the count value from the filename */
			strncpy(buff, entry->d_name+3, 4);
			sscanf(buff, "%d", &val);
			/* */

			/* check if this is the new head value */
			if (head == -1)
				head = val;     /* first value we find */
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

	/* determine what value to return */
	if (not_empty)
	{
		/* return head value discovered from queued files if found */
		return ((head + 1) % (2*QUEUE_LENGTH));
		/* */
	}
	else
	{
		/* return head value of count file, or zero if cannot read file */
		return (read_count(keyval));
		/* */
	}
	/* */
}



/* calculate_tail
**
** Determine the index value of the next job to be removed from the tail of the queue.  (Yes, this is backwards, we
** should be adding to the tail and removing from the head, but it really makes no difference and we don't care
** anyway.)
**
** Arguments:
**	keyval		the index value in the database of the queue to be examined
**
** Returned value:
**	the index value of the next job to be removed from the tail of the queue
*/
int calculate_tail(int keyval)
{
	DIR *dirp;
	struct dirent *entry;
	int tail;
	int val;
	char path[256];
	char buff[4];


	/* get a list of all directory entries (filenames) */
	sprintf(path, "%s/spool/%d", ROOT, keyval);
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
				tail = val;     /* first value we find */
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



/* read_count
**
** Read the integer value contained in the .count file in a queue's spool directory
**
** Arguments:
**	keyval		the index value in the database of the queue whose count value we desire
**
** Returned value:
**	If the count file exists and contains a valid value, it is returned.  Otherwise, zero is returned.
*/
int read_count(int keyval)
{
	FILE *fp;
	char path[256];
	int count;


	/* open .count file */
	sprintf(path, "%s/spool/%d/.count", ROOT, keyval);
	fp = fopen(path, "r");
	/* */

	/* if file opened, read value from it */
	if (fp != NULL)
	{
		/* read value */
		fscanf(fp, "%d", &count);
		/* */

		/* close file */
		fclose(fp);
		/* */

		/* return appropriate value if acceptable */
		if ((count >=0) && (count < 2*QUEUE_LENGTH))
		{
			return count;
		}
		/* */
	}
	/* */

	/* return 0 if no acceptable value could be read */
	return 0;
	/* */
}



void notify_queue_manager(int keyval, int head)
{
	int fd;
	char path[256];
	char buff[1024];
	struct qm_job_submitted *mesg;
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
		/* report error only if it is not simply a problem with the queue manager not */
		/* being there to read our notification */
		if (errno == ENXIO)
			;
			/*printf("DEBUG MESSAGE - Queue manager is not running\n"); */
		else
		{
			perror("Cannot open FIFO .qmpipe");
			exit(0);
		}
		/* */
	}
	/* */

	/* build the message to send - code and queue name */
	buff[0] = PPS_JOB_SUBMITTED;
	mesg = (struct qm_job_submitted *)(buff+1);
	mesg->keyval = keyval;
	mesg->count = head;
	memcpy(&mesg->inf, &inf, sizeof(struct inf));
	/* */

	/* try to send the message                                    */
	/* - we don't really care if this message wasn't read because */
	/*   the queue manager stopped listening.  If it did, it did. */
	write(fd, buff, (1+sizeof(struct qm_job_submitted)));
	/* */

	/* close the named pipe (fifo) */
	close(fd);
	/* */
}



/* lookup_queue
**
** Return the index value from the database for a queue, specified by its name.
**
** Arguments:
**	name		the name of the queue to be looked up
**	queue		returns the configuration of the queue specified
**
** Returned value:
**	The index value of the queue in the database, or -1 if it does not exist.
**
** Exit conditions:
**	On error communicating with MSQL, reporting condition to user.
*/
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

