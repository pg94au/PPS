/*
 *  ppd.c
 *
 *  Print daemon to spool queued jobs to printers.
 *
 *  (c)1997-1999 Paul Grebenc
 *
 *  $Header: /usr/local/PPS_RCS/src/ppd.c,v 1.5 2000/08/11 23:55:31 paul Exp $
 */

#include <config.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <dirent.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#if HAVE_WAIT_H
#include <wait.h>
#elif HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif
#include <pwd.h>
#include <errno.h>

#include "pps.h"
#include "printer/generic.h"
#include "printer/pjl_jetdirect.h"
#include "locking.h"
#include "logging.h"
#include "database.h"
#include "filesystem.h"

/*#define DEBUG*/
char dbg_str[1024];


/* setup/initialization */
int main(int argc, char *argv[]);
int lock_dpid(void);
int create_fifo(void);
void read_database(void);

/* parent waiting for jobs */
void cleanse_queues(void);
void handle_incoming_jobs(void);
void new_job_handler(int type);
void child_exit_handler(int type);
void hangup_handler(int type);

/* child servicing jobs submitted to its queue */
void service_queue(void);
int update_eligible_printer_list(struct eligible_printer eligible_printers[MAX_PRINTERS_PER_QUEUE], int num_eligible, int queue_index);
int zombie_harvester(struct eligible_printer eligible_printers[MAX_PRINTERS_PER_QUEUE], int num_eligible);
void printing_child_exit_handler(int type);
void job_catcher(int type);
void command_handler(int type);
void dispatch_command(struct eligible_printer eligible_printers[MAX_PRINTERS_PER_QUEUE], int num_eligible);

/* child which actually prints a job to a printer */
int print_job(struct eligible_printer *job_entry);
void job_abort_handler(int type);

/* notification to queue manager from child which actually prints a job */
void notify_queue_manager_deleted(int id, struct inf *inf);
void notify_queue_manager_printed(int id, struct inf *inf);
void sigpipe_handler(int type);

/* update of user account balances upon completion of printed jobs */
int check_print_balance(char *usercode, int queue_index);
int calculate_charge(char *usercode, int queue_index, int units, float *charge);
int charge_account(char *usercode, float charge, float *new_balance);
void notify_user_of_charge(char *usercode, char *queuename, int queue_type, int units, float charge, float balance, int result);

/* calculation/update of delimiter values by queue servicing child */
int calculate_head(void);
int calculate_tail(void);
void get_head_tail(int *fd, int *head, int *tail);
void rewrite_count_file(int value);

/* queue servicing child putting down its pid */
void rewrite_value(int fd, int value);

/* used all over to read an integer value from a file */
int readint(int fd, char *ptr, int maxlen);



/* data read in from the MSQL database */
struct pps_queue queues[MAX_QUEUES];
struct printer printers[MAX_PRINTERS];
/* */

int queue_index;	/* index of the current queue being serviced in the quees array */
			/* used all over, particularly by the signal handler, which cannot receive it as an argument */
int printer_index;	/* also used in several places, also particularly in signal handler, so it too must be global */
int dpid_fd;	/* global, because children should be able to close it */
int pending_command;	/* global, because the main program, and the signal handler must have access to it */
struct command_node *command_list;	/* head of our commands received queue */

int broken_pipe = 0;	/* flag to show that queue manager has died on us while writing to it */
int reload = 0;	/* flag to show that the printer/queue database has been updated and needs to be reread */
int child_exited = 0;	/* flag to show that a child which was printing has exited and needs to be waited for */
int delete_job = 0;	/* flag to tell a process that is printing to abort it's current job and exit */



int main(int argc, char *argv[])
{
	/*** we have to be started as root, but we don't want to run as root ***/
	if (getuid() != 0)
	{
		fprintf(stderr, "ppd: this daemon must be started as root\n");
		exit(0);
	}
	else
	{
		setgid(PPS_GID);
		setuid(PPS_UID);
	}
	/*** ***/

	/*** get the lock on .dpid, so we become the print daemon ***/

	if (lock_dpid() == FALSE)
	{
		/* exit, because a daemon is already running */
		fprintf(stderr, "ppd (%d): Daemon already running, exiting.\n", getpid());
		exit(0);
		/* */
	}

	/*** create a FIFO .qmpipe, if it does not already exist ***/

	if (create_fifo() == -1)
	{
		exit(0);
	}

	/*** read in the printer and queue information from the MSQL database ***/

	read_database();

	/*** start all queues to print out jobs possibly abandoned when this server last terminated ***/

	cleanse_queues();

	/*** enter a loop wherein we handle incoming print jobs ***/

	handle_incoming_jobs();
}



int lock_dpid()
{
	int result;
	struct sigaction action;
	char path[256];
	char buff[1024];


	/*** attempt to lock .dpid file in root directory ***/

	/* first open the file if we can */
	sprintf(path, "%s/.dpid", ROOT);
	dpid_fd = open(path, O_CREAT | O_RDWR, S_IRWXU);
	/* */

	if (dpid_fd == -1)
	{
		/* can't open or create .dpid file, that's not good, so */
		/* we'd better report this */
		sprintf(buff, "ppd (%d): cannot open or create .dpid file", getpid());
		error_log(buff);
		exit(0);
		/* */
	}

	/* opened .dpid file, so we write lock it */
	result = lock_file(dpid_fd);
	/* */

	/* quit on error, cannot lock file */
	if (result == -1)
	{
		/* someone else has the lock, so we know a daemon is */
		/* already running, so we just exit quietly */
		return FALSE;
	}
	/* */

	/*** got the lock on .dpid, so we are the print daemon now ***/

	/* set up our signal handler to catch notices of submitted jobs */
	/* before we go writing our pid to .dpid for all to see */
	action.sa_handler = new_job_handler;
	sigemptyset(&action.sa_mask);
	action.sa_flags = 0;
	if (sigaction(SIGUSR1, &action, NULL) < 0)
		exit(0);
	/* */

	/* set up our signal handler to be notified whenever one of */
	/* our children exits, so we can do a wait, to prevent the */
	/* system from being swamped with zombies! */
	action.sa_handler = child_exit_handler;
	sigemptyset(&action.sa_mask);
	action.sa_flags = 0;
	if (sigaction(SIGCHLD, &action, NULL) < 0)
		exit(0);
	/* */

	/* set up our signal handler to catch hangup signals sent to us */
	/* whenever the queue/printer database is modified so that we can */
	/* reread its contents */
	action.sa_handler = hangup_handler;
	sigemptyset(&action.sa_mask);
	action.sa_flags = 0;
	if (sigaction(SIGHUP, &action, NULL) < 0)
		exit(0);
	/* */

	/* rewrite our pid into the .dpid file */
	if (ftruncate(dpid_fd, 0) == -1)
	{
		sprintf(buff, "ppd (%d): cannot truncate .dpid file", getpid());
		error_log(buff);
		exit(0);
	}
	if (lseek(dpid_fd, 0, SEEK_SET) == -1)
	{
		error_log("ppd: cannot seek in .dpid file");
		exit(0);
	}
	sprintf(buff, "%d\n", (int)getpid());
	if (!write(dpid_fd, buff, strlen(buff)))
	{
		sprintf(buff, "ppd (%d): cannot write .dpid file", getpid());
		error_log(buff);
		exit(0);
	}
	/* */

	return TRUE;
}


