/*
 *  pps.h
 *
 *  (c)1997,1998 Paul Grebenc
 *
 *  $Header: /usr/local/PPS_RCS/src/pps.h,v 1.1 1999/12/10 03:33:47 paul Exp $
 */


#ifndef PPS_H
#define PPS_H

#include <stdio.h>
#include <sys/types.h>

#ifndef ERROR
#define ERROR	-1
#endif
#ifndef OK
#define OK	0
#endif
#ifndef TRUE
#define TRUE	1
#endif
#ifndef FALSE
#define FALSE	0
#endif

/* a new attempt at standardized return codes and status values */
#define PPS_ERROR			-1
#define PPS_OK				0
#define PPS_JOB_PRINTED			1
#define PPS_JOB_CANCELLED		2
#define PPS_JOB_TIMEOUT			3
#define PPS_JOB_FAILED			4
#define PPS_PRINTER_RESET		5
#define PPS_PRINTING			6
#define PPS_PAPER_JAM			7
#define PPS_NO_PAPER			8
#define PPS_OFFLINE			9
#define PPS_ONLINE			10
#define PPS_STARTING			11
#define PPS_CONNECTING			12
#define PPS_CONNECTION_REFUSED		13
#define PPS_NO_ROUTE			14
#define PPS_CONNECT_TIMEOUT		15
#define PPS_LOW_BALANCE			16
#define PPS_PRINTER_IDLE		17
#define PPS_JOB_SUBMITTED		18
#define PPS_JOB_CANCEL_REQUESTED	19
#define PPS_RESETTING_PRINTER		20
#define PPS_SENDING_JOB			21
/* */

/* constants for limits on the system's size */
#define MAX_QUEUES		32
#define MAX_PRINTERS_PER_QUEUE	8
#define MAX_PRINTERS MAX_QUEUES * MAX_PRINTERS_PER_QUEUE
#define QUEUE_LENGTH		10
/* */

/* printer types */
#define PPS_PRINTER_GENERIC		1
#define PPS_PRINTER_PJL_JETDIRECT	2
/* */

/* queue charge methods */
#define PPS_CHARGE_NONE		1
#define PPS_CHARGE_PAGE		2
#define PPS_CHARGE_JOB		3
/* */

/* user account balance restrictions */
#define PPS_ACCOUNTING_INACTIVE		0
#define PPS_ACCOUNTING_ACTIVE		1
/* */

/* printer/queue status values */
#define	DOWN		0
#define	UP		1
#define	ACTIVE		2
#define INACTIVE	3
/* */


/***** structure of queue/printer setup stored in the MSQL database *****/

struct pps_queue
{
	int type;
	float charge;
	char name[33];
	char description[65];
	int status;
};

struct pjl_jetdirect		/* the following structures contain information necessary to */
{				/* allow the system to connect to the physical printing device */
	char address[256];
	int port;
};

union model_specific				/* a conglameration of pertinent addressing */
{						/* information for each possible printer type */
	struct pjl_jetdirect pjl_jetdirect;
};

struct printer
{
	int type;
	char name[33];
	char description[65];
	union model_specific specific;
	int queue;
	int status;
};

/***** *****/


/***** structure used for maintaining a list of printers and their status for the queue server *****/

struct eligible_printer
{
	int keyval;
	pid_t pid;
	int jobnum;
	FILE *inf_fp;
	FILE *dat_fp;
	int status;
	int delete_printer;
	int lock_fd;
};

struct printer_status	/* contenets of spool/<queue>/.<printer> file */
{
	int jobnum;	/* job currently printing on this printer */
	int status;	/* current status of job (print, offline, etc.) */
};

/***** *****/



/* structure of contents of inf control files */

struct inf
{
/*	char username[9];*/
	int userid;
	char filename[256];
};


/* queue manager message codes (to be moved to a separate include file later) */

#define QM_ANNOUNCE			1
#define QM_JOB_SUBMITTED		2
#define QM_JOB_DELETED			3
#define QM_JOB_DELETE_REQUESTED		4
#define QM_JOB_PRINTED			5
#define QM_PRINTER_IDLE			6
#define QM_PRINTING			7
#define QM_ERROR			8
#define QM_CONNECTION_LOST		9


/* structures for queue manager messages (also to be moved to same file as above later) */

struct qm_job_submitted
{
	int keyval;
	int count;
	struct inf inf;
};

struct qm_job_delete
{
	int keyval;
	int count;
	struct inf inf;
	time_t time;
};

struct qm_job_deleted
{
	char queue[32];
	int count;
	struct inf inf;
	time_t time;
};

struct qm_job_printed
{
	char queue[32];
	int count;
	struct inf inf;
	time_t time;
	int pagecount;
	float charge;
	float balance;
};


/* container structure for queue of commands */

#define COMMAND_LENGTH 1024	/* no command file can be great than 1K */

struct command
{
	pid_t pid;
	int command_code;
	char mesg[COMMAND_LENGTH];
};

struct command_node
{
	struct command command;
	struct command_node *next;
};


/* command codes sent to ppd */

#define PPD_JOB_DELETE		1


/* structures for ppd command code messages */

struct ppd_job_delete
{
	int count;
};

#endif

