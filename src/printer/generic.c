/*
 *  generic.c
 *
 *  This is a do-nothing virtual printer driver, used to exhibit a framework by which new
 *  print drivers can be constructed.
 *
 *  (c)1998 Paul Grebenc
 *
 *  $Header: /usr/local/PPS_RCS/src/printer/generic.c,v 1.1 1999/12/10 03:26:05 paul Exp $
 */


#include <config.h>
#include <sys/types.h>
#include <unistd.h>

#include "pps.h"


extern int delete_job;

void notify_queue_manager_deleted(int id, struct inf *inf);


int print_to_generic(struct inf *inf, struct eligible_printer *job_entry, int *pages);


int print_to_generic(struct inf *inf, struct eligible_printer *job_entry, int *pages)
{
	int x;
	struct printer_status printer_status;

	/* start with a delay so we can check our status updating */
	sleep(2);

	/* let's start off by letting the world know we're printing */
	printer_status.jobnum = job_entry->jobnum;
	printer_status.status = PPS_PRINTING;

	lseek(job_entry->lock_fd, 0, SEEK_SET);
	write(job_entry->lock_fd, &printer_status, sizeof(struct printer_status));
	/* */

	/* just do some waiting in a loop to simulate printing */
	for (x=0; x<10; x++)
	{
		if (delete_job == 1)
		{
			printf("Aborting job %d\n", job_entry->jobnum);
			notify_queue_manager_deleted(job_entry->jobnum, inf);
			exit(QM_JOB_DELETED);
		}

		sleep(1);
	}
	*pages = 7;
	/* */

	return 0;
}