int create_fifo()
{
	char path[256], buff[1024];


	/* create a FIFO named .qmpipe in the root directory, if it does not already exist */
	sprintf(path, "%s/.qmpipe", ROOT);
	if ((mknod(path, S_IRUSR | S_IWUSR | S_IFIFO, 0) == -1) && (errno != EEXIST))
	{
		/* exit, because we will need the FIFO to communicate with the queue manager */
		sprintf(buff, "ppd (%d): cannot create .qmpipe FIFO - %s", getpid(), strerror(errno));
		error_log(buff);

		return -1;
	}
	/* */

	return 0;
}


void cleanse_queues()
{
	int pid;
	char buff[1024];


	queue_index = 0;

	while (queues[queue_index].type != 0)
	{
		/* fork a child to then service this queue, cleansing it of abandoned jobs, if any exist */
		if ((pid = fork()) < 0)
		{
			sprintf(buff, "ppd (%d): error forking child to service queue", getpid());
			error_log(buff);
		}
		else
		{
			/* fork successful - code for parent */
			if (pid > 0)
			{
				#ifdef DEBUG
				sprintf(dbg_str, "(%d): forked child to cleanse queue - pid %d", getpid(), pid);
				debug_log(dbg_str);
				#endif
			}
			/* */

			/* fork successfull - code for child */
			if (pid == 0)
			{
				/* close parent's open files */
				close(dpid_fd);
				/* */

				/* function to service queue */
				/* never returns, child exits when done */
				service_queue();
				/* */
			}
			/* */
		}
		/* */

		queue_index++;
	}
}



void handle_incoming_jobs()
{
	int pend_fd;
	int result;
	char path[256];
	char buff[1024];
	pid_t pid;
	int sscanf_ret;


	/* endless loop, handling submitted print jobs */
	for(;;)
	{
		/* open the .pending file if it exists */
		sprintf(path, "%s/.pending", ROOT);
		pend_fd = open(path, O_RDWR);
		/* */

		/* if we opened the file, let's go through it */
		if (pend_fd != -1)
		{
			/* we've opened the .pending file, so we lock it */
			result = wait_lock_file(pend_fd);
			/* */

			/* quit on error, can't lock the .pending file */
			if (result == -1)
			{
				sprintf(buff, "ppd (%d): cannot lock .pending file", getpid());
				error_log(buff);
				exit(0);
			}
			/* */

			/* between each queue servicing child spawned, check to see if database has changed */
			if (reload)
			{
				read_database();
				reload = 0;
			}
			/* */

			/* locked .pending, so we go through it, spawning */
			/* children to service queues in need */
			do
			{
				/* read a line from .pending */
				result = readint(pend_fd, buff, 1024);
				if (result == -1)
				{
					sprintf(buff, "ppd (%d): error reading .pending", getpid());
					error_log(buff);
					exit(0);
				}
				if (result > 0)
				{
					/*** queue name was read, service it ***/

					/* get the queue index from the line read */
					buff[strlen(buff)-1] = '\0';
					sscanf_ret = sscanf(buff, "%d", &queue_index);
					/* */

					/* service this queue only if it is a valid one */
					if ((sscanf_ret == 1) && (queue_index >= 0) && (queue_index < MAX_QUEUES)
						&& (queues[queue_index].type != 0) && (queues[queue_index].status == UP))
					{
						/* fork a child which will service the queue */
						if ((pid = fork()) < 0)
						{
							sprintf(buff, "ppd (%d): error forking child to service queue", getpid());
							error_log(buff);
						}
						else
						{
							/* fork successful - code for parent */
							if (pid > 0)
							{
								#ifdef DEBUG
								sprintf(dbg_str, "(%d): forked child to service queue - pid %d", getpid(), pid);
								debug_log(dbg_str);
								#endif
							}
							/* */

							/* fork successful - code for child */
							if (pid == 0)
							{
								/* close parent's open files */
								close(dpid_fd);
								close(pend_fd);
								/* */
	
								/* function to service queue */
								/* never returns, child exits when done */
								service_queue();
								/* */
							}
							/* */
						}
						/* */
					}
					else
					{
						/* log invalid queue submission */
						sprintf(buff, "ppd: received job for nonexistent queue %s", queues[queue_index].name);
						error_log(buff);
					}
					/* */
				}
			}
			while (result > 0);
			/* */

			/* truncate .pending now that we've read it all */
			if (ftruncate(pend_fd, 0) == -1)
			{
				sprintf(buff, "ppd (%d): cannot truncate .pending file", getpid());
				error_log(buff);
				exit(0);
			}
			/* */

			/* close .pending */
			close(pend_fd);
			/* */
		}
		/* */

		/* sleep until we get signalled about the next job sent */
		/* Of course, we will also get signalled here whenever the database is changed, and so */
		/* we will go through an empty .pending file in that case.  We will, however, have reread */
		/* the database! */
		pause();
		/* */
	}
	/* */
}



void new_job_handler(int type)
{
	/* we don't need to do anything at all in this function... all we catch this */
	/* signal for is to cause the pause()'ed main loop to wake up to handle the */
	/* next incoming job */

	#ifdef DEBUG
	sprintf(dbg_str, " *** (%d): caught a signal SIGUSR1 - NEW JOB SUBMITTED *** ", getpid());
	debug_log(dbg_str);
	#endif

	return;
}



void child_exit_handler(int type)
{
	/* clear out any zombies left by exiting children */
	while (waitpid(-1, NULL, WNOHANG) > 0)
	{
		#ifdef DEBUG
		sprintf(dbg_str, " *** (%d): caught a SIGCHLD - Queue servicing child exited *** ", getpid());
		debug_log(dbg_str);
		#endif
	}
	/* */
}



void hangup_handler(int type)
{
	reload = 1;

	#ifdef DEBUG
	sprintf(dbg_str, " *** (%d): caught a SIGHUP - Signal to reload database ***", getpid());
	debug_log(dbg_str);
	#endif
}



