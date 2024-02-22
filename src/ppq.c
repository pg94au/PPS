/*
 *  ppq.c
 *
 *  A BSD lpq-like program which allows for the checking of the status of a particular queue in the system.
 *
 *  The general usage of this program is as follows:
 *
 *  Switches:	-[qP]<queue>	:  Specify the name of the queue to examine (required)
 *		-p<printer>	:  Specify the single printer to examine in the specified queue (optional)
 *		-u<username>	:  Specify the single user to examine in the specified queue/printer (optional)
 *		-v		:  Set verbose output on (optional)
 *		-V		:  Show only queue descriptions and charges (optional)
 *		-b		:  BSD lpq backward compatibility mode
 *
 *  By default, the first unswitched argument is taken to be the queue name.  Thus, the -P or -q switch can be
 *  left off, making "ppq queuename" equivalent to "ppq -Pqueuename".  Each switch can only be specified once.
 *
 *  (c)1998 Paul Grebenc
 *
 *  $Header: /usr/local/PPS_RCS/src/ppq.c,v 1.1 1999/12/10 03:34:16 paul Exp $
 */


#include <config.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <pwd.h>
#include <unistd.h>
#include <errno.h>

#include "pps.h"
#include "locking.h"
#include "database.h"


int main(int argc, char *argv[]);
void ppq_output(int argc, char *argv[]);
int parse_ppq_arguments(int argc, char *argv[], int *queue_idx, struct pps_queue *queue, int *numprinters, int *printer_idx, struct printer printers[], int *userid, int *verbosity);
void bsd_output(int argc, char *argv[]);
int parse_bsd_arguments(int argc, char *argv[], int *queue_idx, struct pps_queue *queue);
int lookup_queue(char *name, struct pps_queue *queue);
int read_printers(int queue_idx, struct printer printers[]);
int lookup_printer(char *printername, int numprinters, struct printer printers[]);
int lookup_user(char *username);
char *get_username(int userid);
void ppq_show_all_queues(void);
void ppq_show_description(struct pps_queue *queue, int numprinters, struct printer printers[]);
int ppq_show_queue_status(int queue_idx, struct pps_queue *queue);
void ppq_show_printer_status(int numprinters, int printer_idx, struct printer printers[], int queue_idx, int userid, int printing_jobs[], int verbosity);
void ppq_show_waiting_jobs(int numprinters, struct printer printers[], int queue_idx, int userid, int printing_jobs[], int verbosity);
int bsd_show_queue_status(int queue_idx, struct pps_queue *queue, int numprinters, struct printer printers[]);

int calculate_head(int queue_index);
int calculate_tail(int queue_index);
void get_head_tail(int queue_index, int *head, int *tail);


int main(int argc, char *argv[])
{
	int i;
	int bsd = 0;


	/* search for a BSD lpq backward compatibility switch */
	for (i=1; i<argc; i++)
	{
		/* all valid arguments start with a dash */
		if (argv[i][0] == '-')
		{
			if (argv[i][1] == 'b')
			{
				if (bsd == 0)
					bsd = 1;
				else
				{
					fprintf(stderr, "ppq: multiple backward compatibility specification not allowed\n");
					exit(0);
				}
			}
		}
		/* */
	}
	/* */

	/* produce output according to the mode specified */
	if (bsd == 0)
		ppq_output(argc, argv);
	else
		bsd_output(argc, argv);
	/* */
}



/* ppq_output
**
** Produce normal ppq output for the user.  The default mode.
**
** Arguments:
**	argc	the argument count, as passed to main()
**	argv	the argument strings, as passed to main()
*/
void ppq_output(int argc, char *argv[])
{
	struct pps_queue queue;
	struct printer printers[MAX_PRINTERS_PER_QUEUE];
	int queue_idx, printer_idx, userid, numprinters;
	int printing_jobs[MAX_PRINTERS_PER_QUEUE];
	int verbosity = 0;	/* default, non-verbose */
	int result;


	/* parse our command line arguments */
	if (parse_ppq_arguments(argc, argv, &queue_idx, &queue, &numprinters, &printer_idx, printers, &userid, &verbosity) == -1)
		exit(0);
	/* */

	/* show all queues if requested */
	if (verbosity == 3)
	{
		ppq_show_all_queues();

		exit(0);
	}
	/* */

	/* show only queue/printer details if so requested */
	if (verbosity == 2)
	{
		ppq_show_description(&queue, numprinters, printers);

		exit(0);
	}
	/* */

	/* show queue line */
	result = ppq_show_queue_status(queue_idx, &queue);
	/* */

	/* show what printers are doing */
	if ((result ==  ACTIVE) || (result == UP) || (verbosity == 1))
	{
		ppq_show_printer_status(numprinters, printer_idx, printers, queue_idx, userid, printing_jobs, verbosity);
	}
	/* */

	/* show waiting jobs, only if we did not want to only look at what a specific printer was doing */
	if (printer_idx == -1)
	{
		ppq_show_waiting_jobs(numprinters, printers, queue_idx, userid, printing_jobs, verbosity);
	}
	/* */

	exit(0);
}



