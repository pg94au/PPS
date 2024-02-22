/*
 *  pjl_jetdirect.c
 *
 *  Print driver for HP LaserJet (PJL compatible) printers with JetDirect compatible interfaces.
 *
 *  (c)1997,1998 Paul Grebenc
 *
 *  $Header: /usr/local/PPS_RCS/src/printer/pjl_jetdirect.c,v 1.2 2000/04/13 00:37:07 paul Exp $
 */


#include <config.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <ctype.h>
#include <netdb.h>
#include <errno.h>

#include "pps.h"
#include "ppd.h"
#include "locking.h"
#include "logging.h"

#ifdef IGNORE
#define PJL_CONNECT_RETRY_TIMEOUT	60	/* minutes before we give up on a job we have not been able to begin printing yet */
						/* (zero to never give up) */
#define PJL_PRINTER_RESET_TIMEOUT	60	/* minutes we spend trying to reset the printer's display after we have printed a job */
						/* (zero to never give up, until we succeed or another job is trying to print) */
#define PJL_INACTIVITY_TIMEOUT		30	/* seconds before we assume an inactive  connection was lost */
						/* (should be set to printer's internal timeout value) */
						/* (zero to never timeout -- not a good idea probably) */
#define PJL_CONNECT_RETRY_INTERVAL	15	/* seconds we wait between failed connection attempts */
#endif

#define PJL_CODE_RESET	10005
#define PJL_CODE_CANCEL	10007

/*
#define DEBUG
#define READ_DEBUG
#define ESTABLISH_DEBUG
#define PREPARE_DEBUG
#define STATUS_DEBUG
#define RESET_DEBUG
#define UNSOLICITED_DEBUG
#define SEND_DEBUG
#define PAGE_DEBUG
*/

struct lj4_struct
{
	int ustatus_code;
	char display[256];	/* I have no idea how large this should be... for example, the 4000N claims to only have a */
				/* 32 char display, but it has at least one 61 char length message...  Setting the length of */
				/* this string too low will cause an overrun into the following variables, which is obviously */
				/* bad, so we will set it higher than is probably necessary, and not worry about it for now. */
	int online;
	int *pages;
	int single_page_total;	/* incremented as we get each USTATUS PAGE message */
	int job_page_total;	/* incremented as we get each USTATUS JOB/END message */
};

struct connection_struct
{
	int sockfd;
	unsigned long address;
	int port;
	int timed_out;
	struct timeval start_tv;
};

struct general_struct
{
	int job_id;
	int status_fd;
	FILE *job_fp;
	char address[256];
	char queue[33];
	char printer[33];
	char username[9];
	int queue_idx;
	int printer_idx;
};

int print_to_jetdirect(struct inf *inf, struct eligible_printer *job_entry, int *pages_printed);

int initialize_variables(struct inf *inf, struct eligible_printer *job_entry, int *pages_printed);
int establish_connection(void);
int prepare_for_job(void);
int get_printer_status(void);

int send_job_to_printer(void);
int finish_job_to_printer(void);
int monitor_printer_to_finish(void);

int accept_unsolicited_message(char *recvbuff);
int accept_ustatus_device_or_timed(char *first_line);
int accept_ustatus_job(char *first_line);
int accept_ustatus_page(char *first_line);

void reset_printer_display(void);
int connect_and_reset_printer(void);

int write_status_file(int status);
void signal_handler(int type);
int writen(int fd, char *ptr, int nbytes);
int readline(int fd, char *ptr, int maxlen);
int read_to_formfeed(void);


struct lj4_struct lj4;
struct connection_struct connection;
struct general_struct general;
char errstr[1024];

extern struct printer printers[MAX_PRINTERS];
extern struct pps_queue queues[MAX_QUEUES];



int print_to_jetdirect(struct inf *inf, struct eligible_printer *job_entry, int *pages_printed)
{
	int retval;
	struct timeval now_tv;
	int result;
	struct sigaction action;
	int cancel_requested = 0;


	/* initialize */
	if ((retval = initialize_variables(inf, job_entry, pages_printed)) != PPS_OK)
		return retval;
	/* */

	/* catch these signals should they occur during communication with printer */
	connection.timed_out = FALSE;

	action.sa_handler = signal_handler;
	sigemptyset(&action.sa_mask);
	action.sa_flags = 0;
	if (sigaction(SIGPIPE, &action, NULL) < 0)
		exit(0);
	action.sa_handler = signal_handler;
	sigemptyset(&action.sa_mask);
	action.sa_flags = 0;
	if (sigaction(SIGALRM, &action, NULL) < 0)
		exit(0);
	/* */

	/* try (repeatedly, if necessary) to get to the point where we can begin sending our job */
	for (;;)
	{
		/* update status to show that we are trying to connect to the printer */
		write_status_file(PPS_CONNECTING);
		/* */

		/* now we are going to get a connection to the printer, up the point where we will be */
		/* getting TIMED messages back, established */
		if ((retval = establish_connection()) != PPS_OK)
			return retval;
		/* */

		/* now setup the printer for the job we are going to send (set display, modes, etc.) */
		if (prepare_for_job() == PPS_OK)
		{
			/* before we begin, grab the current status of the printer and write it to our status file */
			if (get_printer_status() == PPS_OK)
				break;
			/* */
		}
		else
		{
			/* update status to show that our connection has been refused */
			write_status_file(PPS_CONNECTION_REFUSED);
			/* */

			sleep(PJL_CONNECT_RETRY_INTERVAL);
			if (PJL_CONNECT_RETRY_TIMEOUT != 0)
			{
				gettimeofday(&now_tv, NULL);
				if (now_tv.tv_sec >= (connection.start_tv.tv_sec + (PJL_CONNECT_RETRY_TIMEOUT * 60)))
					return PPS_CONNECT_TIMEOUT;
			}
			else
			{
				/* write out current connection status */
				write_status_file(PPS_CONNECT_TIMEOUT);
				/* */
			}
		}
		/* */
	}
	/* */

	/* update status file to show that we are starting to send the job to the printer */
	write_status_file(PPS_SENDING_JOB);
	/* */

	/* start sending the job, receiving unsolicited messages as we go */
	result = send_job_to_printer();

	if (result == PPS_JOB_CANCEL_REQUESTED)
		cancel_requested = 1;	/* remember this for later when we log the result */

	/* send suffix to job, letting printer know the job has completely been sent */
	if ((result = finish_job_to_printer()) == PPS_OK)
	{
		if (result == PPS_JOB_CANCEL_REQUESTED)
			cancel_requested = 1;	/* remember this for later when we log the result */

		/* watch the progress of the printer until the last page comes out */
		result = monitor_printer_to_finish();
		/* */

		if (result == PPS_JOB_CANCEL_REQUESTED)
			cancel_requested = 1;	/* remember this for later when we log the result */
	}
	/* */

	/* we are through communicating with the printer for this job */
	close(connection.sockfd);
	/* */

	/* now that the job is complete, spawn a process to reset the printer's display */
	reset_printer_display();
	/* */

	/* determine which page count we are going to trust, depending on how the job terminated */
	/* NOTE:  If the job completed uneventfully, and we got our JOB/END message, then we can trust the */
	/*        PAGES= count given to us in this message.  Since it comes from the printer's internal count, */
	/*        and we've seen no reason to distrust it thus far, we will trust it more than our own single */
	/*        page count.  If on the other hand, we do not have the printer's page count, esp. in the case */
	/*        of a job that timed out or a printer that was reset, we will have to go on the number of */
	/*        pages which we have counted as the job was progressing.  At worst, this count should only */
	/*        be off, if ever, by at most a single page... I think. */
	if ((cancel_requested) && ((result != PPS_JOB_TIMEOUT) && (result != PPS_PRINTER_RESET)))
	{
		/* cancelled jobs can terminate gracefully, but we still want to log them as cancelled */
		result = PPS_JOB_CANCELLED;
		/* */
	}

	switch (result)
	{
		case PPS_JOB_PRINTED:
		case PPS_JOB_CANCELLED:
			*lj4.pages = lj4.job_page_total;
			break;
		case PPS_PRINTER_RESET:
		case PPS_JOB_TIMEOUT:
			*lj4.pages = lj4.single_page_total;
			break;
	}
	/* */

	return result;
}