void service_queue()
{
	int dpid_fd, lock_fd;
	int result;
	char path[256];
	char buff[1024];
	int head, tail, new_head;
	int inf_fd;
	FILE *inf_fp, *dat_fp;
	struct eligible_printer eligible_printers[MAX_PRINTERS_PER_QUEUE];
	int next_eligible = 0, num_eligible;
	int x;
	int num_children = 0;
	int job_sent = 0;
	struct printer_status printer_status;
	struct sigaction action;


	/*** determine whether we can gain exclusive control of this queue ***/

	sprintf(path, "%s/spool/%d", ROOT, queue_index);

	/* if the spool directory for this queue does not exist, create it */
	if (mkdir_p(path, S_IRWXU) == -1)
	{
		/* cannot create the spool directory for this queue */
		sprintf(buff, "ppd/queue (%d): cannot create spool directory file for %s", getpid(), queues[queue_index].name);
		error_log(buff);
		exit(0);
	}
	/* open the spool/queue/.dpid file if we can */
	sprintf(path, "%s/spool/%d/.dpid", ROOT, queue_index);
	dpid_fd = open(path, O_CREAT | O_RDWR, S_IRWXU);
	/* */

	if (dpid_fd == -1)
	{
		/* can't open or create .dpid file, that's not good */
		sprintf(buff, "ppd/queue (%d): cannot open or create .dpid file for %s - [%s]", getpid(), queues[queue_index].name, strerror(errno));
		error_log(buff);
		exit(0);
	}

	/* opened .dpid file, so we try to write lock it */
	result = lock_file(dpid_fd);
	/* */

	/* quit on error, cannot lock file */
	if (result == -1)
	{
		/* this queue is already being serviced, if .dpid is locked, */
		/* so we just send the child servicing it a friendly tap to let it know a job was sent */
		notify_queue_job_sent(queue_index);

		exit(0);
	}
	/* */

	/*** got the lock on .dpid, so we control this queue now ***/

	/* * * here we will set up our signal handler to receive notices * * */
	/* * * of commands sent by the forthcoming queue manager         * * */
	action.sa_handler = command_handler;
	sigemptyset(&action.sa_mask);
	action.sa_flags = 0;
	if (sigaction(SIGUSR1, &action, NULL) < 0)
		exit(0);

	/* * * we want a signal handler that is going to just catch a signal * * */
	/* * * so we break out of pause() calls whenever a new job was sent  * * */
	action.sa_handler = job_catcher;
	sigemptyset(&action.sa_mask);
	action.sa_flags = 0;
	if (sigaction(SIGUSR2, &action, NULL) < 0)
		exit(0);

	/* * * here we will set up our signal handler to receive notications * * */
	/* * * whenever one of our children which prints jobs exits          * * */
	action.sa_handler = printing_child_exit_handler;
	sigemptyset(&action.sa_mask);
	action.sa_flags = 0;
	if (sigaction(SIGCHLD, &action, NULL) < 0)
		exit(0);

	/* * * here we will write our pid over the one currently in .dpid * * */
	rewrite_value(dpid_fd, (int)getpid());
	/* */

	/*** get current head and tail values, recreating them if necessary ***/
	get_head_tail(&lock_fd, &head, &tail);
	/* */

	/* build a list of printers which are attached to the queue we are serving */
	num_eligible = update_eligible_printer_list(eligible_printers, 0, queue_index);
	/* */

	/* if there are no eligible printers, this queue is up but cannot print, so we can just quit */
	if (num_eligible == 0)
	{
		exit(0);
	}
	/* */

	/* loop through all queued jobs, printing each of them out */
	do
	{
		/* unlock .lock file so jobs can be queued while we are busy printing the current one */
		if (unlock_file(lock_fd) == -1)
		{
			sprintf(buff, "ppd/queue (%d): cannot unlock .lock file for queue %s", getpid(), queues[queue_index].name);
			error_log(buff);
			exit(0);
		}
		/* */

		/* don't try to print if there is nothing left to print and we're just waiting for children to exit */
		/* Remember... we are using an index space twice the size of our maximum queue length, and leaving */
		/* at least QUEUE_LENGTH slots unused at all times, so as to allow for the dynamic determination of */
		/* head and tail values without having to store them anywhere permanently */
		if (((head >= tail) && (head-tail < QUEUE_LENGTH)) || ((head < tail) && (tail-head > QUEUE_LENGTH)))
		{
			#ifdef DEBUG
			sprintf(dbg_str, "%s (%d): Head = %d, Tail = %d", queues[queue_index].name, getpid(), head, tail);
			debug_log(dbg_str);
			#endif

			/* find next eligible printer to use, if one exists */
			for (x=0; x < num_eligible; x++)
			{
				/* any idle printer is eligible */
				if ((eligible_printers[next_eligible].status == PPS_PRINTER_IDLE) && (eligible_printers[next_eligible].delete_printer != 1))
					break;
				else
					next_eligible = (next_eligible + 1) % num_eligible;
				/* */
			}
			/* */

			/* if there is a printer ready to print a job, send it, else do nothing yet */
			if ((eligible_printers[next_eligible].status == PPS_PRINTER_IDLE) && (eligible_printers[next_eligible].delete_printer != 1))
			{
				/* we found a printer we can send a job to, so we remember this */
				job_sent = 1;
				/* */
	
				/* try to open inf<tail> in queue directory */
				sprintf(path, "%s/spool/%d/inf%d", ROOT, queue_index, tail);
				inf_fd = open(path, O_RDWR);
				if (inf_fd != -1)
				{
					/* try to lock the inf file as a sign we are using it - no point in waiting, */
					/* though, because the only reason it might be locked otherwise is to be deleted */
					if (lock_file(inf_fd) != -1)
					{
						/* got the lock on our inf file, and now we want an i/o stream for it */
						eligible_printers[next_eligible].inf_fp = fdopen(inf_fd, "r");
						/* */
	
						/* try to open dat<tail> */
						sprintf(path, "%s/spool/%d/dat%d", ROOT, queue_index, tail);
						eligible_printers[next_eligible].dat_fp = fopen(path, "r");
						if (eligible_printers[next_eligible].dat_fp != NULL)
						{
							/* prepare this idle printer for the next job */
							eligible_printers[next_eligible].jobnum = tail;
							/* */

							/** create and lock a file indicating that this printer is busy and containing its status **/
							sprintf(path, "%s/spool/%d/.%d", ROOT, queue_index, eligible_printers[next_eligible].keyval);
							if ((eligible_printers[next_eligible].lock_fd = open(path, O_CREAT | O_TRUNC | O_RDWR, S_IRWXU)) == -1)
							{
								sprintf(buff, "ppd/queue (%d): could not create printer lock file for printer %s, queue %s",
									getpid(), printers[eligible_printers[next_eligible].keyval].name, queues[queue_index].name);
								error_log(buff);

								exit(0);
							}

							/* initialize status before we lock this file, just in case ppq looks at it */
							printer_status.jobnum = eligible_printers[next_eligible].jobnum;
							printer_status.status = PPS_STARTING;
							write(eligible_printers[next_eligible].lock_fd, &printer_status, sizeof(struct printer_status));
							/* */

							if (lock_file(eligible_printers[next_eligible].lock_fd) == -1)
							{
								sprintf(buff, "ppd/queue (%d): could not lock printer lock file for printer %s, queue %s",
									getpid(), printers[eligible_printers[next_eligible].keyval].name, queues[queue_index].name);
								error_log(buff);

								exit(0);
							}
							/** **/

							/* print the job */
							if (print_job(&eligible_printers[next_eligible]) == 0)
								num_children++;
							/* */
							
						}
						else
						{
							/* delete the inf file */
							sprintf(path, "%s/spool/%d/inf%d", ROOT, queue_index, tail);
							if (unlink(path) == -1)
							{
								sprintf(buff, "ppd/queue (%d): cannot delete %s", getpid(), path);
								error_log(buff);
								exit(0);
							}
							fclose(eligible_printers[next_eligible].inf_fp);
							/* */
						}
						/* */
					}
					/* */
				}
				/* */
			}
			else
			{
				/* no job could be sent, so we remember this */
				job_sent = 0;
				/* */
			}
			/* */
		}
		else
		{
			job_sent = 0;
		}
		/* */
	
		/* stop here to wait either for a job to complete, or the database to be updated, */
		/* upon either of which events we will be signalled */
		/* BUT do not stop if we just sent a job to the printer... in that case, try right away */
		/* to see if we can send yet another, so we only wait when we find that we cannot */
		if (job_sent == 0)
		{
			pause();
		}
		/* */

		/* harvest our exited children if any exist */
		/* NOTE:  The check of the flag and its reset below is not an atomic operation, and the flag could */
		/*        possibly be set at any time by the child exit signal handler.  This does not really */
		/*        matter though, because even if a signal occurs between the check and the reset, we will */
		/*        still be doing the zombie harvest in any case.  At worst, a signal will occur after the */
		/*        flag reset, but before we have finished collecting zombies.  In this case, we will simply */
		/*        end up doing an additional but harmless (non-blocking) zombie check the next time we get */
		/*        to this conditional in the loop we are in. */
		if (child_exited)
		{
			child_exited = 0;

			num_children -= zombie_harvester(eligible_printers, num_eligible);

			/* update our eligible printer list if necessary */
			num_eligible = update_eligible_printer_list(eligible_printers, num_eligible, queue_index);
			/* */
		}
		/* */

		/* reload the database if we have been hung up since the last job was sent */
		/* NOTE:  The check of the flag and its reset below is not an atomic operation, and the flag could */
		/*        possibly be set at any time by the database change signal handler.  This does not really */
		/*        matter though, because even if a signal occurs between the check and the reset, we will */
		/*        still be doing the database update in any case.  At worst, a signal will occur after the */
		/*        flag reset, but before we have finished our update.  In this case, we will simply */
		/*        end up doing an additional but harmless database update the next time we get */
		/*        to this conditional in the loop we are in. */
		if (reload)
		{
			reload = 0;

			/* reread the contents of the queue/printer database */
			read_database();
			/* */

			/* update our eligible printer list if necessary */
			num_eligible = update_eligible_printer_list(eligible_printers, num_eligible, queue_index);
			/* */
		}
		/* */

		/* if a command has been sent to us, we will deal with it */
		/* NOTE:  There is no synchronization problem here, since our command dispatcher locks the .command file */
		/*        for the duration of it's dispatch procedure, preventing new commands from being issued at that time. */
		if (pending_command == 1)
		{
			dispatch_command(eligible_printers, num_eligible);
		}
		/* */

		/* increment tail value to print next file if we just sent one */
		if (job_sent == 1)
		{
			tail = ((tail + 1) % (2*QUEUE_LENGTH));
		}
		/* */

		/* lock .lock file while reading the head value again, so lpr can't interfere */
		wait_lock_file(lock_fd);
		new_head = calculate_head();
		if (new_head != -1)
		{
			/* we only want head values not equal to -1.  A -1 head value means the queue is empty, */
			/* so the head value has not changed. */
			head = new_head;
		}
		/* */
	}
	while (((((head >= tail) && (head-tail < QUEUE_LENGTH)) || ((head < tail) && (tail-head > QUEUE_LENGTH))) &&
		(queues[queue_index].status == UP) && (num_eligible > 0)) || (num_children > 0));
	/* */

	/* rewrite new count value */
	rewrite_count_file(tail);
	/* */

	/* close our files */
	close(dpid_fd);
	close(lock_fd);
	/* */

	exit(0);
}