/* parse_ppq_arguments
**
** Parse out command line arguments given to this program.  The general syntax is listed at the top of this file.
**
** Arguments:
**	argc		the argument count as passed to main()
**	argv		the argument strings as passed to main()
**	queue_idx	returns the index number of the selected queue, parsed from the command line
**	numprinters	returns the number of printers attached to the selected queue
**	printer_idx	returns the index number of the selected printer if specified, parsed from the command line,
**				or -1 if no single printer was specified
**	printers	array structure returning all of the printers attached to the specified queue
**	userid		returns the index number of the selected user if specified, parsed from the command line,
**				or -1 if no single user was specified
**	verbosity	passes the current verbosity level (defaults to off, or 0), which is modified to a verbose
**				level if specified on the command line (value 1)
**
** Returned value:
**	Success:  0 (command line parsed successfully)
**	Failure:  -1 (currently never returned, as all errors exit, but would signify a non-critical error
**			parsing command line
**
** Exit conditions:
**	On multiple queue, printer or user specification, reporting error to user.
**	On illegal/invalid option or switch, reporting error to user.
**	On failure to specify a queue, reporting error to user.
**	On failure to find specified queue, printer or user, reporting error to user.
*/
int parse_ppq_arguments(int argc, char *argv[], int *queue_idx, struct pps_queue *queue, int *numprinters, int *printer_idx, struct printer printers[], int *userid, int *verbosity)
{
	int i;
	char queuename[33], printername[33], username[9];


	/* initialize */
	queuename[0] = '\0';
	printername[0] = '\0';
	username[0] = '\0';
	*verbosity = 0;
	/* */

	/* go through each argument, parsing it out */
	for (i=1; i<argc; i++)
	{
		/* all valid arguments start with a dash */
		if (argv[i][0] == '-')
		{
			switch(argv[i][1])
			{
				case 'q':
				case 'P':	/* for lpr compatibility */
					if (queuename[0] == '\0')
						strncpy(queuename, &argv[i][2], 33);
					else
					{
						fprintf(stderr, "ppq: multiple queue specification not allowed\n");
						exit(0);
					}
					break;
				case 'p':
					if (printername[0] == '\0')
					{
						if (*verbosity == 2)
						{
							fprintf(stderr, "ppq: cannot specify a printer at this verbosity level\n");
							exit(0);
						}
						strncpy(printername, &argv[i][2], 33);
					}
					else
					{
						fprintf(stderr, "ppq: multiple printer specification not allowed\n");
						exit(0);
					}
					break;
				case 'u':
					if (username[0] == '\0')
					{
						if (*verbosity == 2)
						{
							fprintf(stderr, "ppq: cannot specify a user at this verbosity level\n");
							exit(0);
						}
						strncpy(username, &argv[i][2], 9);
					}
					else
					{
						fprintf(stderr, "ppq: multiple user specification not allowed\n");
						exit(0);
					}
					break;
				case 'v':
					if (strlen(argv[i]) == 2)
					{
						if (*verbosity == 0)
						{
							/* only if the -v switch is alone do we set verbosity, */
							/* else it is interpreted as some illegal switch */
							*verbosity = 1;
							/* */
						}
						else
						{
							fprintf(stderr, "ppq: multiple verbosity specification not allowed\n");
							exit(0);
						}
					}
					else
					{
						fprintf(stderr, "lpq: illegal option \"%s\"\n", argv[i]);
						exit(0);
					}
					break;
				case 'V':
					if (strlen(argv[i]) == 2)
					{
						if (*verbosity == 0)
						{
							/* only if the -v switch is alone do we set verbosity, */
							/* else it is interpreted as some illegal switch */
							*verbosity = 2;
							/* */
						}
						else
						{
							fprintf(stderr, "ppq: multiple verbosity specification not allowed\n");
							exit(0);
						}
					}
					else
					{
						fprintf(stderr, "lpq: illegal option \"%s\"\n", argv[i]);
						exit(0);
					}
					break;
				default:
					fprintf(stderr, "ppq: unrecognized switch \"-%c\"\n", argv[i][1]);
					exit(0);
					
					break;
			}
		}
		else
		{
			/* use the first unswitched argument as the queue name */
			if (queuename[0] == '\0')
				strncpy(queuename, argv[i], 33);
			else
			{
				fprintf(stderr, "ppq: unrecognized command \"%s\"\n", argv[i]);

				exit(0);
			}
			/* */
		}
		/* */
	}
	/* */

	/* we need the name of a queue to do anything, except a show description */
	if (queuename[0] == '\0')
	{
		/* if we do a super verbose on no specific queue, we get a list of all queues */
		if ((*verbosity == 2) && (argc == 2))
		{
			*verbosity = 3;

			return 0;
		}
		/* */

		/* cannot leave out queue specification under normal circumstances */
		fprintf(stderr, "ppq: must specify which queue to examine\n");
		exit(0);
		/* */
	}
	/* */

	/* get the index values for arguments */
	if ((*queue_idx = lookup_queue(queuename, queue)) == -1)
	{
		fprintf(stderr, "ppq: queue [%s] does not exist\n", queuename);
		exit(0);
	}

	*numprinters = read_printers(*queue_idx, printers);

	if (printername[0] != '\0')
	{
		if ((*printer_idx = lookup_printer(printername, *numprinters, printers)) == -1)
		{
			fprintf(stderr, "ppq: printer [%s] does not exist\n", printername);
			exit(0);
		}
	}
	else
	{
		*printer_idx = -1;
	}
	if (username[0] != '\0')
	{
		if ((*userid = lookup_user(username)) == -1)
		{
			fprintf(stderr, "ppq: user [%s] does not exist\n", username);
			exit(0);
		}
	}
	else
	{
		*userid = -1;
	}
	/* */

	return 0;
}