int initialize_variables(struct inf *inf, struct eligible_printer *job_entry, int *pages_printed)
{
	struct hostent *hostent;
	struct in_addr *in_addr;


	/* set pointer to number of pages printed, and set its value to zero */
	/* pages printed has to be accessible just about everywhere, but changes to it need to be */
	/* reflected in the pointer passed to print_to_jetdirect, so we make the value of the pointer */
	/* passed to print_to_jetdirect, which is passed to us here, global */
	lj4.pages = pages_printed;
	*lj4.pages = 0;
	/* */

	/* NOTE:  These two page totals are calculated separately.  If a print job ends prematurely, before we have */
	/* received a USTATUS JOB/END message with a page total, we can only fall back on the number of pages we have */
	/* been counting as they appeared in USTATUS PAGE messages.  As there are some problems with USTATUS PAGE messages */
	/* coming from the printer without corresponding pages actually output from the printer (which happens at least */
	/* occasionally when the printer is reset from its control panel while printing), we would rather rely on the */
	/* end of job page count, wherever possible.  Hopefully there are no bugs in the USTATUS PAGE counting mechanism */
	/* of this code, but it is probably safer to rely on the printer's own counting mechanism, wherever possible. */
	lj4.single_page_total = 0;
	lj4.job_page_total = 0;
	/* */

	/* fill in the general job information structure, which contains commonly needed information */
	/* pertaining to the job we are printing */
	general.job_id = job_entry->jobnum;
	strcpy(general.address, printers[job_entry->keyval].specific.pjl_jetdirect.address);
	strcpy(general.queue, queues[printers[job_entry->keyval].queue].name);
	strcpy(general.printer, printers[job_entry->keyval].name);
	strcpy(general.username, get_username(inf->userid));
	general.queue_idx = printers[job_entry->keyval].queue;
	general.printer_idx = job_entry->keyval;
	general.status_fd = job_entry->lock_fd;
	/* */

	/* the function sending the job to the printer is going to need the file pointer to it */
	general.job_fp = job_entry->dat_fp;
	/* */

	/* get the address of this printer in binary network byte order form and its port */
	if (isdigit(general.address[0]))
	{
		if ((connection.address = inet_addr(general.address)) == -1)
		{
			sprintf(errstr, "%s/%s (pjl_jetdirect) (%d): invalid ip address '%s' given for printer '%s' attached to queue '%s'",
				general.queue, general.printer, getpid(), general.address, general.printer, general.queue);
			error_log(errstr);
			#ifdef DEBUG
			debug_log(errstr);
			#endif

			return PPS_JOB_FAILED;
		}
	}
	else
	{
		if ((hostent = gethostbyname(general.address)) == NULL)
		{
			sprintf(errstr, "%s/%s (pjl_jetdirect) (%d): unable to resolve hostname '%s' given for printer '%s' attached to queue '%s'",
				general.queue, general.printer, getpid(), general.address, general.printer, general.queue);
			error_log(errstr);
			#ifdef DEBUG
			debug_log(errstr);
			#endif

			return PPS_JOB_FAILED;
		}
		else
		{
		 	in_addr = (struct in_addr *)hostent->h_addr_list[0];
			connection.address = in_addr->s_addr;
		}
	}

	connection.port = printers[job_entry->keyval].specific.pjl_jetdirect.port;
	/* */

	/* get the time at which we begin attempting to print this job */
	gettimeofday(&connection.start_tv, NULL);
	/* */

	return PPS_OK;
}