int update_eligible_printer_list(struct eligible_printer eligible_printers[MAX_PRINTERS_PER_QUEUE], int num_eligible, int queue_index)
{
	int delete_count = 0;
	int x, y;
	int exists;


	/* count and mark any printers which no longer exist or are down */
	for (x=0; x < num_eligible; x++)
	{
		/* first stage - if this printer is to be deleted, we will mark it to be so, but we don't necessarily */
		/*               want to delete it yet, because it may still be printing a job */
		if ((printers[eligible_printers[x].keyval].status == DOWN) || (printers[eligible_printers[x].keyval].queue != queue_index))
		{
			eligible_printers[x].delete_printer = 1;
		}
	}

	for (x=0; x < num_eligible; x++)
	{
		/* second stage - if a printer that is to be deleted is idle, then we can safely mark it to actually */
		/*                be removed from our list of eligible printers */
		if ((eligible_printers[x].status == PPS_PRINTER_IDLE) && (eligible_printers[x].delete_printer == 1))
		{
			delete_count++;
			eligible_printers[x].keyval = -1;
		}
	}
	/* */

	/* remove any of the just marked printers from our eligible printer list */
	x = 0;
	while (delete_count > 0)
	{
		/* if this is one of the ones we just marked, remove it from the list */
		if (eligible_printers[x].keyval == -1)
		{
			/* copy the rest of the list after the deleted printer up one in the list */
			for (y=x; y < num_eligible; y++)
			{
				eligible_printers[y].keyval = eligible_printers[y+1].keyval;
				eligible_printers[y].pid = eligible_printers[y+1].pid;
				eligible_printers[y].jobnum = eligible_printers[y+1].jobnum;
				eligible_printers[y].inf_fp = eligible_printers[y+1].inf_fp;
				eligible_printers[y].dat_fp = eligible_printers[y+1].dat_fp;
				eligible_printers[y].status = eligible_printers[y+1].status;
				eligible_printers[y].delete_printer = eligible_printers[y+1].delete_printer;
				eligible_printers[y].lock_fd = eligible_printers[y+1].lock_fd;
			}
			/* */

			/* decrement our counts now that we've deleted one of the printers */
			num_eligible--;
			delete_count--;
			/* */
		}
		/* */
	}
	/* */

	/* check through all printers in the database to see if any need to be added to our eligible list */
	for (x=0; x < MAX_PRINTERS; x++)
	{
		/* add printers which have just been up'ed or attached */
		if ((printers[x].queue == queue_index) && (printers[x].status == UP))
		{
			/* check first whether this eligible printer is already in our list */
			exists = 0;
			for (y=0; y < num_eligible; y++)
			{
				if (eligible_printers[y].keyval == x)
				{
					exists = 1;
					break;
				}
			}
			/* */

			/* if this printer is not in the list, add it */
			if (!exists)
			{
				eligible_printers[num_eligible].keyval = x;
				eligible_printers[num_eligible].status = PPS_PRINTER_IDLE;
				eligible_printers[num_eligible].delete_printer = 0;
				num_eligible++;
			}
			/* */
		}
		/* */
	}
	/* */

	return num_eligible;
}