/* bsd_output
**
** Produce BSD lpq-style output for the user.  This mode is for backward compatibility only.
**
** Arguments:
**	argc	the argument count, as passed to main()
**	argv	the argument strings, as passed to main()
*/
void bsd_output(int argc, char *argv[])
{
	struct pps_queue queue;
	struct printer printers[MAX_PRINTERS_PER_QUEUE];
	int numprinters;
	int queue_idx;


	/* parse our command line arguments */
	if (parse_bsd_arguments(argc, argv, &queue_idx, &queue) == -1)
		exit(0);
	/* */

	numprinters = read_printers(queue_idx, printers);

	bsd_show_queue_status(queue_idx, &queue, numprinters, printers);

	exit(0);
}



/* parse_bsd_arguments
**
** Parse out BSD-compatibility mode command line arguments given to this program.
**
** Arguments:
**	argc		the argument count as passed to main()
**	argv		the argument strings as passed to main()
**	queue_idx	returns the index number of the selected queue, parsed from the command line
**	queue		returns the configuration for the specified queue
**
** Returned value:
**	Success:  0 (command line parsed successfully)
**	Failure:  -1 (currently never returned, as all errors exit, but would signify a non-critical error
**			parsing command line
**
** Exit conditions:
**	On illegal/invalid option or switch, reporting error to user.
**	On failure to find specified queue, reporting error to user.
*/
int parse_bsd_arguments(int argc, char *argv[], int *queue_idx, struct pps_queue *queue)
{
	int i;
	char queuename[33];


	/* initialize */
	strcpy(queuename, "lp");
	/* */

	/* go through each argument, parsing it out */
	for (i=1; i<argc; i++)
	{
		/* all valid switches start with a dash */
		if (argv[i][0] == '-')
		{
			switch(argv[i][1])
			{
				case 'P':
					/* because at least Linux's lpq allows multiple -P switches but only */
					/* uses the last one, we'll just do the same thing here */
					strncpy(queuename, &argv[i][2], 33);
					break;
				case 'b':
					/* ignore the backward compatibility switch now */
					break;
				default:
					fprintf(stderr, "ppq: illegal option -- %c\n", argv[i][1]);
					exit(0);
			}
		}
		else
		{
			/* right now we're ignoring all of the other options... */
			/* How important is it that we deal with the others anyway? */
		}
		/* */
	}
	/* */

	/* get the goods on the queue that was specified */
	if ((*queue_idx = lookup_queue(queuename, queue)) == -1)
	{
		fprintf(stderr, "lpq: %s: unknown printer\n", queuename);
		exit(0);
	}
	/* */

	return 0;
}



/* lookup_queue
**
** Given the name of a queue in the system, returns a queue structure containing relevant information from the
** database for this queue.
**
** Arguments:
**	name	passes the name of the queue to return information about
**	queue	a structure into which the contents of the database record for this queue will be returned
**
** Returned value:
**	Success:  The key value of the queue specified
**	Failure:  -1, indicating that the specified queue could not be found in the database
**
** Exit conditions:
**	On failure to communicate with mSQL server, reporting error to user.
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
		fprintf(stderr, "ppq: error communicating with Mini SQL to lookup queue name.\n");
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



/* read_printers
**
** Given the index of a queue in the system database, return the contents of the records of all printers which
** are attached to this queue.
**
** Arguments:
**	queue_idx	the index value of the queue for which we wish to have printer information
**	printers	array of printer structures into which all database records for printers which are attached
**				to the specified queue will be copied
**
** Returned value:
**	The number of printers which are attached to the queue (the number of printer records copied)
**
** Exit conditions:
**	On failure to connect to mSQL database, reporting error to the user.
**
** Notes:  See note within code.
*/
int read_printers(int queue_idx, struct printer printers[])
{
	int sock;
	m_result *result;
	m_row row;
	int numprinters, x;
	char string[1024];


	/* connect to MSQL */
	sock = mconnect();
	if (sock == -1)
	{
		fprintf(stderr, "ppq: error communicating with Mini SQL to lookup printer name.\n");
		exit(0);
	}
	/* */

	/* get the data for the printer which was specified by name */
	sprintf(string, "SELECT type,name,description,status,queue,keyval FROM printers WHERE queue=%d", queue_idx);
	result = send_query(sock, string);
	/* */

	/* get the number of printers attatched to this queue */
	numprinters = msqlNumRows(result);
	/* */

	/* read each printer into an array element */
	for (x=0; x < numprinters; x++)
	{
		/* printer exists, so copy over all printer information */
		row = fetch_result(result);

		sscanf(row[0], "%d", &printers[x].type);
		strcpy(printers[x].name, row[1]);
		strcpy(printers[x].description, row[2]);
		sscanf(row[3], "%d", &printers[x].status);
		/* *** NOTE *** */
		/* Oh, this is horrible... We know the queue value for all of these printers, since we select only the ones */
		/* attatched to a particular queue, but we need the key values for these printers, since they aren't simply */
		/* zero to seven (their indices in this array of printer structures).  So, what are we going to do?  We are */
		/* going to temporarily use the queue index value of the printer structure to store the printer index instead. */
		/* We'll try not to let this type of thing become a habit... */
		sscanf(row[5], "%d", &printers[x].queue);
		/* */
	}
	/* */

	return numprinters;
}



