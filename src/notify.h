/*
 *  notify.h
 *
 *  Functions to provide for notification (communication) of events throughout the system.
 *
 *  (c)1997,1998 Paul Grebenc
 *
 *  $Header: /usr/local/PPS_RCS/src/notify.h,v 1.1 1999/12/10 03:35:34 paul Exp $
 */


#ifndef LOGGING_H
#define LOGGING_H

void notify_parent_job_queued(int keyval);
void notify_parent_reload_database(void);
void notify_queue_reload_database(int keyval);
void notify_queue_job_sent(int keyval);

#endif