int zombie_harvester(struct eligible_printer eligible_printers[MAX_PRINTERS_PER_QUEUE], int num_eligible)
{
	pid_t zombie_pid;
	int exit_status;
	int printer_num;
	char path[256], buff[1024];
	int num_children = 0;
	struct inf zombie_inf;


	/* keep looping as long as there are zombies to wait for */
	/* -- do not wait if there are none left */
	while ((zombie_pid = waitpid(-1, &exit_status, WNOHANG)) > 0)
	{
		/* keep a count of how many children we collected, for service_queue() */
		num_children++;
		/* */

		/* find the entry in our eligible_printer table that this child is associated with */
		printer_num = 0;
		while ((printer_num < num_eligible) && (eligible_printers[printer_num].pid != zombie_pid))
			printer_num++;
		/* */

		/* check if the child exited on its own */
		if (WIFEXITED(exit_status))
		{
			switch(WEXITSTATUS(exit_status))
			{
				case PPS_JOB_PRINTED:
					#ifdef DEBUG
					sprintf(dbg_str, "%s (%d): harvested a printed job - pid %d", queues[queue_index].name, getpid(), zombie_pid);
					debug_log(dbg_str);
					#endif
					break;

				case PPS_JOB_CANCELLED:
					#ifdef DEBUG
					sprintf(dbg_str, "%s (%d): harvested an cancelled job - pid %d", queues[queue_index].name, getpid(), zombie_pid);
					debug_log(dbg_str);
					#endif
					break;

				case PPS_PRINTER_RESET:
					#ifdef DEBUG
					sprintf(dbg_str, "%s (%d): harvested an reset job - pid %d", queues[queue_index].name, getpid(), zombie_pid);
					debug_log(dbg_str);
					#endif
					break;

				case PPS_JOB_TIMEOUT:
					#ifdef DEBUG
					sprintf(dbg_str, "%s (%d): harvested an timed out job - pid %d", queues[queue_index].name, getpid(), zombie_pid);
					debug_log(dbg_str);
					#endif
					break;

				case PPS_LOW_BALANCE:
					#ifdef DEBUG
					sprintf(dbg_str, "%s (%d): harvested an uprinted job due to low balance - pid %d", queues[queue_index].name, getpid(), zombie_pid);
					debug_log(dbg_str);
					#endif
					break;
			}

			/* set this printer to idle again */
			eligible_printers[printer_num].status = PPS_PRINTER_IDLE;
			/* */
		}
		else
		{
			/* we should probably be finding out here why our child did not exit gracefully... */
			/* do something like check the printer or whatever...  right now we really haven't */
			/* got time to worry about this yet, so we'll just assume that printer is idle again */
			#ifdef DEBUG
			sprintf(dbg_str, "%s (%d): harvested an ungracefully terminated job - pid %d", queues[queue_index].name, zombie_pid, getpid());
			debug_log(dbg_str);
			#endif

			/* set this printer to idle again */
			eligible_printers[printer_num].status = PPS_PRINTER_IDLE;
			/* */
			
			/*** we should also probably be logging the fact that something bad happened ***/
			/*** while printing to this printer ***/
			rewind(eligible_printers[printer_num].inf_fp);
			if (fread(&zombie_inf, sizeof(struct inf), 1, eligible_printers[printer_num].inf_fp) != 1)
			{
				/* NOTE! */
				/* We always end up here as things currently stand, because the child we forked reads from the inf file pointer */
				/* passed to it.  I don't think this should happen!  It seems from all the documentation I've read that a parent */
				/* and child can both access a file without any problem (other than synchronization).  However, it does not work, */
				/* we always get an EOF... even after rewinding...  This could be worked around by reopening the file in the child */
				/* process, as opposed to using the forked copy of the file pointer, but I don't want to do that until I find out why */
				/* this doesn't work... at least not yet. */
				sprintf(buff, "ppd/queue (%d): cannot read inf file (will be mended) for abnormal termination of job sent to '%s' on '%s'",
					getpid(), printers[eligible_printers[printer_num].keyval].name,
					queues[printers[eligible_printers[printer_num].keyval].queue].name);
				error_log(buff);
			}
			else
			{
				sprintf(buff, "ppd/queue (%d): abnormal termination while printing job to '%s' on '%s' for user '%s'",
					getpid(), printers[eligible_printers[printer_num].keyval].name,
					queues[printers[eligible_printers[printer_num].keyval].queue].name, get_username(zombie_inf.userid));
				error_log(buff);
			}
			/*** ***/
		}
		/* */

		/* close the inf, dat and lock files */
		fclose(eligible_printers[printer_num].inf_fp);
		fclose(eligible_printers[printer_num].dat_fp);
		close(eligible_printers[printer_num].lock_fd);
		/* */

		/* delete this job's inf and dat files from the spool directory */
		sprintf(path, "%s/spool/%d/dat%d", ROOT, queue_index, eligible_printers[printer_num].jobnum);
		if (unlink(path) == -1)
		{
			sprintf(buff, "ppd/queue (%d): cannot delete %s", getpid(), path);
			error_log(buff);
			exit(PPS_ERROR);
		}
		sprintf(path, "%s/spool/%d/inf%d", ROOT, queue_index, eligible_printers[printer_num].jobnum);
		if (unlink(path) == -1)
		{
			sprintf(buff, "ppd/queue (%d): cannot delete %s", getpid(), path);
			error_log(buff);
			exit(PPS_ERROR);
		}
		/* */
	}
	/* */

	return num_children;
}



void printing_child_exit_handler(int type)
{
	/* all we need to do is set the child_exited flag, because the main thread takes care of everything else */
	child_exited = 1;
	/* */

	#ifdef DEBUG
	sprintf(dbg_str, " *** %s (%d): caught a SIGCHLD - A printing child exited *** ", queues[queue_index].name, getpid());
	debug_log(dbg_str);
	#endif
}


void job_catcher(int type)
{
	#ifdef DEBUG
	sprintf(dbg_str, " *** %s (%d): caught a SIGUSR2 - We've been told about a new job *** ", queues[queue_index].name, getpid());
	debug_log(dbg_str);
	#endif

	return;
}



int print_job(struct eligible_printer *job_entry)
{
	int x;
	struct inf inf;
	int pipe_fd[2];
	char buff[1024];
	int units, result;
	float charge, new_balance;
	struct sigaction action;


	/*** open a pipe here so that the child can communicate its setting of its signal handler to the parent ***/
	if (pipe(pipe_fd) < 0)
	{
		sprintf(buff, "ppd/queue (%d): unable to create communication pipe for synchronization with printing child", getpid());
		error_log(buff);
		exit(0);
	}
	/* */

	/* attempt to fork the child process to print the job */
	if ((job_entry->pid = fork()) < 0)
	{
		sprintf(buff, "ppd/queue (%d): error forking child to print job #%d to printer %s attached to queue %s\n",
			getpid(), job_entry->jobnum, printers[job_entry->keyval].name, queues[queue_index].name);
		error_log(buff);

		return -1;
	}

	/* parent's code which returns to service_queue() */
	if (job_entry->pid != 0)
	{
		#ifdef DEBUG
		sprintf(dbg_str, "%s (%d): child forked to print job to printer - pid %d", queues[queue_index].name, getpid(), job_entry->pid);
		debug_log(dbg_str);
		#endif

		job_entry->status = PPS_PRINTING;

		/*** parent waits at this point for the child to communicate via the pipe to let it know ***/
		/*** that it has set up its signal handler, before we return ***/

		/* close child's end of pipe */
		close(pipe_fd[1]);

		/* wait for the child to write */
		read(pipe_fd[0], &x, 1);

		/* close our end of the pipe */
		close(pipe_fd[0]);
		/* */

		return 0;
	}
	/* */

	/* * * ONLY THE FORKED CHILD GETS TO THIS POINT * * */

	/*** child sets its signal handler, and then communicates to the parent via the pipe indicating that ***/
	/*** it has done so ***/

	/* set out printer index value globally */
	printer_index = job_entry->keyval;
	/* */

	/* set signal handler to catch job abort requests */
	action.sa_handler = job_abort_handler;
	sigemptyset(&action.sa_mask);
	action.sa_flags = 0;
	if (sigaction(SIGUSR1, &action, NULL) < 0)
		exit(0);
	/* */

	/* close the parent's end of pipe */
	close(pipe_fd[0]);

	/* let parent know that our signal handler is established */
	write(pipe_fd[1], &x, 1);

	/* close our end of the pipe */
	close(pipe_fd[1]);
	/* */

	#ifdef DEBUG
	sprintf(dbg_str, "%s/%s (%d): printing file #%d to queue %s, printer %s...", queues[queue_index].name, printers[job_entry->keyval].name,
		getpid(), job_entry->jobnum, queues[queue_index].name, printers[printer_index].name);
	debug_log(dbg_str);
	#endif

	/* read in the inf file */
	rewind(job_entry->inf_fp);
	if (fread(&inf, sizeof(struct inf), 1, job_entry->inf_fp) != 1)
	{
		sprintf(buff, "ppd/queue/printer (%d): cannot read %s/spool/%d/inf%d", getpid(), ROOT, queue_index, job_entry->jobnum);
		error_log(buff);
		exit(PPS_ERROR);
	}
	/* */

	/* check whether this user has the ability (account balance-wise) to print to this queue */
	if (check_print_balance(get_username(inf.userid), queue_index) != OK)
	{
		#ifdef DEBUG
		sprintf(dbg_str, "%s/%s (%d): user %s cannot print to printer %s - insufficient balance",
			queues[queue_index].name, printers[printer_index].name,
			getpid(), get_username(inf.userid), queues[queue_index].name);
		debug_log(dbg_str);
		#endif

		exit(PPS_LOW_BALANCE);
	}
	/* */

	/* determine how to print the job and do it */
	switch(printers[job_entry->keyval].type)
	{
		case PPS_PRINTER_GENERIC:
			result = print_to_generic(&inf, job_entry, &units);
			#ifdef DEBUG
			sprintf(dbg_str, "%s/%s (%d): printed to generic:  result = %d, units = %d",
				queues[queue_index].name, printers[printer_index].name, getpid(), result, units);
			debug_log(dbg_str);
			#endif
			break;
		case PPS_PRINTER_PJL_JETDIRECT:
			result = print_to_jetdirect(&inf, job_entry, &units);
			#ifdef DEBUG
			sprintf(dbg_str, "%s/%s (%d): printed to jetdirect:  result = %d, units = %d",
				queues[queue_index].name, printers[printer_index].name, getpid(), result, units);
			debug_log(dbg_str);
			#endif
			break;
	}
	/* */

	calculate_charge(get_username(inf.userid), queue_index, units, &charge);
	charge_account(get_username(inf.userid), charge, &new_balance);

	/* log printed job to the print_log file */
	print_log(&inf, queues[queue_index].name, printers[job_entry->keyval].name, units, charge, new_balance, result);
	/* */

	/* notify user of the charge to their account for this job if there was one and if configured to do so */
	notify_user_of_charge(get_username(inf.userid), queues[queue_index].name, queues[queue_index].type, units, charge, new_balance, result);
	/* */

	/* notify queue manager that this job has finished printing */
	/*notify_queue_manager_printed(job_entry->jobnum, &inf);	HPUX dies here */
	/* */

	exit(result);
}