/* lookup_printer
**
** Find a printer in our array of printer records by the name specified.
**
** Arguments:
**	printername	the name of the printer to search for
**	numprinters	the number of valid printer records in our array
**	printers	the array of printer records
**
** Returned value:
**	Success:  The index value of the record within the printers array which corresponds to the one
**			specified by printername
**	Failure:  -1 (the printer specified does not exist or is not attached to this queue)
*/
int lookup_printer(char *printername, int numprinters, struct printer printers[])
{
	int x;


	/* search through the printers attatched to this queue for the one named */
	for (x=0; x < numprinters; x++)
	{
		if (strncmp(printername, printers[x].name, 33) == 0)
		{
			return x;
		}
	}
	/* */

	/* printer not found */
	return -1;
	/* */
}



/* lookup_user
**
** Find a user on the system whose username corresponds to the one specified.
**
** Arguments:
**	username	the login name of the user to search for
**
** Returned value:
**	Success:  The userid value of the user specified
**	Failure:  -1 (specified user does not exist on the system)
*/
int lookup_user(char *username)
{
	struct passwd *passwd;


	/* try to get the password information for this username */
	passwd = getpwnam(username);
	if (passwd == NULL)
	{
		return -1;
	}
	/* */

	/* get the userid from the password information */
	/* NOTE:  should probably go through entire source and change all occurences of userid value */
	/*        to use uid_t as the data type, rather than (safely enough, of course) assuming int */
	return passwd->pw_uid;
	/* */
}



/* get_username
**
** Obtain the name of a user on the system, given the user's id value.
**
** Arguments:
**	userid	the user's numerical userid on the system
**
** Returned value:
**	A string containing the login name associated with the given userid, or NULL if the user does not exist.
*/
char *get_username(int userid)
{
	struct passwd *pwd;
	static char username[9];


	/* try to get the password information for this userid */
	pwd = getpwuid(userid);
	if (pwd == NULL)
	{
		return (char *)NULL;
	}
	else
	{
		strcpy(username, pwd->pw_name);

		return username;
	}
	/* */
}



/* ppq_show_all_queues
**
** Show all queues, their descriptions and their print charges in the system
**
** Exit conditions:
**	On error communicating with the MSQL server, reporting to user.
*/
void ppq_show_all_queues()
{
	int sock;
	m_result *result;
	m_row row;
	int numqueues, x, type;
	float charge;


	/* connect to mSQL */
	sock = mconnect();
	if (sock == -1)
	{
		fprintf(stderr, "ppq: error communicating with Mini SQL to lookup queue names.\n");
		exit(0);
	}
	/* */

	/* get the data for all queues */
	result = send_query(sock, "SELECT type,charge,name,description,status,keyval FROM queues ORDER BY name");
	/* */

	/* see if there are any queues in the system */
	numqueues = msqlNumRows(result);

	if (result == 0)
	{
		fprintf(stderr, "\nNO QUEUES PRESENT IN SYSTEM\n");
		return;
	}
	/* */

	/* print all queue information out */
	printf("\n");
	for (x=0; x < numqueues; x++)
	{
		row = fetch_result(result);

		printf("QUEUE [%s]\n", row[2]);
		printf("Description:  %s\n", row[3]);
		sscanf(row[0], "%d", &type);
		sscanf(row[1], "%f", &charge);
		switch(type)
		{
			case 1:	/* no charge */
				printf("     Charge:  None\n");
				break;
			case 2:	/* per page */
				printf("     Charge:  $%0.2f per page\n", charge);
				break;
			case 3:	/* per job */
				printf("     Charge:  $%0.2f per job\n", charge);
				break;
			default:
				printf("     Unknown print charge type\n");
				break;
		}
		printf("\n");
	}
	/* */
}



/* ppq_show_description
**
** Show a detailed description of the queue and printer settings contained in the database.
**
** Arguments:
**	queue		queue structure containing its database record entry
**	numprinters	number of printers attached to this queue
**	printers	array of printer structures containing their database record entries
*/
void ppq_show_description(struct pps_queue *queue, int numprinters, struct printer printers[])
{
	int x;


	/* show information about the queue */
	printf("\nQUEUE [%s] is ", queue->name);
	if (queue->status == UP)
		printf("UP\n");
	else
		printf("DOWN\n");
	printf("Description:  %s\n", queue->description);
	switch(queue->type)
	{
		case 1:	/* no charge */
			printf("     Charge:  None\n");
			break;
		case 2:	/* per page */
			printf("     Charge:  $%0.2f per page\n", queue->charge);
			break;
		case 3:	/* per job */
			printf("     Charge:  $%0.2f per job\n", queue->charge);
			break;
		default:
			printf("     Unknown print charge type\n");
			break;
	}
	/* */

	/* show information about each printer */
	for (x=0; x < numprinters; x++)
	{
		printf("\nPRINTER [%s] is ", printers[x].name);
		if (printers[x].status == UP)
			printf("UP\n");
		else
			printf("DOWN\n");
		printf("Description:  %s\n", printers[x].description);
	}
	/* */

	printf("\n");	/* neatness counts! */
}