int establish_connection()
{
	struct sockaddr_in serv_addr;
	struct timeval now_tv;
	char sendbuff[1024], recvbuff[1024], timestamp[26], synchbuff[256];
	int len;
	time_t t;


	for(;;)
	{
		/*** make the network connection to the printer ***/

		/* open a TCP socket (an Internet stream socket) */
		if ((connection.sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
		{
			sprintf(errstr, "%s/%s (pjl_jetdirect) (%d): cannot allocate stream socket to print to JetDirect in printer '%s' attached to queue '%s' at '%s'",
				general.queue, general.printer, getpid(), general.printer, general.queue, general.address);
			error_log(errstr);

			#ifdef DEBUG
			debug_log(errstr);
			#endif

			return PPS_ERROR;
		}
		/* */

		/* fill in the structure "serv_addr" with the address of the server that we want to connect to */
		bzero((char *)&serv_addr, sizeof(serv_addr));
		serv_addr.sin_family = AF_INET;
		serv_addr.sin_addr.s_addr = connection.address;
		serv_addr.sin_port = htons(connection.port);
		/* */

		/* attempt to connect to the remote printer */
		if (connect(connection.sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
		{
			/* connection to remote printer failed for some reason */
			#ifdef ESTABLISH_DEBUG
			sprintf(errstr, "ppd/queue/print(pjl_jetdirect) (%d): attempt to connect to printer [%s:%s] failed",
				getpid(), general.queue, general.printer);
			perror(errstr);
			#endif

			close(connection.sockfd);

			sleep(PJL_CONNECT_RETRY_INTERVAL);
			if (PJL_CONNECT_RETRY_TIMEOUT != 0)
			{
				gettimeofday(&now_tv, NULL);
				if (now_tv.tv_sec >= (connection.start_tv.tv_sec + (PJL_CONNECT_RETRY_TIMEOUT * 60)))
					return PPS_CONNECT_TIMEOUT;
			}
			else
			{
				/* write out current connection status */
				switch(errno)
				{
					case 111:	/* connection refused */
						write_status_file(PPS_CONNECTION_REFUSED);
						break;
					case 113:	/* no route to host */
						write_status_file(PPS_NO_ROUTE);
						break;
					default:
						write_status_file(PPS_ERROR);
						break;
				}
				/* */
			}
			/* */
		}
		else
		{
			/*** attempt to send RESET, TIMED and synchronization requests ***/

			/* enter PJL mode, send RESET, TIMED and ECHO requests to initialize printer */
			#ifdef ESTABLISH_DEBUG
			sprintf(errstr, "%s/%s (pjl_jetdirect) (%d): sending RESET, TIMED and ECHO commands", general.queue, general.printer, getpid());
			debug_log(errstr);
			#endif
			t = time(NULL);
			sprintf(timestamp, ctime(&t));
			timestamp[24] = '\0';
			sprintf(sendbuff, "%c%%-12345X@PJL\r\n@PJL RESET\r\n@PJL USTATUS TIMED=15\r\n@PJL ECHO %s\r\n%c%%-12345X", 27, timestamp, 27);
			if (writen(connection.sockfd, sendbuff, strlen(sendbuff)) < strlen(sendbuff))
			{
				#ifdef DEBUG
				sprintf(errstr, "%s/%s (pjl_jetdirect) (%d): problem writing to socket to initialize connection to printer\n",
					general.queue, general.printer, getpid());
				debug_log(errstr);
				#endif

				close(connection.sockfd);

				sleep(PJL_CONNECT_RETRY_INTERVAL);
				if (PJL_CONNECT_RETRY_TIMEOUT != 0)
				{
					gettimeofday(&now_tv, NULL);
					if (now_tv.tv_sec >= (connection.start_tv.tv_sec + (PJL_CONNECT_RETRY_TIMEOUT * 60)))
						return PPS_CONNECT_TIMEOUT;
				}
				else
				{
					/* write out current connection status */
					write_status_file(PPS_CONNECT_TIMEOUT);
					/* */
				}
			}
			else
			{
				#ifdef ESTABLISH_DEBUG
				sprintf(errstr, "%s/%s (pjl_jetdirect) (%d): sent RESET, TIMED and ECHO commands\n",
					general.queue, general.printer, getpid());
				debug_log(errstr);
				#endif

				/*** wait for response from ECHO command (synchronization message) ***/
				sprintf(synchbuff, "@PJL ECHO %s\r\n", timestamp);
				for (;;)
				{
					if (len = readline(connection.sockfd, recvbuff, sizeof(recvbuff)) == -1)
					{
						#ifdef DEBUG
						sprintf(errstr, "%s/%s (pjl_jetdirect) (%d): problem reading from socket while waiting for synch response\n",
							general.queue, general.printer, getpid());
						debug_log(errstr);
						#endif

						close(connection.sockfd);

						if (PJL_CONNECT_RETRY_TIMEOUT != 0)
						{
							gettimeofday(&now_tv, NULL);
							if (now_tv.tv_sec >= (connection.start_tv.tv_sec + (PJL_CONNECT_RETRY_TIMEOUT * 60)))
								return PPS_CONNECT_TIMEOUT;
							else
								break;
						}
						else
						{
							/* write out current connection status */
							write_status_file(PPS_CONNECT_TIMEOUT);
							/* */

							break;
						}
					}
					if (strcmp(recvbuff, synchbuff) == 0)
					{
						#ifdef DEBUG
						sprintf(errstr, "%s/%s (pjl_jetdirect) (%d): received response to sychronization message, connection established\n",
							general.queue, general.printer, getpid());
						debug_log(errstr);
						#endif

						return PPS_OK;
					}
				}
				/*** ***/
			}
			/* */
		}
	}
}



/* we need to:
 *		give this job a name
 *		change the display on the printer
 *		request all status messages
 */
int prepare_for_job()
{
	char sendbuff[1024], recvbuff[1024], synchbuff[256], timestamp[26], uppername[9];
	time_t t;
	int len, x;


	/*** enter PJL mode, set job name, printer display, and request all status messages ***/

	/* get user's name in all uppercase for the printer's display */
	for(x=0; x<strlen(general.username); x++)
	{
		uppername[x] = toupper(general.username[x]);
	}
	uppername[x] = '\0';
	/* */

	/* get a timestamp for our ECHO command, to determine when our requests have been processed */
	t = time(NULL);
	sprintf(timestamp, ctime(&t));
	timestamp[24]='\0';
	/* */

	/* build the string to send to the printer and send it */
	#ifdef PREPARE_DEBUG
	sprintf(errstr, "%s/%s (pjl_jetdirect) (%d): setting job name, printer display, and requesting status messages",
		general.queue, general.printer, getpid());
	debug_log(errstr);
	#endif
	sprintf(sendbuff, "\033%%-12345X@PJL\r\n@PJL JOB NAME=\"%d\"\r\n@PJL RDYMSG DISPLAY=\"%s\"\r\n",
		general.job_id, uppername);
	strcat(sendbuff, "@PJL USTATUS DEVICE=ON\r\n@PJL USTATUS JOB=ON\r\n@PJL USTATUS PAGE=ON\r\n");
	sprintf(sendbuff+strlen(sendbuff), "@PJL ECHO %s\r\n\033%%-12345X", timestamp);

	if (writen(connection.sockfd, sendbuff, strlen(sendbuff)) < strlen(sendbuff))
	{
		#ifdef DEBUG
		sprintf(errstr, "%s/%s (pjl_jetdirect) (%d): problem writing to socket while preparing to send job",
			general.queue, general.printer, getpid());
		debug_log(errstr);
		#endif

		close(connection.sockfd);

		write_status_file(PPS_CONNECT_TIMEOUT);

		return PPS_JOB_TIMEOUT;
	}
	/* */

	/* wait for response from ECHO command (synchronize) */
	sprintf(synchbuff, "@PJL ECHO %s\r\n", timestamp);
	for(;;)
	{
		if (len = readline(connection.sockfd, recvbuff, sizeof(recvbuff)) == -1)
		{
			#ifdef DEBUG
			sprintf(errstr, "%s/%s (pjl_jetdirect) (%d): problem reading from socket while waiting for synch response after printer setup",
				general.queue, general.printer, getpid());
			debug_log(errstr);
			#endif

			close(connection.sockfd);

			return PPS_JOB_TIMEOUT;
		}
		if (strcmp(recvbuff, synchbuff) == 0)
		{
			#ifdef PREPARE_DEBUG
			sprintf(errstr, "%s/%s (pjl_jetdirect) (%d): received response to synch request after printer setup",
				general.queue, general.printer, getpid());
			debug_log(errstr);
			#endif

			return PPS_OK;
		}
	}
	/* */

	#ifdef PREPARE_DEBUG
	sprintf(errstr, "%s/%s (pjl_jetdirect) (%d): set up job name, printer display; requested status messages",
		general.queue, general.printer, getpid());
	debug_log(errstr);
	#endif

	return PPS_OK;
}



int send_job_to_printer()
{
	fd_set rfds, wfds;
	struct timeval tv, last_tv, now_tv;
	int retval;
	int read_char = 0;
	unsigned char write_char;
	int sent_a_char = 1;


	/* initialize the current time used to check connectivity */
	gettimeofday(&last_tv, NULL);
	/* */

	/* send the job to the printer, accepting unsolicited messages as we go */
	#ifdef SEND_DEBUG
	sprintf(errstr, "%s/%s (pjl_jetdirect) (%d): beginning to send the job to the printer", general.queue, general.printer, getpid());
	debug_log(errstr);
	#endif

	while (read_char != EOF)
	{
		/* determine whether we can read or write before a timeout occurs */
		FD_ZERO(&rfds);
		FD_ZERO(&wfds);
		FD_SET(connection.sockfd, &rfds);
		FD_SET(connection.sockfd, &wfds);
		tv.tv_sec = (PJL_INACTIVITY_TIMEOUT * 2);
		tv.tv_usec = 0;

		retval = select(connection.sockfd+1, &rfds, &wfds, NULL, &tv);
		/* */

		/* check if we can do some i/o */
		if (retval)
		{
			if (FD_ISSET(connection.sockfd, &wfds))
			{
				/* we can write to the printer */
				if (sent_a_char == 1)
				{
					/* read another character from the file only if we've successfully sent the current one */
					read_char = fgetc(general.job_fp);
					sent_a_char = 0;
					/* */
				}
				if (read_char != EOF)
				{
					alarm(PJL_INACTIVITY_TIMEOUT);
					write_char = (unsigned char)read_char;
					if (write(connection.sockfd, &write_char, 1) != 1)
					{
						alarm(0);
						#ifdef DEBUG
						sprintf(errstr, "%s/%s (pjl_jetdirect) (%d): problem sending job to printer",
							general.queue, general.printer, getpid());
						debug_log(errstr);
						#endif

						sprintf(errstr, "ppd/queue/print(pjl_jetdirect) (%d): lost connection to printer '%s' on queue '%s' while printing job for '%s'",
							getpid(), general.printer, general.queue, general.username);
						error_log(errstr);

						return PPS_JOB_TIMEOUT;
					}
					else
					{
						sent_a_char = 1;
					}
				}
				/* */
			}

			if (FD_ISSET(connection.sockfd, &rfds))
			{
				/* we can read from the printer */
				gettimeofday(&last_tv, NULL);

				retval = accept_unsolicited_message(NULL);

				switch(retval)
				{
					case PPS_JOB_PRINTED:
					case PPS_JOB_TIMEOUT:
					case PPS_JOB_CANCEL_REQUESTED:
					case PPS_JOB_CANCELLED:
					case PPS_PRINTER_RESET:
						return retval;
					default:
						break;
				}
				/* */
			}
		}
		/* */

		/* check if we've heard from the printer within our defined timeout value */
		gettimeofday(&now_tv, NULL);
		if ((now_tv.tv_sec - last_tv.tv_sec) > PJL_INACTIVITY_TIMEOUT)
		{
			/* we haven't heard from the printer within our defined timeout period, assume connection lost */
			sprintf(errstr, "%s/%s (pjl_jetdirect) (%d): lost connection to '%s' attached to '%s' while printing job for '%s'",
				general.queue, general.printer, getpid(), general.printer, general.queue, general.username);
			error_log(errstr);

			#ifdef DEBUG
			debug_log(errstr);
			#endif
			
			return PPS_JOB_TIMEOUT;
			/* */
		}
		/* */
	}

	#ifdef SEND_DEBUG
	sprintf(errstr, "%s/%s (pjl_jetdirect) (%d): finished sending job to printer", general.queue, general.printer, getpid());
	debug_log(errstr);
	#endif
	/* */

	/* an OK simply means that we are finished our job here, and nothing eventful happened */
	/* to cause the job to be terminated prematurely */
	return PPS_OK;
	/* */
}



int finish_job_to_printer()
{
	char ubuff[1024];
	int nleft, nwritten;
	fd_set rfds, wfds;
	struct timeval tv, last_tv, now_tv;
	int retval;


	/* build the string to send to the printer to tell it the job has completed */
	sprintf(ubuff, "\033%%-12345X@PJL\r\n@PJL EOJ NAME=\"%d\"\r\n\033%%-12345X", general.job_id);
	/* */

	/* initialize the current time used to check connectivity */
	gettimeofday(&last_tv, NULL);
	/* */

	/* send the entire string, a piece at a time if necessary, watching for unsolicited messages as we go */
	nleft = strlen(ubuff);
	while (nleft > 0)
	{
		/* determine whether we can read or write before a timeout occurs */
		FD_ZERO(&rfds);
		FD_ZERO(&wfds);
		FD_SET(connection.sockfd, &rfds);
		FD_SET(connection.sockfd, &wfds);
		tv.tv_sec = (PJL_INACTIVITY_TIMEOUT * 2);
		tv.tv_usec = 0;

		retval = select(connection.sockfd+1, &rfds, &wfds, NULL, &tv);
		/* */

		/* check if we can do some i/o */
		if (retval)
		{
			if (FD_ISSET(connection.sockfd, &wfds))
			{
				/* we can write to the printer */
				alarm(PJL_INACTIVITY_TIMEOUT);
				nwritten = write(connection.sockfd, &ubuff[strlen(ubuff)-nleft], nleft);
				alarm(0);
				if (nwritten == -1)
				{
					return PPS_JOB_TIMEOUT;
				}
				else
				{
					nleft -= nwritten;
				}
				/* */
			}

			if (FD_ISSET(connection.sockfd, &rfds))
			{
				/* we can read from the printer */
				gettimeofday(&last_tv, NULL);

				retval = accept_unsolicited_message(NULL);

				switch(retval)
				{
					case PPS_JOB_PRINTED:
					case PPS_JOB_TIMEOUT:
					case PPS_JOB_CANCEL_REQUESTED:
					case PPS_JOB_CANCELLED:
					case PPS_PRINTER_RESET:
						return retval;
					default:
						break;
				}
				/* */
			}
		}
		/* */

		/* check if we've heard from the printer within our defined timeout value */
		gettimeofday(&now_tv, NULL);
		if ((now_tv.tv_sec - last_tv.tv_sec) > PJL_INACTIVITY_TIMEOUT)
		{
			/* we haven't heard from the printer within our defined timeout period, assume connection lost */
			sprintf(errstr, "%s/%s (pjl_jetdirect) (%d): lost connection to '%s' attached to '%s' while printing job for '%s'",
				general.queue, general.printer, getpid(), general.printer, general.queue, general.username);
			error_log(errstr);

			#ifdef DEBUG
			debug_log(errstr);
			#endif

			return PPS_JOB_TIMEOUT;
			/* */
		}
		/* */
	}

	#ifdef SEND_DEBUG
	sprintf(errstr, "%s/%s (pjl_jetdirect) (%d): end of job marker sent to printer", general.queue, general.printer, getpid());
	#endif

	return PPS_OK;
}



int monitor_printer_to_finish()
{
	fd_set rfds;
	struct timeval tv;
	int retval;
	int job_cancelled = 0;


	/* wait for the job to finish printing, monitoring its progress as it goes */
	for (;;)
	{
		/* determine whether we can read before a timeout occurs */
		FD_ZERO(&rfds);
		FD_SET(connection.sockfd, &rfds);
		tv.tv_sec = (PJL_INACTIVITY_TIMEOUT * 2);
		tv.tv_usec = 0;

		retval = select(connection.sockfd+1, &rfds, NULL, NULL, &tv);
		/* */

		/* check if we have anything to read from the printer */
		if (retval)
		{
			/* we can read a message from the printer */
			retval = accept_unsolicited_message(NULL);

			switch(retval)
			{
				case PPS_JOB_PRINTED:
					if (job_cancelled == 1)
						return PPS_JOB_CANCELLED;
					else
						return PPS_JOB_PRINTED;
				case PPS_JOB_TIMEOUT:
				case PPS_PRINTER_RESET:
					return retval;
				case PPS_JOB_CANCEL_REQUESTED:
				case PPS_JOB_CANCELLED:
					job_cancelled = 1;
					break;
				default:
					break;
			}
			/* */
		}
		else
		{
			/* we timed out waiting */
			sprintf(errstr, "%s/%s (pjl_jetdirect) (%d): lost connection to '%s' attached to '%s' while printing job for '%s'",
				general.queue, general.printer, getpid(), general.printer, general.queue, general.username);
			error_log(errstr);

			#ifdef DEBUG
			debug_log(errstr);
			#endif

			return PPS_JOB_TIMEOUT;
		}
		/* */
	}
	/* */
}



int accept_unsolicited_message(char *recvbuff)
{
	char ubuff[1024];

	/* get the first line of the message, if we don't have it already, so we can analyze what kind it is */
	if (recvbuff == NULL)
	{
		/* try to read a line from the printer */
		if (readline(connection.sockfd, ubuff, 255) == -1)
		{
			/* we lost our connection reading from the printer */
			#ifdef DEBUG
			sprintf(errstr, "%s/%s (pjl_jetdirect) (%d): problem reading unsolicited message from printer",
				general.queue, general.printer, getpid());
			debug_log(errstr);
			#endif

			sprintf(errstr, "ppd/queue/print(pjl_jetdirect) (%d): lost connection to printer '%s' on queue '%s' while printing job for '%s'",
				getpid(), general.printer, general.queue, general.username);
			error_log(errstr);

			return PPS_JOB_TIMEOUT;
			/* */
		}
		/* */
	}
	else
	{
		/* use the line passed to us as the first line */
		strcpy(ubuff, recvbuff);
		/* */
	}
	/* */

	/*** find out which message was sent to us, and deal with it accordingly ***/

	/* USTATUS DEVICE or USTATUS TIMED messages */
	if ((strstr(ubuff, "@PJL USTATUS DEVICE\r\n") != NULL) || (strstr(ubuff, "@PJL USTATUS TIMED\r\n") != NULL))
	{
		#ifdef UNSOLICITED_DEBUG
		if (strstr(ubuff, "@PJL USTATUS DEVICE\r\n") != NULL)
		{
			sprintf(errstr, "%s/%s (pjl_jetdirect) (%d): USTATUS DEVICE message received", general.queue, general.printer, getpid());
			debug_log(errstr);
		}
		else
		{
			sprintf(errstr, "%s/%s (pjl_jetdirect) (%d): USTATUS TIMED message received", general.queue, general.printer, getpid());
			debug_log(errstr);
		}
		#endif

		/* handle this message */
		return accept_ustatus_device_or_timed(ubuff);
		/* */
	}
	/* */

	/* USTATUS JOB messages */
	if (strstr(ubuff, "@PJL USTATUS JOB\r\n") != NULL)
	{
		#ifdef UNSOLICITED_DEBUG
		sprintf(errstr, "%s/%s (pjl_jetdirect) (%d): USTATUS JOB message received", general.queue, general.printer, getpid());
		debug_log(errstr);
		#endif

		/* handle this message */
		return accept_ustatus_job(ubuff);
		/* */
	}
	/* */

	/* USTATUS PAGE messages */
	if (strstr(ubuff, "@PJL USTATUS PAGE\r\n") != NULL)
	{
		#ifdef UNSOLICITED_DEBUG
		sprintf(errstr, "%s/%s (pjl_jetdirect) (%d): USTATUS PAGE message received", general.queue, general.printer, getpid());
		debug_log(errstr);
		#endif

		/* handle this message */
		return accept_ustatus_page(ubuff);
		/* */
	}
	/* */

	/* if the message received was not yet handled, just ignore it */
	/* NOTE:  We do get messages from the printer which appear to be undocumented/unexpected... */
	/*        This includes the <ESC>%-12345X string and %%[ Page: x ]%% messages, that I know of. */
	/*        In consideration of the possibility that there may be other strings sent by the */
	/*        printer which are not documented, we must be prepared for any that we do not */
	/*        know how to parse, or simply wish to ignore.  This is of course an easy thing to do, */
	/*        if we simply ignore them. */
	#ifdef UNSOLICITED_DEBUG
	sprintf(errstr, "%s/%s (pjl_jetdirect) (%d): received an unknown unsolicited message which we are ignoring",
		general.queue, general.printer, getpid());
	debug_log(errstr);
	#endif

	return PPS_OK;
	/* */
}



int accept_ustatus_device_or_timed(char *first_line)
{
	char ubuff[1024];
	int x;


	/* NOTE:  We aren't going to be making use of the first_line string passed to us right now, and probably */
	/*        never will.  If we ever need to, we'd like it to be available, without having to rewrite */
	/*        everything.  The reason we don't use it is because at this point we don't need the first line */
	/*        sent to us.  It still seems more complete to me if this function has access to the entire */
	/*        message sent to it, including the first line, whether it makes use of it or not.  An argument */
	/*        could certainly be made against this choice. */

	/* grab the data sent to us in this device status update, until we hit a form-feed at its end */
	do
	{
		/* read a line in so we can analyze it */
		if (readline(connection.sockfd, ubuff, 1023) == -1)
		{
			/* we lost our connection reading from the printer */
			#ifdef DEBUG
			sprintf(errstr, "%s/%s (pjl_jetdirect) (%d): problem reading USTATUS TIMED or DEVICE message from printer",
				general.queue, general.printer, getpid());
			debug_log(errstr);
			#endif

			sprintf(errstr, "%s/%s (pjl_jetdirect) (%d): lost connection to printer '%s' on queue '%s' while printing job for '%s'",
				general.queue, general.printer, getpid(), general.printer, general.queue, general.username);
			error_log(errstr);

			return PPS_JOB_TIMEOUT;
			/* */
		}
		/* */

		/* check for the current display */
		if (strncmp(ubuff, "DISPLAY=", 8) == 0)
		{
			/* grab the string between quotes */
			x=0;
			while (ubuff[x+9] != '\"')
			{
				lj4.display[x] = ubuff[x+9];
				x++;
			}
			lj4.display[x] = '\0';
			/* */

			#ifdef UNSOLICITED_DEBUG
			sprintf(errstr, "%s/%s (pjl_jetdirect) (%d): DISPLAY=\"%s\"", general.queue, general.printer, getpid(), lj4.display);
			debug_log(errstr);
			#endif
		}
		/* */

		/* check for printer on/offline status */
		if (strncmp(ubuff, "ONLINE=", 7) == 0)
		{
			if (strncmp(ubuff+7, "FALSE", 5) == 0)
			{
				#ifdef UNSOLICITED_DEBUG
				sprintf(errstr, "%s/%s (pjl_jetdirect) (%d): ONLINE=FALSE", general.queue, general.printer, getpid());
				debug_log(errstr);
				#endif

				write_status_file(PPS_OFFLINE);

				lj4.online = PPS_OFFLINE;
			}
			else
			{
				#ifdef UNSOLICITED_DEBUG
				sprintf(errstr, "%s/%s (pjl_jetdirect) (%d): ONLINE=TRUE", general.queue, general.printer, getpid());
				debug_log(errstr);
				#endif

				if (lj4.online == FALSE)
				{
					write_status_file(PPS_ONLINE);
				}

				lj4.online = PPS_ONLINE;
			}
		}
		/* */

		/* check what the device status code currently is */
		if (strncmp(ubuff, "CODE=", 5) == 0)
		{
			/* read the code as an integer */
			sscanf(ubuff+5, "%d", &lj4.ustatus_code);
			/* */

			#ifdef UNSOLICITED_DEBUG
			sprintf(errstr, "%s/%s (pjl_jetdirect) (%d): CODE=%d", general.queue, general.printer, getpid(), lj4.ustatus_code);
			debug_log(errstr);
			#endif
		}
		/* */
	}
	while (ubuff[0] != '\f');
	/* */

	/* return a status code depending on what message was sent to us */
	/* - for status messages that require no action be taken, simply return PPS_OK */
	switch(lj4.ustatus_code)
	{
		case PJL_CODE_RESET:
			/* this message is sent if the printer (LJ4's, for instance) */
			/* are manually reset from the control panel on the machine */
			write_status_file(PPS_RESETTING_PRINTER);

			return PPS_PRINTER_RESET;

		case PJL_CODE_CANCEL:
			/* this message is sent to us when the user hits the cancel button on the physical */
			/* control panel on the printer, it simply means the job will soon be cancelled by */
			/* the printer */
			write_status_file(PPS_JOB_CANCEL_REQUESTED);

			return PPS_JOB_CANCEL_REQUESTED;

		default:
			return PPS_OK;
	}
	/* */
}



int accept_ustatus_job(char *first_line)
{
	char ubuff[1024], tempbuff[1024];
	int x;
	int temppages;
	int job_completed = 0;	/* so we know after we receive an END message whether it was for the job we sent */


	/* get the second line, so we can find out what kind of USTATUS JOB message this is */
	if (readline(connection.sockfd, ubuff, 1023) == -1)
	{
		/* we lost our connection reading from the printer */
		#ifdef DEBUG
		sprintf(errstr, "%s/%s (pjl_jetdirect) (%d): problem reading USTATUS JOB message from printer",
			general.queue, general.printer, getpid());
		debug_log(errstr);
		#endif

		sprintf(errstr, "%s/%s (pjl_jetdirect) (%d): lost connection to printer '%s' on queue '%s' while printing job for '%s'",
			general.queue, general.printer, getpid(), general.printer, general.queue, general.username);
		error_log(errstr);

		return PPS_JOB_TIMEOUT;
		/* */
	}
	/* */

	/* are we at the start of a job */
	if (strcmp(ubuff, "START\r\n") == 0)
	{
		sprintf(tempbuff, "NAME=\"%d\"\r\n", general.job_id);	/* we are looking for messages pertaining to this job */

		/* the only line coming should be the name of the job that started, and then a form-feed, but just in */
		/* case they ever change things, we'll ignore anything else rather than gag on unexpected input */
		do
		{
			/* read a line from the printer */
			if (readline(connection.sockfd, ubuff, 1023) == -1)
			{
				/* we lost our connection reading from the printer */
				#ifdef DEBUG
				sprintf(errstr, "%s/%s (pjl_jetdirect) (%d): problem reading USTATUS JOB message from printer",
					general.queue, general.printer, getpid());
				debug_log(errstr);
				#endif

				sprintf(errstr, "%s/%s (pjl_jetdirect) (%d): lost connection to printer '%s' on queue '%s' while printing job for '%s'",
					general.queue, general.printer, getpid(), general.printer, general.queue, general.username);
				error_log(errstr);

				return PPS_JOB_TIMEOUT;
				/* */
			}
			/* */

			/* check for the confirmation of the name of this job as the one starting */
			if (strcmp(ubuff, tempbuff) == 0)
			{
				#ifdef UNSOLICITED_DEBUG
				sprintf(errstr, "%s/%s (pjl_jetdirect) (%d): our job (%s) has started printing", general.queue, general.printer, getpid(), ubuff);
				debug_log(errstr);
				#endif

				write_status_file(PPS_PRINTING);
			}
			else
			{
				if (strncmp(ubuff, "NAME=", 5) == 0)
				{
					/* NOTE:  One way to attempt to circumvent the page counting methods of a program such as this would be to try to */
					/*        embed a PJL print job within the print job sent to the printer by this program.  It may be possible then */
					/*        to cause the printer to reset its page count, thus defeating a page counting system.  This technique will */
					/*        probably always be ineffective with this system, but its presence should nonetheless be noted.  It is of */
					/*        course very possible that the print drivers for the client being used will automatically embed PJL JOB */
					/*        markers in the code sent to the printer.  Thus, a message such as this does not necessarily mean that */
					/*        someone is attempting to cheat the system.  Still, it should be noticed, so that hopefully this driver */
					/*        can be updated to handle it, if possible. */
					#ifdef DEBUG
					sprintf(errstr, "%s/%s (pjl_jetdirect) (%d): *** RECEIVED A USTATUS JOB START FOR AN UNKNOWN JOB ***",
						general.queue, general.printer, getpid());
					#endif

					sprintf(errstr, "%s/%s (pjl_jetdirect) (%d): WARNING - recieved an unexpected JOB START message from printer while printing\
job for '%s' to printer '%s' on '%s'", general.queue, general.printer, getpid(), general.username, general.printer, general.queue);
					error_log(errstr);
				}
			}
			/* */
		}
		while (ubuff[0] != '\f');
		/* */

		return PPS_OK;
	}
	/* */

	/* are we at the end of the job */
	if (strcmp(ubuff, "END\r\n") == 0)
	{
		sprintf(tempbuff, "NAME=\"%d\"\r\n", general.job_id);	/* we are looking for messages pertaining to this job */

		/* the only two lines coming should be the name of the job and the number of pages printed, and then a form-feed, */
		/* but just in case they ever change things, we'll ignore anything else rather than gag on unexpected input */
		do
		{
			/* read a line from the printer */
			if (readline(connection.sockfd, ubuff, 1023) == -1)
			{
				/* we lost our connection reading from the printer */
				#ifdef DEBUG
				sprintf(errstr, "%s/%s (pjl_jetdirect) (%d): problem reading USTATUS JOB message from printer",
					general.queue, general.printer, getpid());
				debug_log(errstr);
				#endif

				sprintf(errstr, "%s/%s (pjl_jetdirect) (%d): lost connection to printer '%s' on queue '%s' while printing job for '%s'",
					general.queue, general.printer, getpid(), general.printer, general.queue, general.username);
				error_log(errstr);

				return PPS_JOB_TIMEOUT;
				/* */
			}
			/* */

			/* check for the confirmation of the name of this job as the one ending */
			if (strcmp(ubuff, tempbuff) == 0)
			{
				/* so we know the job that we started is the one that has ended */
				job_completed = 1;
				/* */

				#ifdef UNSOLICITED_DEBUG
				tempbuff[strlen(tempbuff)-2] = '\0';
				sprintf(errstr, "%s/%s (pjl_jetdirect) (%d): our job (%s) has finished printing",
					general.queue, general.printer, getpid(), ubuff);
				debug_log(errstr);
				#endif
			}
			else
			{
				if (strncmp(ubuff, "NAME=", 5) == 0)
				{
					/* NOTE:  One way to attempt to circumvent the page counting methods of a program such as this would be to try to */
					/*        embed a PJL print job within the print job sent to the printer by this program.  It may be possible then */
					/*        to cause the printer to reset its page count, thus defeating a page counting system.  This technique will */
					/*        probably always be ineffective with this system, but its presence should nonetheless be noted.  It is of */
					/*        course very possible that the print drivers for the client being used will automatically embed PJL JOB */
					/*        markers in the code sent to the printer.  Thus, a message such as this does not necessarily mean that */
					/*        someone is attempting to cheat the system.  Still, it should be noticed, so that hopefully this driver */
					/*        can be updated to handle it, if possible. */
					#ifdef DEBUG
					sprintf(errstr, "%s/%s (pjl_jetdirect) (%d): *** RECEIVED A USTATUS JOB END FOR AN UNKNOWN JOB ***",
						general.queue, general.printer, getpid());
					debug_log(errstr);
					#endif

					sprintf(errstr, "%s/%s (pjl_jetdirect) (%d): WARNING - recieved an unexpected JOB END message from printer while printing\
 job for '%s' to printer '%s' on '%s'", general.queue, general.printer, getpid(), general.username, general.printer, general.queue);
					error_log(errstr);
				}
			}
			/* */

			/* check for the number of pages printed by this job */
			if (strncmp(ubuff, "PAGES=", 6) == 0)
			{
				sscanf(ubuff+6, "%d", &temppages);
				lj4.job_page_total += temppages;

				#if defined(UNSOLICITED_DEBUG) || defined(PAGE_DEBUG)
				sprintf(errstr, "%s/%s (pjl_jetdirect) (%d): %d pages were printed by this job, for a total of %d pages",
					general.queue, general.printer, getpid(), temppages, lj4.job_page_total);
				debug_log(errstr);
				#endif
			}
			/* */
		}
		while (ubuff[0] != '\f');
		/* */

		/* return a value based on whether our job we sent has completed or not */
		if (job_completed)
			return PPS_JOB_PRINTED;
		else
			return PPS_OK;
		/* */
	}
	/* */

	/* was the job just cancelled */
	if (strcmp(ubuff, "CANCELED\r\n") == 0)
	{
		#ifdef UNSOLICITED_DEBUG
		sprintf(errstr, "%s/%s (pjl_jetdirect) (%d): job has been cancelled by user", general.queue, general.printer, getpid());
		debug_log(errstr);
		#endif

		/* here some lines of code are going to be sent to us, but we don't really care about them; the */
		/* job has been cancelled, so we're going to close the connection and be done with it, the */
		/* details don't matter */
		read_to_formfeed();

		write_status_file(PPS_JOB_CANCEL_REQUESTED);

		return PPS_JOB_CANCELLED;
	}
	/* */

	/* if we are here, there is a USTATUS JOB message that we are not taking care of... */
	#ifdef UNSOLICITED_DEBUG
	sprintf(errstr, "%s/%s (pjl_jetdirect) (%d): received an unrecognized '%s' USTATUS JOB message",
		general.queue, general.printer, getpid(), ubuff);
	debug_log(errstr);
	#endif

	return PPS_OK;
	/* */
}



/* request current status from printer, store it, and write it to status file */
int get_printer_status()
{
	char sendbuff[1024], recvbuff[1024];
	int x;


	/*** enter PJL mode, request printer status, and accept printer's reponse ***/

	/* send request for printer status */
	sprintf(sendbuff, "\033%%-12345X@PJL\r\n@PJL INFO STATUS\r\n\033%%-12345X");

	#ifdef STATUS_DEBUG
	sprintf(errstr, "%s/%s (pjl_jetdirect) (%d): requesting printer device status", general.queue, general.printer, getpid());
	debug_log(errstr);
	#endif
	if (writen(connection.sockfd, sendbuff, strlen(sendbuff)) < strlen(sendbuff))
	{
		#ifdef DEBUG
		sprintf(errstr, "%s/%s (pjl_jetdirect) (%d): problem writing to socket while requesting printer status before sending job",
			general.queue, general.printer, getpid());
		debug_log(errstr);
		#endif

		close(connection.sockfd);

		write_status_file(PPS_CONNECT_TIMEOUT);

		return PPS_JOB_TIMEOUT;
	}
	/* */

	/* wait for response to our printer status request */
	#ifdef STATUS_DEBUG
	sprintf(errstr, "%s/%s (pjl_jetdirect) (%d): waiting for response to printer device status request",
		general.queue, general.printer, getpid());
	debug_log(errstr);
	#endif
	for(;;)
	{
		/* read a line and see if it is the beginning of the response we are looking for */
		if (readline(connection.sockfd, recvbuff, 1023) == -1)
		{
			#ifdef DEBUG
			sprintf(errstr, "%s/%s (pjl_jetdirect) (%d): timed out waiting for response to an INFO STATUS request",
				general.queue, general.printer, getpid());
			debug_log(errstr);
			#endif

			return -1;
		}
		else
		{
			if (strncmp(recvbuff, "@PJL INFO STATUS\r\n", 18) == 0)
			{
				/* this is the message we are looking for */
				break;
				/* */

			}
		}
	}
	/* */

	/* read the response we got from the printer */
	do
	{
		if (readline(connection.sockfd, recvbuff, 1023) == -1)
		{
			#ifdef DEBUG
			sprintf(errstr, "%s/%s (pjl_jetdirect) (%d): timed out reading response to an INFO STATUS request",
				general.queue, general.printer, getpid());
			debug_log(errstr);
			#endif

			return -1;
		}
		else
		{
			/* check for the current display */
			if (strncmp(recvbuff, "DISPLAY=", 8) == 0)
			{
				/* grab the string between quotes */
				x=0;
				while (recvbuff[x+9] != '\"')
				{
					lj4.display[x] = recvbuff[x+9];
					x++;
				}
				lj4.display[x] = '\0';
				/* */

				#ifdef STATUS_DEBUG
				sprintf(errstr, "%s/%s (pjl_jetdirect) (%d): DISPLAY=\"%s\"", general.queue, general.printer, getpid(), lj4.display);
				debug_log(errstr);
				#endif
			}
			/* */

			/* check for printer on/offline status */
			if (strncmp(recvbuff, "ONLINE=", 7) == 0)
			{
				if (strncmp(recvbuff+7, "FALSE", 5) == 0)
				{
					#ifdef STATUS_DEBUG
					sprintf(errstr, "%s/%s (pjl_jetdirect) (%d): ONLINE=FALSE", general.queue, general.printer, getpid());
					debug_log(errstr);
					#endif

					lj4.online = PPS_OFFLINE;
				}
				else
				{
					#ifdef STATUS_DEBUG
					sprintf(errstr, "%s/%s (pjl_jetdirect) (%d): ONLINE=TRUE", general.queue, general.printer, getpid());
					debug_log(errstr);
					#endif

					lj4.online = PPS_ONLINE;
				}
			}
			/* */

			/* check the current status code */
			if (strncmp(recvbuff, "CODE=", 5) == 0)
			{
				/* read the code as an integer */
				sscanf(recvbuff+5, "%d", &lj4.ustatus_code);

				#ifdef STATUS_DEBUG
				sprintf(errstr, "%s/%s (pjl_jetdirect) (%d): CODE=%d", general.queue, general.printer, getpid(), lj4.ustatus_code);
				debug_log(errstr);
				#endif
				/* */
			}
			/* */
		}
	}
	while (recvbuff[0] != '\f');		/* message ends with a formfeed */
	#ifdef STATUS_DEBUG
	sprintf(errstr, "%s/%s (pjl_jetdirect) (%d): got printer device status", general.queue, general.printer, getpid());
	debug_log(errstr);
	#endif
	/* */

	return PPS_OK;
}



int accept_ustatus_page(char *first_line)
{
	char ubuff[1024];


	/* whether to count this page as printed or not... */
	/* it appears that sometimes after a reset, we can get a slew of page messages for pages that */
	/* have not actually been output by the printer, but merely processed... in any case, should */
	/* this ever happen, we will be ready */
	if (lj4.ustatus_code != PJL_CODE_RESET)
	{
		/* increment page count */
		lj4.single_page_total = lj4.single_page_total + 1;

		#ifdef PAGE_DEBUG
		sprintf(errstr, "%s/%s (pjl_jetdirect) (%d): we appear to have printed PAGE %d",
			general.queue, general.printer, getpid(), lj4.single_page_total);
		debug_log(errstr);
		#endif
	}
	else
	{
		/* NOTE:  when you reset an LJ4 from the control panel, while it is resetting, the printer */
		/*        sends a flood of USTATUS PAGE messages that are perhaps processed, but not actually */
		/*        output by the printer.  We want to ignore these, so as not to throw off our page count */
		#ifdef PAGE_DEBUG
		sprintf(errstr, "%s/%s (pjl_jetdirect) (%d): a page was processed which does not appear to have come out",
			general.queue, general.printer, getpid());
		debug_log(errstr);
		#endif
	}
	/* */

	/* right now we don't care what is sent to us */
	read_to_formfeed();
	/* */

	return PPS_OK;
}



void reset_printer_display()
{
	pid_t pid;
	struct timeval now_tv;
	int lock_status;
	char path[256];


	#ifdef RESET_DEBUG
	sprintf(errstr, "%s/%s (pjl_jetdirect) (%d): attempting to reset printer's display", general.queue, general.printer, getpid());
	debug_log(errstr);
	#endif

	/* fork a child to reset the printer's display, so the parent can return immediately */
	if ((pid = fork()) < 0)
	{
		#ifdef DEBUG
		sprintf(errstr, "%s/%s (pjl_jetdirect) (%d): error forking process to reset printer display",
			general.queue, general.printer, getpid());
		debug_log(errstr);
		#endif

		return;
	}
	/* */

	/* the parent process will return, so it can be collected immediately by the ppd daemon */
	if (pid != 0)
	{
		#ifdef RESET_DEBUG
		sprintf(errstr, "%s/%s (pjl_jetdirect) (%d): child forked to reset printer display - pid %d",
			general.queue, general.printer, getpid(), pid);
		debug_log(errstr);
		#endif

		return;
	}
	/* */

	/* reset the starting time for which we will begin attempting to reset the printer's display */
	gettimeofday(&connection.start_tv, NULL);
	/* */

	/* keep trying to reset dislay until we time out or we discover that the printer is in use again */
	for (;;)
	{
		if (connect_and_reset_printer() == PPS_OK)
		{
			#ifdef RESET_DEBUG
			sprintf(errstr, "%s/%s[reset] (pjl_jetdirect) (%d): finished resetting printer [%s] on queue [%s]'s display",
				general.queue, general.printer, getpid(), general.printer, general.queue);
			debug_log(errstr);
			#endif

			exit(0);
		}

		/* wait a little bit before we try to connect again */
		sleep(PJL_CONNECT_RETRY_INTERVAL);
		/* */

		/* check to see if the printer we are trying to reset is active again... we only want */
		/* to keep trying to reset the display as long as the printer is idle */
		sprintf(path, "%s/spool/%d/.%d", ROOT, general.queue_idx, general.printer_idx);
		if (open_and_test_lock(path, &lock_status) == -1)
		{
			#ifdef RESET_DEBUG
			sprintf(errstr, "%s/%s[reset] (pjl_jetdirect) (%d): error testing file lock while resetting display, giving up",
				general.queue, general.printer, getpid());
			debug_log(errstr);
			#endif

			exit(0);
		}
		if (lock_status == F_WRLCK)
		{
			#ifdef RESET_DEBUG
			sprintf(errstr, "%s/%s[reset] (pjl_jetdirect) (%d): printer became active, giving up",
				general.queue, general.printer, getpid());
			debug_log(errstr);
			#endif

			exit(0);
		}
		/* */

		/* see if we should keep trying, or if we have tried for long enough already */
		if (PJL_PRINTER_RESET_TIMEOUT != 0)
		{
			gettimeofday(&now_tv, NULL);
			if (now_tv.tv_sec >= (connection.start_tv.tv_sec + (PJL_PRINTER_RESET_TIMEOUT * 60)))
			{
				#ifdef RESET_DEBUG
				sprintf(errstr, "%s/%s[reset] (pjl_jetdirect) (%d): timed out trying to reset printer, too bad",
					general.queue, general.printer, getpid());
				debug_log(errstr);
				#endif

				exit(0);
			}
		}
		/* */
	}
	/* */
}



int connect_and_reset_printer()
{
	struct sigaction action;
	struct sockaddr_in serv_addr;
	time_t t;
	char sendbuff[1024], recvbuff[1024], timestamp[26], synchbuff[256], queuebuff[33];
	int len, i;
	

	/* catch these signals should they occur during communication with printer */
	connection.timed_out = FALSE;

	action.sa_handler = signal_handler;
	sigemptyset(&action.sa_mask);
	action.sa_flags = 0;
	if (sigaction(SIGPIPE, &action, NULL) < 0)
		exit(0);

	action.sa_handler = signal_handler;
	sigemptyset(&action.sa_mask);
	action.sa_flags = 0;
	if (sigaction(SIGALRM, &action, NULL) < 0)
		exit(0);
	/* */

	/*** make the network connection to the printer ***/

	/* open a TCP socket (an Internet stream socket) */
	if ((connection.sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
	{
		sprintf(errstr, "ppd/queue/printer(pjl_jetdirect) (%d): cannot allocate stream socket to reset display of JetDirect in printer '%s' attached to queue '%s' at '%s'",
			getpid(), general.printer, general.queue, general.address);
		error_log(errstr);
		#ifdef DEBUG
		debug_log(errstr);
		#endif

		return PPS_ERROR;
	}
	/* */

	/* fill in the structure "serv_addr" with the address of the server that we want to connect to */
	bzero((char *)&serv_addr, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = connection.address;
	serv_addr.sin_port = htons(connection.port);
	/* */

	/* attempt to connect to the remote printer */
	#ifdef RESET_DEBUG
	sprintf(errstr, "%s/%s[reset] (pjl_jetdirect) (%d): attempting to connect to printer to reset display",
		general.queue, general.printer, getpid());
	debug_log(errstr);
	#endif
	if (connect(connection.sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
	{
		/* connection to remote printer failed for some reason */
		#ifdef RESET_DEBUG
		sprintf(errstr, "%s/%s[reset] (pjl_jetdirect) (%d): attempt to connect to printer to reset failed - %s",
			general.queue, general.printer, getpid(), strerror(errno));
		debug_log(errstr);
		#endif

		close(connection.sockfd);

		/* (write out) and return connection status */
		switch(errno)
		{
			case 111:	/* connection refused */
				#ifdef RESET_DEBUG
				sprintf(errstr, "%s/%s[reset] (pjl_jetdirect) (%d): connection refused",
					general.queue, general.printer, getpid());
				debug_log(errstr);
				#endif
				return PPS_CONNECTION_REFUSED;
			case 113:
				#ifdef RESET_DEBUG
				sprintf(errstr, "%s/%s[reset] (pjl_jetdirect) (%d): no route to host",
					general.queue, general.printer, getpid());
				debug_log(errstr);
				#endif
				return PPS_NO_ROUTE;
			default:
				return PPS_ERROR;
		}
		/* */
	}
	else
	{
		/*** attempt to reset display of printer ***/

		/* enter PJL mode, send RDYMSG DISPLAY, RESET and ECHO commands, and exit PJL */
		#ifdef RESET_DEBUG
		sprintf(errstr, "%s/%s[reset] (pjl_jetdirect) (%d): connected to printer",
			general.queue, general.printer, getpid());
		debug_log(errstr);
		sprintf(errstr, "%s/%s[reset] (pjl_jetdirect) (%d): sending RDYMSG DISPLAY, RESET and ECHO commands",
			general.queue, general.printer, getpid());
		debug_log(errstr);
		#endif
		t = time(NULL);
		sprintf(timestamp, ctime(&t));
		timestamp[24] = '\0';
		for(i=0; i<strlen(general.queue); i++)
			queuebuff[i] = toupper(general.queue[i]);
		queuebuff[i] = '\0';
		sprintf(sendbuff, "\033%%-12345X@PJL\r\n@PJL RDYMSG DISPLAY=\"%s READY\"\r\n@PJL RESET\r\n@PJL ECHO %s\r\n\033%%-12345X",
			queuebuff, timestamp);
		if (writen(connection.sockfd, sendbuff, strlen(sendbuff)) < strlen(sendbuff))
		{
			#ifdef RESET_DEBUG
			sprintf(errstr, "%s/%s[reset] (pjl_jetdirect) (%d): problem writing to socket to reset printer display",
				general.queue, general.printer, getpid());
			debug_log(errstr);
			#endif

			close(connection.sockfd);

			return PPS_CONNECT_TIMEOUT;
		}
		else
		{
			#ifdef RESET_DEBUG
			sprintf(errstr, "%s/%s[reset] (pjl_jetdirect) (%d): sent RDYMSG DISPLAY, RESET and ECHO commands",
				general.queue, general.printer, getpid());
			debug_log(errstr);
			#endif

			/*** wait for response from ECHO command (synchronization message) ***/
			sprintf(synchbuff, "@PJL ECHO %s\r\n", timestamp);
			for(;;)
			{
				if (len = readline(connection.sockfd, recvbuff, sizeof(recvbuff)) == -1)
				{
					#ifdef RESET_DEBUG
					sprintf(errstr, "%s/%s[reset] (pjl_jetdirect) (%d): problem reading from socket while waiting for synch response",
						general.queue, general.printer, getpid());
					debug_log(errstr);
					#endif

					close(connection.sockfd);

					return PPS_CONNECT_TIMEOUT;
				}

				if (strcmp(recvbuff, synchbuff) == 0)
				{
					#ifdef RESET_DEBUG
					sprintf(errstr, "%s/%s[reset] (pjl_jetdirect) (%d): received response to synchronization message, printer reset",
						general.queue, general.printer, getpid());
					debug_log(errstr);
					#endif

					close(connection.sockfd);

					return PPS_OK;
				}
			}
			/*** ***/
		}
		/* */
	}
	/* */
}



int write_status_file(int status)
{
	struct printer_status printer_status;


	/* set printer status to be written */
	printer_status.jobnum = general.job_id;
	printer_status.status = status;
	/* */

	/* write the current status of the printer out to its lock file */
	if (lseek(general.status_fd, 0, SEEK_SET) == -1)
	{
		sprintf(errstr, "ppd/queue/printer(pjl_jetdirect) (%d): error seeking in lockfile of printer '%s' attached to queue '%s' - [%s]",
			getpid(), general.printer, general.queue, strerror(errno));
		error_log(errstr);

		return PPS_ERROR;
	}
	if (write(general.status_fd, &printer_status, sizeof(struct printer_status)) == -1)
	{
		sprintf(errstr, "ppd/queue/printer(pjl_jetdirect) (%d): error writing to lockfile of printer '%s' attached to queue '%s'",
			getpid(), general.printer, general.queue);
		error_log(errstr);

		return PPS_ERROR;
	}
	/* */

	return PPS_OK;
}



void signal_handler(int type)
{
	switch (type)
	{
		case SIGALRM:
			#ifdef DEBUG
			sprintf(errstr, " *** %s/%s (pjl_jetdirect) (%d) : SIGALRM caught *** ",
				general.queue, general.printer, getpid());
			debug_log(errstr);
			#endif
			break;
		case SIGPIPE:
			#ifdef DEBUG
			sprintf(errstr, " *** %s/%s (pjl_jetdirect) (%d) : SIGPIPE caught *** ",
				general.queue, general.printer, getpid());
			debug_log(errstr);
			#endif
			break;
	}

	connection.timed_out = TRUE;
}



int writen(int fd, char *ptr, int nbytes)
{
	int nleft, nwritten;


	nleft = nbytes;
	while (nleft > 0)
	{
		/* trigger a SIGALRM to go off in 30 seconds, during which time we try to write to */
		/* the printer. */
		/* NOTE:  Some unixes will restart system calls on interruption.  If the write() call */
		/*        here is restarted upon interruption, then our alarm will have no effect     */
		/*        and this will in effect remain a blocking i/o function.  Linux seems fine,  */
		/*        but this bit of code will be very critical if ported to another system!     */
		alarm(PJL_INACTIVITY_TIMEOUT);
		nwritten = write(fd, ptr, nleft);
		alarm(0);
		if (nwritten <= 0)
		{
			break;
		}
		nleft -= nwritten;
		ptr += nwritten;
	}

	if (connection.timed_out)
		return -1;
	else
		return (nbytes - nleft);
}



int readline(int fd, char *ptr, int maxlen)
{
	int n, rc;
	char c;
	fd_set rfds;
	struct timeval tv;
	int retval;
	char pjl_string[10], *sofar;
	int len=0;


	#if defined(READ_DEBUG) && defined(DEBUG_STDERR)
	fprintf(stderr, "%s/%s (pjl_jetdirect) (%d): readline read [", general.queue, general.printer, getpid());
	#endif

	/* for some reason, the printer sends us this on its own... and we want to ignore it */
	sprintf(pjl_string, "%c%%-12345X", 27);
	sofar = ptr;
	/* */

	for (n=1; n < maxlen; n++)
	{
		/* stop trying to read if our connection was lost due to SIGPIPE */
		if (connection.timed_out)
			return -1;
		/* */

		/* wait for data from printer */
		FD_ZERO(&rfds);
		FD_SET(fd, &rfds);
		tv.tv_sec = PJL_INACTIVITY_TIMEOUT;
		tv.tv_usec = 0;
		retval = select(fd+1, &rfds, NULL, NULL, &tv);
		if (retval == 0)
			return -1;
		/* */

		alarm(PJL_INACTIVITY_TIMEOUT);
		if ((rc = read(fd, &c, 1)) == 1)
		{
			#if defined(READ_DEBUG) && defined(DEBUG_STDERR)
			switch(c)
			{
				case '\f':
					fprintf(stderr, "<FF>");
					break;
				case '\r':
					fprintf(stderr, "<CR>");
					break;
				case '\n':
					fprintf(stderr, "<LF>");
					break;
				case 4:
					fprintf(stderr, "<!EOT>");
					break;
				case 27:
					fprintf(stderr, "<ESC>");
					break;
				default:
					fputc(c, stderr);
					break;
			}
			#endif

			alarm(0);

			/* we want to ignore EOT's...  there's nothing about this in the manual...? */
			if (c != 4)
			{
				*ptr++ = c;
				len++;
			}
			/* */

			/* are we at the end of the message yet */
			if ((c == '\n') || (c == '\f') || (strncmp(pjl_string, sofar, 9) == 0))
			{
				#if defined(READ_DEBUG) && defined(DEBUG_STDERR)
				fprintf(stderr, "] : %d\n", len);
				#endif

				break;
			}
			/* */
		}
		else
		{
			alarm(0);
			if (rc == 0)
			{
				if (n==1)
					return 0;	/* EOF, no data read */
				else
					break;		/* EOF, some data read */
			}
			else
			{
				return -1;	/* error */
			}
		}
	}

	*ptr = 0;

	#ifdef READ_DEBUG
	sprintf(errstr, "%s/%s (pjl_jetdirect) (%d): readline read [%s]", general.queue, general.printer, getpid(), ptr-len);
	debug_log(errstr);
	#endif

	return len;
}




int read_to_formfeed()
{
	int rc;
	char c;
	fd_set rfds;
	struct timeval tv;
	int retval;


	/* just keep reading characters from the file until we reach a formfeed */
	do
	{
		/* stop trying to read if our connection was lost due to SIGPIPE */
		if (connection.timed_out)
			return -1;
		/* */

		/* wait for data from printer */
		FD_ZERO(&rfds);
		FD_SET(connection.sockfd, &rfds);
		tv.tv_sec = PJL_INACTIVITY_TIMEOUT;
		tv.tv_usec = 0;
		retval = select(connection.sockfd+1, &rfds, NULL, NULL, &tv);
		if (retval == 0)
			return -1;
		/* */

		alarm(PJL_INACTIVITY_TIMEOUT);
		rc = read(connection.sockfd, &c, 1);
		alarm(0);
		#if defined(READ_DEBUG) && defined(DEBUG_STDERR)
		if (c == '\f')
			fprintf(stderr, "<FF>");
		else
			fputc(c, stderr);
		#endif
		if (rc == 0)		/* EOF */
			return;
		if (rc == -1)
		{
			#ifdef DEBUG
			sprintf(errstr, "%s/%s (pjl_jetdirect) (%d): error reading to formfeed", general.queue, general.printer, getpid());
			debug_log(errstr);
			#endif
			return;
		}
	}
	while (c != '\f');
	/* */

	return 0;
}