void job_abort_handler(int type)
{
	delete_job = 1;

	#ifdef DEBUG
	sprintf(dbg_str, " *** %s/%s (%d): received a SIGUSR1, signal to abort current job *** ",
		queues[queue_index].name, printers[printer_index].name, getpid());
	debug_log(dbg_str);
	#endif
}



void dispatch_command(struct eligible_printer eligible_printers[MAX_PRINTERS_PER_QUEUE], int num_eligible)
{
	char path[256], buff[1024];
	int x;
	int cmd_fd;
	struct command *command;
	struct command_node *temp;


	/* first we must obtain a write lock on our command file to prevent incoming commands */
	sprintf(path, "%s/spool/%d/.command", ROOT, queue_index);
	cmd_fd = open(path, O_CREAT | O_WRONLY);
	if (cmd_fd == -1)
	{
		sprintf(buff, "ppd/queue (%d): cannot create or open %s", getpid(), path);
		error_log(buff);
		exit(0);
	}
	wait_lock_file(cmd_fd);
	/* */

	/* process all pending commands now */
	while (command_list != NULL)
	{
		/* do the right thing, based on command code value */
		command = &command_list->command;
		switch(command->command_code)
		{
			case PPD_JOB_DELETE:
				/* determine whether this job is currently being printed */
				x = 0;
				while ((x < num_eligible) && (eligible_printers[x].jobnum != ((struct ppd_job_delete *)command->mesg)->count))
					x++;
				if ((x < num_eligible) && (eligible_printers[x].status != PPS_PRINTER_IDLE))
					kill(eligible_printers[x].pid, SIGUSR1);

				break;
			default:
				/* undefined command code */
				sprintf(buff, "ppd/queue (%d): Invalid command code in message received, code=%d\n", getpid(), command->command_code);
				error_log(buff);
				exit(0);
		}
		/* */

		/* remove that command from the queue */
		temp = command_list->next;
		free(command_list);
		command_list = temp;
		/* */
	}
	/* */

	/* close the .command file, releasing our lock */
	close(cmd_fd);
	/* */
}



void command_handler(int type)
{
	FILE *fp;
	struct command_node *new, *last;
	char command[COMMAND_LENGTH];
	char path[256];


	/* if the signal is for an incoming command, set a flag for our main thread to check */
	if (type == SIGUSR1)
	{
		#ifdef DEBUG
		sprintf(dbg_str, " *** %s (%d): we've been hit with a command *** ", queues[queue_index].name, getpid());
		debug_log(dbg_str);
		#endif

		/* open the .command file */
		sprintf(path, "%s/spool/%d/.command", ROOT, queue_index);
		fp = fopen(path, "r");
		/* */

		/* ignore command if we cannot open the .command file - tough luck for the sender */
		if (fp == NULL)
		{
			return;
		}
		/* */

		/* read the command from the file, acknowledge and process it */
		if (fread(&command, COMMAND_LENGTH, 1, fp) == 1)
		{
			/* acknowledge the command by signalling back to sender */
			if (kill(*((pid_t *)&command), SIGUSR2) != -1)
			{
				#ifdef DEBUG
				sprintf(dbg_str, "%s (%d): we've hit back", queues[queue_index].name, getpid());
				debug_log(dbg_str);
				#endif

				/* insert command into pending commands queue */
				if (command_list == NULL)
				{
					/* insert first node into queue */
					new = (struct command_node *)malloc(sizeof(struct command_node));
					new->next = NULL;
					command_list = new;
					memcpy(&new->command, &command, COMMAND_LENGTH);
					/* */
				}
				else
				{
					/* attach node to end of existing queue */
					new = command_list;
					while (new->next != NULL)
					{
						last = new;
						new = new->next;
					}
					new = (struct command_node *)malloc(sizeof(struct command_node));
					new->next = NULL;
					last->next = new;
					memcpy(&new->command, &command, COMMAND_LENGTH);
					/* */
				}
				/* */

				/* ensure main thread knows it has a command pending */
				pending_command = 1;
				/* */
			}
			/* */
		}
		/* */
	}
	/* */
}



void notify_queue_manager_deleted(int id, struct inf *inf)
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
			sprintf(buff, "ppd/queue/printer (%d): cannot open FIFO .qmpipe to write to queue manager\n", getpid());
			error_log(buff);
			exit(0);
		}
		/* */
	}
	/* */

	/* build the message to send */
	buff[0] = PPS_JOB_CANCELLED;
	mesg = (struct qm_job_deleted *)(buff+1);
	strcpy(mesg->queue, queues[queue_index].name);
	mesg->count = id;
	mesg->time = time(NULL);
	memcpy(&mesg->inf, inf, sizeof(struct inf));
	/* */

	/* try to send messge                                         */
	/* - we don't really care if this message wasn't read because */
	/*   the queue manager stopped listening.  If it did, it did. */
	write(fd, buff, (1+sizeof(struct qm_job_deleted)));
	/* */

	/* close the named pipe (fifo) */
	close(fd);
	/* */
}