/* ppq_show_queue_status
**
** Display the current status of the specified queue to the user.
**
** Arguments:
**	queue_idx	the index value of the queue to be reported on.
**	queue		the record containing information pertaining to the specified queue.
**
** Returned value:
**	The current activity status of the queue specified.
*/
int ppq_show_queue_status(int queue_idx, struct pps_queue *queue)
{
	FILE *fp;
	char path[256];
	pid_t pid;


	/* let's show what information we can about this queue's current status */
	if (queue->status == UP)
	{
		/* try to find out if the queue is currently active */
		sprintf(path, "%s/spool/%d/.dpid", ROOT, queue_idx);
		fp = fopen(path, "r");

		if (fp != NULL)
		{
			/* read queue daemon's pid */
			fscanf(fp, "%d", (int *)&pid);
			fclose(fp);
			/* */

			/* check to see if this process is running by sending it a null signal */
			if (kill(pid, 0) == 0)
			{
				/* queue is running */
				printf("\nQUEUE   [%s] is UP and is ACTIVE\n", queue->name);

				return ACTIVE;
				/* */
			}
			else
			{
				if (errno == ESRCH)
				{
					/* queue is not running */
					printf("\nQUEUE   [%s] is UP but INACTIVE\n", queue->name);

					return INACTIVE;
					/* */
				}
				else
				{
					/* who knows where the queue daemon is? */
					printf("\nQUEUE   [%s] is UP, activity undetermined\n", queue->name);

					return UP;
					/* */
				}
			}
			
		}
		else
		{
			/* if there is no .dpid file, queue daemon can't be running unless it was manually deleted */
			printf("\nQUEUE   [%s] is UP, but INACTIVE\n", queue->name);

			return INACTIVE;
			/* */
		}
		/* */
	}
	else
	{
		printf("QUEUE   [%s] is DOWN\n", queue->name);

		return DOWN;
	}
	/* */
}



/* ppq_show_printer_status
**
** Report current printer status to the user.
**
** Arguments:
**	numprinters	the number of printers attached the the queue being examined
**	printer_idx	if specified (not -1), the single printer to be considered
**	printers	array containing pertinent information for all printers attached to specified queue
**	queue_idx	index value of queue for which printers are to be reported on
**	userid		if specified (not -1), the single user to be considered
**	printing_jobs	array into which the currently printing job of the corresponding entry in the printers
**				array is to be stored, if active.  Else, -1 is stored into the corressponding
**				element of each inactive printer.
**	verbosity	the output verbosity level
**
** Exit conditions:
**	On the inability to test the status of any printer by checking its lock file, reporting error to user.
**	On any error opening or reading the status or inf files of any printer examined, reporting error to user.
**	On recovery of an illegal status value from any printer examined, reporting error to user.
**	On the inability to check the size of any data file examined, reporting error to user.
**
** Notes:
**	If a specific printer to examine was given, only that printer's status will be reported.
**	If a specific userid was given, only printers currently printing jobs for that user will be reported.
**	If the verbose level is 1, also report printers which are currently down.
*/
void ppq_show_printer_status(int numprinters, int printer_idx, struct printer printers[], int queue_idx, int userid, int printing_jobs[], int verbosity)
{
	FILE *fp;
	char path[256];
	int lock_status;
	struct printer_status printer_status;
	struct inf inf;
	struct stat filestat;
	int shown = 0;
	int x;


	/* show all printers, or just the one requested if that be the case */
	for (x=0; x < numprinters; x++)
	{
		/* show all printers if no specific one was asked for, or only the one that */
		/* was asked for if a specific one was requested */
		if ((printer_idx == -1) || (printer_idx == x))
		{
			/* remember, the queue element of the printers structure contains that particular printer's key value, not the */
			/* key value of its queue! */
			sprintf(path, "%s/spool/%d/.%d", ROOT, queue_idx, printers[x].queue);

			/* check to see if the printer is active, in which case it's status file will be locked */
			if (open_and_test_lock(path, &lock_status) == -1)
			{
				if (errno != ENOENT)
				{
					/* if an error occurred, inform user and quit... but not if it's because the file doesn't exist. */
					/* If a new printer is created and it is examined here before it has ever been used, it does exist, */
					/* but there won't be a status file yet created */
					fprintf(stderr, "ppq: cannot test for write lock by printer [%s]\n", printers[x].name);
					exit(0);
					/* */
				}
				else
				{
					/* if the only reason our check failed is because the file doesn't exist, then we'll just make */
					/* it look as though the file does, but it is unlocked, because it means the same thing */
					lock_status = F_UNLCK;
					/* */
				}
			}
			/* */

			/* if this file is locked, we know the printer is doing something */
			if (lock_status == F_WRLCK)
			{
				/* open the status file for this printer */
				/* remember, we're using the queue element for the printer's key value */
				sprintf(path, "%s/spool/%d/.%d", ROOT, queue_idx, printers[x].queue);
				fp = fopen(path, "r");
				if (fp == NULL)
				{
					fprintf(stderr, "ppq: error opening status file of printer [%s]\n", printers[x].name);
					exit(0);
				}
				if (fread(&printer_status, sizeof(struct printer_status), 1, fp) != 1)
				{
					fprintf(stderr, "ppq: error reading status file of printer [%s]\n", printers[x].name);
					exit(0);
				}
				fclose(fp);
				/* */

				/* open the job's inf file and get information about the job */
				sprintf(path, "%s/spool/%d/inf%d", ROOT, queue_idx, printer_status.jobnum);
				fp = fopen(path, "r");
				if (fp == NULL)
				{
					fprintf(stderr, "ppq: error opening inf file of job #%d\n", printer_status.jobnum);
					exit(0);
				}
				if (fread(&inf, sizeof(struct inf), 1, fp) != 1)
				{
					fprintf(stderr, "ppq: error reading inf file of job #d\n", printer_status.jobnum);
					exit(0);
				}
				fclose(fp);
				/* */

				/* store the number of the job that is printing to this printer */
				printing_jobs[x] = printer_status.jobnum;
				/* */

				/* do not show this printer's status if only a particular user's jobs were */
				/* requested, and one is not printing that user's job */
				if ((userid == -1) || (inf.userid == userid))
				{
					/* pretty output - space before first printer shown */
					if (shown == 0)
					{
						shown = 1;
						printf("\n");
					}
					/* */

					/* display printer status */
					printf("PRINTER [%s] ", printers[x].name);
					switch(printer_status.status)
					{
						case PPS_STARTING:
							printf("is preparing to print\n");
							break;
						case PPS_CONNECTING:
							printf("- connecting\n");
							break;
						case PPS_CONNECTION_REFUSED:
							printf("- connection refused\n");
							break;
						case PPS_SENDING_JOB:
							printf("- sending job\n");
							break;
						case PPS_PRINTING:
							printf("is printing\n");
							break;
						case PPS_PAPER_JAM:
							printf("has a paper jam\n");
							break;
						case PPS_NO_PAPER:
							printf("is out of paper\n");
							break;
						case PPS_ONLINE:
							printf("is online\n");
							break;
						case PPS_OFFLINE:
							printf("is offline\n");
							break;
						case PPS_RESETTING_PRINTER:
							printf("is being reset\n");
							break;
						default:
							fprintf(stderr, "ppq: printer [%s] has an illegal status value\n", printers[x].name);
							exit(0);
					}
					/* */

					/* display job information */
					errno = 0;
					sprintf(path, "%s/spool/%d/dat%d", ROOT, queue_idx, printer_status.jobnum);
					if ((stat(path, &filestat) == -1) && (errno != ENOENT))
					{
							fprintf(stderr, "ppq: unable to gets stats on existing data file for job %d\n", printer_status.jobnum);
							exit(0);
					}
					printf("  %-8s %-3s %-48s %s\n", "Owner", "Job", "Filename", "Size");
					if (errno == ENOENT)
					{
						printf("  %-8s %3d %-48s -\n", get_username(inf.userid), printer_status.jobnum, inf.filename);
					}
					else
					{
						printf("  %-8s %3d %-48s %d\n", get_username(inf.userid), printer_status.jobnum, inf.filename, filestat.st_size);
					}
					/* */
				}
				/* */
			}
			else
			{
				/* this printer is not printing a job, so make its current job number invalid */
				printing_jobs[x] = -1;
				/* */

				/* show this printer's status only if jobs from all users were requested */
				if (userid == -1)
				{
					if (printers[x].status == UP)
					{
						/* pretty output - space before first printer shown */
						if (shown == 0)
						{
							shown = 1;
							printf("\n");
						}
						/* */

						printf("PRINTER [%s] is INACTIVE\n", printers[x].name);
					}
					else
					{
						if (verbosity == 1)
						{
							/* pretty output - space before first printer shown */
							if (shown == 0)
							{
								shown = 1;
								printf("\n");
							}
							/* */

							printf("PRINTER [%s] is DOWN\n", printers[x].name);
						}
					}
				}
				/* */
			}
			/* */
		}
		/* */
	}

	return;
	/* */
}