void notify_queue_manager_printed(int id, struct inf *inf)
{
	int fd;
	char path[256];
	char buff[1024];
	struct qm_job_printed *mesg;
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
			sprintf(buff, "ppd/queue/printer (%d): cannot open FIFO .qmpipe to send to queue manager\n", getpid());
			error_log(buff);
			exit(0);
		}
		/* */
	}
	/* */

	/* build the message to send */
	buff[0] = PPS_JOB_PRINTED;
	mesg = (struct qm_job_printed *)(buff+1);
	strcpy(mesg->queue, queues[queue_index].name);
	mesg->count = id;
	mesg->time = time(NULL);
	mesg->pagecount = rand() % 10;
	mesg->charge = mesg->pagecount * 0.10;
	mesg->balance = 10.00 - mesg->charge;
	memcpy(&mesg->inf, inf, sizeof(struct inf));
	/* */

	/* try to send messge                                         */
	/* - we don't really care if this message wasn't read because */
	/*   the queue manager stopped listening.  If it did, it did. */
	write(fd, buff, (1+sizeof(struct qm_job_printed)));
	/* */

	/* close the named pipe (fifo) */
	close(fd);
	/* */
}



void sigpipe_handler(int type)
{
	/* set flag indicating SIGPIPE occurred */
	broken_pipe = 1;
	/* */

	#ifdef DEBUG
	sprintf(dbg_str, " *** %s/%s (%d): caught SIGPIPE while writing to queue manager *** ",
		queues[queue_index].name, printers[printer_index].name, getpid());
	debug_log(dbg_str);
	#endif
}



int calculate_head()
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



int calculate_tail()
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



int readint(int fd, char *ptr, int maxlen)
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



void get_head_tail(int *fd, int *head, int *tail)
{
	char path[256];
	char buff[1024];
	int result;


	/* open the .lock file */
	sprintf(path, "%s/spool/%d/.lock", ROOT, queue_index);
	*fd  = open(path, O_CREAT | O_RDWR, S_IRWXU);
	/* */

	/* if we could neither open .lock, nor create it, give up */
	if (*fd == -1)
	{
		sprintf(buff, "ppd/queue (%d): cannot create or open .lock for queue %s", getpid(), queues[queue_index].name);
		error_log(buff);
		exit(0);
	}
	/* */

	/* .lock opened, try to lock it */
	result = wait_lock_file(*fd);
	/* */

	/* if we can't lock it, report error and exit */
	if (result == -1)
	{
		sprintf(buff, "ppd/queue (%d): cannot lock .lock for queue %s", getpid(), queues[queue_index].name);
		error_log(buff);
		exit(0);
	}
	/* */

	/* calculate a tail value by examining the queue's contents */
	*tail = calculate_tail();
	/* */

	/* check if queue is empty */
	if (*tail == -1)
	{
		exit(0);
	}
	/* */

	/* calculate head value... */
	/* we do know that it must exist, because we would already */
	/* have taken care of an empty queue situation and exited earlier */
	*head = calculate_head();

	/* if head still turns out to be incalculable... */
	if (*head == -1)
	{
		sprintf(buff, "ppd/queue (%d): cannot determine head value for apparentely non-empty queue %s", getpid(), queues[queue_index].name);
		error_log(buff);
		exit(0);
	}
	/* */
}



void rewrite_value(int fd, int value)
{
	char buff[1024];


	/* truncate file first */
	if (ftruncate(fd, 0) == -1)
	{
		sprintf(buff, "ppd/queue (%d): cannot truncate job counter file", getpid());
		error_log(buff);
		exit(0);
	}
	/* */

	/* seek back to beginning of file */
	if (lseek(fd, 0, SEEK_SET) == -1)
	{
		sprintf(buff, "ppd/queue (%d): cannot seek to beginning of job counter file", getpid());
		error_log(buff);
		exit(0);
	}
	/* */

	/* write new count value into file */
	sprintf(buff, "%d\n", value);
	if (!write(fd, buff, strlen(buff)))
	{
		sprintf(buff, "ppd/queue (%d): cannot write job counter value into file", getpid());
		error_log(buff);
		exit(0);
	}
	/* */
}



void rewrite_count_file(int value)
{
	int fd;
	char path[256];
	char buff[1024];


	/* open the .count file for writing, creating if necessary, and truncating */
	sprintf(path, "%s/spool/%d/.count", ROOT, queue_index);
	fd = open(path, O_CREAT | O_TRUNC | O_WRONLY, S_IRWXU);
	/* */

	/* write count value to file if file opened successfully */
	if (fd != -1)
	{
		sprintf(buff, "%d\n", value);
		if (!write(fd, buff, strlen(buff)))
		{
			sprintf(buff, "ppd/queue (%d): cannot write to %s\n", getpid(), path);
			error_log(buff);
			exit(0);
		}
	}
	/* */

	/* close .count file */
	close(fd);
	/* */
}



void read_database()
{
	int sock;
	m_result *result, *specific_result;
	m_row row, specific_row;
	int num, x;
	char string[1024], buff[1024];
	int keyval;


	/* start out with both arrays empty */
	for (x=0; x<MAX_QUEUES; x++)
		queues[x].type = 0;
	for (x=0; x<MAX_PRINTERS; x++)
		printers[x].type = 0;
	/* */

	/* connect to MSQL */
	sock = mconnect();
	if (sock == -1)
	{
		sprintf(buff, "ppd (%d): unable to connect to Mini SQL to read database", getpid());
		error_log(buff);
		exit(0);
	}
	/* */

	/* get all of the queue data */
	result = send_query(sock, "SELECT type,charge,name,description,status,keyval FROM queues");
	num = msqlNumRows(result);
	/* */

	/* read the retreived data into the global queue array */
	for(x=0; x<num; x++)
	{
		/* pull the next row of information and store it */
		row = fetch_result(result);

		sscanf(row[5], "%d", &keyval);
		sscanf(row[0], "%d", &queues[keyval].type);
		sscanf(row[1], "%f", &queues[keyval].charge);
		strcpy(queues[keyval].name, row[2]);
		strcpy(queues[keyval].description, row[3]);
		sscanf(row[4], "%d", &queues[keyval].status);
		/* */

		#ifdef DEBUG
		sprintf(dbg_str, "Inserted queue:\nkeyval = %d, type = %d, charge = %0.2f, name = '%s', description = '%s', status = %d\n",
			keyval, queues[keyval].type, queues[keyval].charge, queues[keyval].name, queues[keyval].description, queues[keyval].status);
		debug_log(dbg_str);
		#endif
	}
	/* */

	end_query(result);

	/* get all of the printer data */
	result = send_query(sock, "SELECT type,name,description,queue,status,keyval FROM printers");
	num = msqlNumRows(result);
	/* */

	/* read the retreived data into the global printer array */
	for(x=0; x<num; x++)
	{
		/* pull the next row of information and store it */
		row = fetch_result(result);

		sscanf(row[5], "%d", &keyval);
		sscanf(row[0], "%d", &printers[keyval].type);
		strcpy(printers[keyval].name, row[1]);
		strcpy(printers[keyval].description, row[2]);
		sscanf(row[3], "%d", &printers[keyval].queue);
		sscanf(row[4], "%d", &printers[keyval].status);

		switch(printers[keyval].type)	/* read the proper address information in */
		{
			case PPS_PRINTER_PJL_JETDIRECT:
				sprintf(string, "SELECT address,port,keyval FROM pjl_jetdirect WHERE keyval=%d", keyval);
				specific_result = send_query(sock, string);
				specific_row = fetch_result(specific_result);
				strcpy(printers[keyval].specific.pjl_jetdirect.address, specific_row[0]);
				sscanf(specific_row[1], "%d", &printers[keyval].specific.pjl_jetdirect.port);

				#ifdef DEBUG
				sprintf(dbg_str, "Inserted printer:\nkeyval = %d, type = %d, name = '%s', description = '%s', address = '%s', port = %d, queue = %d, status = %d\n",
					keyval, printers[keyval].type, printers[keyval].name, printers[keyval].description, 
					printers[keyval].specific.pjl_jetdirect.address, printers[keyval].specific.pjl_jetdirect.port, printers[keyval].queue, printers[keyval].status);
				debug_log(dbg_str);
				#endif

				break;
		}
		/* */
	}
	/* */

	end_query(result);

	mclose(sock);
}



int charge_account(char *usercode, float charge, float *new_balance)
{
	int fd_result, db_fd;
	char path[256];
	int sock;
	m_result *db_result;
	m_row row;
	char string[1024], buff[1024];
	float original_balance;
	int account_status;


	/*** attempt to lock .dbaccess file in root directory ***/

	/* first open the file if we can */
	sprintf(path, "%s/.dbaccess", ROOT);
	db_fd = open(path, O_CREAT | O_RDWR, S_IRWXU);
	/* */

	if (db_fd == -1)
	{
		/* can't open or create .dbaccess file, that's not good... */
		/* we'd better report this, but we ought to still charge this user anyway, as the */
		/* odds of multiple balance updates on the same user will be extremely tiny under */
		/* normal circumstances, and we don't want to let everyone just print for free! */
		sprintf(buff, "ppd/queue (%d): cannot open or create .dbaccess file", getpid());
		error_log(buff);
		/* */
	}

	/* try to lock .dbaccess file if we can */
	if (db_fd != -1)
	{
		fd_result = wait_lock_file(db_fd);

		if (fd_result == -1)
		{
			/* we'd better report the failure to lock this file, although as above, we still */
			/* want to go ahead with the balance update anyway, for the same reason */
			error_log("ppd: cannot get a file lock on .dbaccess");
			/* */
		}
	}
	/* */

	/*** do the balance update on this user ***/

	/* connect to msql server */
	sock = mconnect();
	if (sock == -1)
	{
		sprintf(buff, "ppd/queue/printer (%d): cannot connect to mSQL server to update user balance", getpid());
		error_log(buff);
		return -1;
	}
	/* */

	/* try to get the user's current record */
	sprintf(string, "SELECT usercode,status,balance FROM users WHERE usercode='%s'", usercode);
	db_result = send_query(sock, string);
	if (msqlNumRows(db_result) == 0)
	{
		mclose(sock);
		close(db_fd);

		sprintf(buff, "ppd/queue/printer (%d): account update requested for invalid usercode '%s'", getpid(), usercode);
		error_log(buff);
		return -1;
	}
	/* */

	/* calculate the new balance */
	row = fetch_result(db_result);

	sscanf(row[1], "%d", &account_status);
	sscanf(row[2], "%f", &original_balance);

	if ((account_status == PPS_ACCOUNTING_ACTIVE) && (charge > 0.0))
		*new_balance = original_balance - charge;
	else
	{
		/* this user has unlimited printing credit, or there is no charge, so don't charge */
		*new_balance = original_balance;

		mclose(sock);
		close(db_fd);

		return 0;
		/* */
	}
	/* */

	/* update the user's balance */
	sprintf(string, "UPDATE users SET balance=%0.2f WHERE usercode='%s'", *new_balance, usercode);
	db_result = send_query(sock, string);

	if (msql_err)
	{
		sprintf(buff, "ppd/queue/printer (%d): error updating the balance of user '%s'", getpid(), usercode);
		error_log(buff);

		mclose(sock);
		close(db_fd);
		return -1;
	}

	mclose(sock);
	close(db_fd);
	/* */

	return 0;
}



void notify_user_of_charge(char *usercode, char *queuename, int queue_type, int units, float charge, float balance, int result)
{
	#ifdef USE_SAMBA_SMBCLIENT
	char string[1024];

	#endif

	return;
}



int check_print_balance(char *usercode, int queue_index)
{
	int sock;
	m_result *db_result;
	m_row row;
	char string[1024], buff[1024];
	int account_status;
	float account_balance;


	/* check first if there is no charge to print to this printer */
	if (queues[queue_index].type == PPS_CHARGE_NONE)
	{
		return PPS_OK;
	}
	/* */

	/* connect to the msql server */
	sock = mconnect();
	if (sock == -1)
	{
		sprintf(buff, "ppd/queue/printer (%d): cannot connect to mSQL server to check user balance", getpid());
		error_log(buff);
		return -1;
	}
	/* */

	/* try to get the user's account status */
	sprintf(string, "SELECT usercode,status,balance FROM users WHERE usercode='%s'", usercode);
	db_result = send_query(sock, string);
	if (msqlNumRows(db_result) == 0)
	{
		mclose(sock);

		sprintf(buff, "ppd/queue/printer (%d): balance check requested for invalid usercode '%s'", getpid(), usercode);
		error_log(buff);
		return -1;
	}

	row = fetch_result(db_result);
	sscanf(row[1], "%d", &account_status);
	sscanf(row[2], "%f", &account_balance);
	/* */

	/* close connection to msql server */
	mclose(sock);
	/* */

	/* check whether this user has unlimited account priveledges */
	if (account_status == PPS_ACCOUNTING_INACTIVE)
		return PPS_OK;
	else
	{
		if (account_balance > 0.0)
			return PPS_OK;
		else
			return PPS_LOW_BALANCE;
	}
	/* */
}



int calculate_charge(char *usercode, int queue_index, int units, float *charge)
{
	int sock;
	m_result *db_result;
	m_row row;
	char string[1024], buff[1024];
	int account_status;


	/* connect to the msql server */
	sock = mconnect();
	if (sock == -1)
	{
		sprintf(buff, "ppd/queue/printer (%d): cannot connect to mSQL server to calculate charge for job", getpid());
		error_log(buff);
		return ERROR;
	}
	/* */

	/* try to get the user's account status */
	sprintf(string, "SELECT usercode,status FROM users WHERE usercode='%s'", usercode);
	db_result = send_query(sock, string);
	if (msqlNumRows(db_result) == 0)
	{
		mclose(sock);

		sprintf(buff, "ppd/queue/printer (%d): job charge calculation requested for invalid usercode '%s'", getpid(), usercode);
		error_log(buff);
		return ERROR;
	}

	row = fetch_result(db_result);
	sscanf(row[1], "%d", &account_status);
	/* */

	/* close connection to msql server */
	mclose(sock);
	/* */

	/* calculate the charge for this job */
	if (account_status == PPS_ACCOUNTING_INACTIVE)
		*charge = 0.0;
	else
	{
		switch(queues[queue_index].type)
		{
			case PPS_CHARGE_NONE:
				*charge = 0.0;
				break;
			case PPS_CHARGE_PAGE:
				*charge = units * queues[queue_index].charge;
				break;
			case PPS_CHARGE_JOB:
				*charge = queues[queue_index].charge;
				break;
		}
		/* */
	}

	return OK;
}