/* ppq_show_waiting_jobs
**
** Report on jobs which are currently waiting in the queue to be printed.
**
** Arguments:
**	numprinters	the number of printers attached to the specified queue
**	printers	array of records from database containing information about each printer attached to specified queue
**	queue_idx	the index value of the queue whose waiting jobs are to be reported
**	userid		if not -1, the userid of the single user whose waiting jobs are to be reported
**	printing_jobs	an array containing either the index value of a job that is currently printing, or -1
**	verbosity	the verbosity level for output
**
** Exit conditions:
**	On error opening or reading the inf file for any job, reporting error to user.
**	On error getting the size of an existing dat file.
**
** Notes:
**	If verbosity is set to 1, the fact that there are no waiting jobs is explicitly reported.
**	A nonexistent dat file for a job will be reported with a "-" in its size field as displayed.
*/
void ppq_show_waiting_jobs(int numprinters, struct printer printers[], int queue_idx, int userid, int printing_jobs[], int verbosity)
{
	FILE *fp;
	char path[256];
	int waiting_job = 0, jobnum, y;
	int head, tail;
	struct inf inf;
	struct stat filestat;


	/* get the head and tail values for jobs in this queue */
	get_head_tail(queue_idx, &head, &tail);
	/* */

	/* if there are no jobs, just return... funny, there should be jobs unless this program got */
	/* suspended for an inordinate amount of time... */
	if ((head == -1) || (tail == -1))
	{
		if (verbosity == 1)
			printf("\nNO WAITING JOBS\n");

		return;
	}
	/* */

	/* go through all the pending jobs, printing out their relevant information */
	jobnum = tail;
	for(;;)
	{
		/* check to see if this job is currently printing */
		for (y=0; y < numprinters; y++)
		{
			if (jobnum == printing_jobs[y])
			{
				/* this job is currently printing, so we don't report it again */
				if (jobnum == head)	/* checked all jobs */
				{
					if ((waiting_job == 0) && (verbosity == 1))
						printf("\nNO WAITING JOBS\n");
					
					return;
				}

				jobnum = (jobnum + 1) % 1000;
				continue;	/* check the next job */
				/* */
			}
		}
		/* */

		/* open the job's inf file and get information about the job */
		sprintf(path, "%s/spool/%d/inf%d", ROOT, queue_idx, jobnum);
		fp = fopen(path, "r");
		if (fp == NULL)
		{
			/* determine whether this inf file wasn't opened because it doesn't exist (the job */
			/* may have been deleted) or because of some other error */
			if (errno != ENOENT)
			{
				/* file does exist */
				fprintf(stderr, "ppq: error opening existing inf file of job %d\n", jobnum);
				exit(0);
			}
			else
			{
				/* file doesn't exist, move on to next or finish if we've checked them all */
				if (jobnum == head)
				{
					if ((waiting_job == 0) && (verbosity == 1))
						printf("\nNO WAITING JOBS\n");
					
					return;
				}

				jobnum = (jobnum + 1) % 1000;
				continue;
			}
			/* */
		}
		if (fread(&inf, sizeof(struct inf), 1, fp) != 1)
		{
			fprintf(stderr, "ppq: error reading inf file of job %d\n", jobnum);
			exit(0);
		}
		fclose(fp);
		/* */

		/* display the waiting jobs heading if this is the first waiting job we found */
		/* check as well whether we want to print out everyone's jobs or only one user's */
		if (((waiting_job == 0) && (userid == -1)) || ((waiting_job == 0) && (userid == inf.userid)))
		{
			waiting_job = 1;

			printf("\nWAITING JOBS\n");
			printf("  %-8s %-3s %-48s %s\n", "Owner", "Job", "Filename", "Size");
		}
		/* */

		/* print out this job's information if we want all user's jobs, or this is the user */
		/* we want, if we want only one */
		if ((userid == -1) || (inf.userid == userid))
		{
			/* determine the size of this job's "dat" file */
			sprintf(path, "%s/spool/%d/dat%d", ROOT, queue_idx, jobnum);
			errno = 0;
			if ((stat(path, &filestat) == -1) && (errno != ENOENT))
			{
				fprintf(stderr, "ppq: unable to get stats on existing data file for job %d\n", jobnum);
				exit(0);
			}
			/* */

			/* print relevant information for this job */
			if (errno == ENOENT)
			{
				printf("  %-8s %3d %-48s -\n", get_username(inf.userid), jobnum, inf.filename);
				printf("  %-8s %3d %-48s %d\n", get_username(inf.userid), jobnum, inf.filename, filestat.st_size);
			}
			else
			{
				printf("  %-8s %3d %-48s %d\n", get_username(inf.userid), jobnum, inf.filename, filestat.st_size);
			}
			/* */
		}
		/* */

		/* check if this is the last job in the queue or not */
		if (jobnum == head)
		{
			if ((waiting_job == 0) && (verbosity == 1))
				printf("\nNO WAITING JOBS\n");
			
			return;
		}
		else
		{
			jobnum = (jobnum + 1) % 1000;
		}
	}
	/* */
}



/* calculate_head
**
** Determine the index value of the job at the head of the queue.  (This refers to the most recently added job in
** the queue.  Yes, this is backwards, we should be adding to the tail and removing from the head, but it really
** makes no difference and we don't care anyway.)
**
** Arguments:
**	queue_index	the index value in the database of the queue to be examined
**
** Returned value:
**	the index value of the job at the head of the queue
*/
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



/* calculate_tail
**
** Determine the index value of the job at the tail of the queue.  (This refers to the least recently added job in
** the queue.  Yes, this is backwards, we should be adding to the tail and removing from the head, but it really
** makes no difference and we don't care anyway.)
**
** Arguments:
**	queue_index	the index value in the database of the queue to be examined
**
** Returned value:
**	the index value of the job at the tail of the queue
*/
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



/* get_head_tail
**
** Determine the head and tail values of the queue.
**
** Arguments:
**	queue_index	the index value in the database of the queue to be examined
**	head		returns the head index value of the queued jobs
**	tail		returns the tail index value of the queued jobs
**
** Exit conditions:
**	If the tail value is successfully read, but the head value cannot be read, an error message is reported.
*/
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



/* bsd_show_queue_status
**
** This is one big ugly procedure which produces all of the BSD lpq-style output required for the BSD
** compatibility mode.  It is hideous... unruly...  It appears to work.
*/
int bsd_show_queue_status(int queue_idx, struct pps_queue *queue, int numprinters, struct printer printers[])
{
	int x;
	char path[1024];
	pid_t pid;
	FILE *fp;
	int printing_jobs[MAX_PRINTERS_PER_QUEUE];
	struct printer_status printer_status;
	struct inf inf;
	struct stat filestat;
	int lock_status;
	int head, tail, jobnum, rank, first=0;;


	/* try to find out if the daemon is running */
	sprintf(path, "%s/.dpid", ROOT);
	fp = fopen(path, "r");

	if (fp != NULL)
	{
		/* read daemon's pid */
		fscanf(fp, "%d", (int *)&pid);
		fclose(fp);
		/* */

		/* check to see if this process is running by sending it a null signal */
		if ((kill(pid, 0) != 0) && (errno == ESRCH))
		{
			/* daemon is not running */
			printf("Warning: no daemon present\n");
			/* */
		}
		/* */
	}
	else
	{
		/* assume if we can't read .dpid that the daemon cannot be running! */
		printf("Warning: no daemon present\n");
		/* */
	}
	/* */

	/* print queue's status */
	if (queue->status == DOWN)
	{
		printf("Warning: %s is down:\n", queue->name);
		printf("Warning: %s queue is turned off\n", queue->name);
	}
	/* */

	/* show user if queue is currently active */
	sprintf(path, "%s/spool/%d/.dpid", ROOT, queue_idx);
	fp = fopen(path, "r");

	if (fp != NULL)
	{
		/* read queue daemon's pid */
		fscanf(fp, "%d", (int *)&pid);
		fclose(fp);
		/* */

		/* check to see if this process is running by sending it a null signal */
		if (kill(pid, 0) == 0)
		{
			/* queue daemon is running */
			printf("%s is ready and printing\n", queue->name);
			/* */
		}
		/* */
	}
	/* */

	/* print out the jobs that are currently active */
	for (x=0; x < numprinters; x++)
	{
		/* remember, the queue element of the printers structure contains that particular */
		/* printer's key value, not the key value of its queue! */
		sprintf(path, "%s/spool/%d/.%d", ROOT, queue_idx, printers[x].queue);

		/* check to see if the printer is active, in which case it's status file will be locked */
		if (open_and_test_lock(path, &lock_status) == -1)
		{
			/* either the file is unlocked, or our lock test failed... but since this */
			/* is just a patchwork lpq mode, we'll just ignore it and not fail */
			lock_status = F_UNLCK;
		}
		/* */

		/* if this file is locked, we know the printer is doing something */
		if (lock_status == F_WRLCK)
		{
			/* open the status file for this printer */
			/* remember, we're using the queue element for the printer's key value. */
			sprintf(path, "%s/spool/%d/.%d", ROOT, queue_idx, printers[x].queue);
			fp = fopen(path, "r");
			if (fp != NULL)		/* ignore this printer if we encounter an error */
			{
				if (fread(&printer_status, sizeof(struct printer_status), 1, fp) == 1) /* ignore printer on error */
				{
					/* store the number of the job that is printing to this printer */
					printing_jobs[x] = printer_status.jobnum;
					fclose(fp);
					/* */

					/* open the job's inf file and get information about the job */
					sprintf(path, "%s/spool/%d/inf%d", ROOT, queue_idx, printer_status.jobnum);
					fp = fopen(path, "r");
					if (fp != NULL)		/* ignore this job if we encounter an error */
					{
						if (fread(&inf, sizeof(struct inf), 1, fp) == 1) /* ignore job on error */
						{
							fclose(fp);

							/* get the size of this job */
							errno = 0;
							sprintf(path, "%s/spool/%d/dat%d", ROOT, queue_idx, printer_status.jobnum);
							if (stat(path, &filestat) != -1)
							{
								/* print the heading for queued jobs if this is the first we found */
								if (first == 0)
								{
									first = 1;
									printf("Rank   Owner      Job  Files                                 Total Size\n");
								}
								/* */

								/* display active job */
								printf("%-7s%-11s%-5d%-38s%d bytes\n", "active", get_username(inf.userid),
									printer_status.jobnum, inf.filename, filestat.st_size);
								/* */
							}
							/* */
						}
					}
					/* */
				}
				/* */
			}
			/* */
		}
		else
		{
			/* this printer is not currently printing a job, so make its current job number invalid */
			printing_jobs[x] = -1;
			/* */
		}
		/* */
	}
	/* */

	/** print out the jobs that are waiting in the queue **/
	/* get the head and tail values for jobs in this queue */
	get_head_tail(queue_idx, &head, &tail);
	/* */

	/* return if there are no jobs */
	if ((head == -1) || (tail == -1))
	{
		printf("no entries\n");
		return 0;
	}
	/* */

	/* go through all pending jobs, printing out their relevant information */
	rank = 1;
	jobnum = tail;
	for(;;)
	{
		/* check to see if this job is currently printing */
		for (x=0; x < numprinters; x++)
		{
			if (jobnum == printing_jobs[x])
			{
				/* this job is currently printing */
				if (jobnum == head)	/* checked all jobs */
					return 0;

				jobnum = (jobnum + 1) % 1000;
				continue;
				/* */
			}
		}
		/* */

		/* open the job's inf file and get information about the job */
		sprintf(path, "%s/spool/%d/inf%d", ROOT, queue_idx, jobnum);
		fp = fopen(path, "r");
		if (fp != NULL)		/* ignore this job if we encounter an error */
		{
			if (fread(&inf, sizeof(struct inf), 1, fp) == 1) /* ignore job on error */
			{
				fclose(fp);

				/* get the size of this job */
				errno = 0;
				sprintf(path, "%s/spool/%d/dat%d", ROOT, queue_idx, jobnum);
				if (stat(path, &filestat) != -1)
				{
					/* display pending job */
					switch (rank)
					{
						case 1:	printf("%-7s", "1st    ");
							break;
						case 2:	printf("%-7s", "2nd    ");
							break;
						case 3:	printf("%-7s", "3rd    ");
							break;
						default:
							/* print the heading for queued jobs if this is the first we found */
							if (first == 0)
							{
								first = 1;
								printf("Rank   Owner      Job  Files                                 Total Size\n");
							}
							/* */

							/* using path here, just because I don't feel like */
							/* declaring anything else just for this */
							sprintf(path, "%dth       ", rank);
							printf("%-7s", path);
							/* */
							break;
					}
					rank++;

					printf("%-11s%-5d%-38s%d bytes\n", get_username(inf.userid),
						jobnum, inf.filename, filestat.st_size);
					/* */

					if (jobnum == head)
						return 0;
					jobnum = (jobnum + 1) % 1000;
					continue;
				}
				else
				{
					if (jobnum == head)
						return 0;
					jobnum = (jobnum + 1) % 1000;
					continue;
				}
				/* */
			}
			else
			{
				if (jobnum == head)
					return 0;
				jobnum = (jobnum + 1) % 1000;
				continue;
			}
		}
		else
		{
			if (jobnum == head)
				return 0;
			jobnum = (jobnum + 1) % 1000;
			continue;
		}
		/* */
	}
	/* */

	/* were no jobs queued or printing? */
	if (first == 0)
	{
		printf("no entries\n");
	}
	/* */
}

