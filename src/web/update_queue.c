/*  update_queue.c
 *
 *  Take the form submitted by a user, parse the data, and update the configuratin of a queue.
 *
 *  (c)1998 Paul Grebenc
 *
 *  $Header: /usr/local/PPS_RCS/src/web/update_queue.c,v 1.1 1999/12/10 03:30:02 paul Exp $
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <values.h>
#include <errno.h>

#include "pps.h"
#include "database.h"
#include "locking.h"
#include "parse.h"

#define UPDATE		1
#define DELETE		2
#define ARG_ERROR	3
#define MSQL_ERROR	4
#define QUEUE_EXISTS	5
#define OKAY		6

struct record
{
	struct pps_queue queue;
	int keyval;
};

int main();
int examine_posted(struct record *record);
int lookup_name(char *name, int keyval);
void delete_queue(struct record *record);
void update_queue(struct record *record);


main()
{
	struct record record;


	/* get the data posted to us from the update form, and do with it what is required */
	switch (examine_posted(&record))
	{
		case UPDATE:
			update_queue(&record);
			exit(0);
		case DELETE:
			delete_queue(&record);
			exit(0);
		case ARG_ERROR:
			printf("Content-type:  text/html\n\n");
			printf("<HTML>\n<HEAD><TITLE>PPS - Queue Update Error</TITLE></HEAD>\n\n");
			printf("<BODY TEXT=\"#FFFFFF\" BGCOLOR=\"#000080\">");
			printf("<CENTER>\n<FONT SIZE=+2>DISPLAY/UPDATE QUEUE INFORMATION</FONT><BR><BR>\n");
			printf("Error with submitted arguments.<BR>Queue not modified.<BR><BR>\n");
			printf("Notify administrator.\n</CENTER>\n");

			printf("<FORM METHOD=POST ACTION=show_setup.cgi>\n");
			printf("<CENTER><INPUT TYPE=SUBMIT NAME=ACTION VALUE=OK></CENTER>\n");
			printf("</FORM>\n");

			printf("</BODY>\n</HTML>\n");

			exit(0);
	}
	/* */
}



int examine_posted(struct record *record)
{
	int action;
	char errtxt[4096];
	int notify_admin = 0;
	int lookup_result;


	/* parse all posted variables */
	if ((parse_all() == -1)
	|| (getenv("CGI_name") == NULL)
	|| (getenv("CGI_description") == NULL)
	|| (getenv("CGI_type") == NULL)
	|| (getenv("CGI_charge") == NULL)
	|| (getenv("CGI_status") == NULL)
	|| (getenv("CGI_keyval") == NULL))
	{
		printf("Content-type:  text/html\n\n");
		printf("<HTML>\n<HEAD><TITLE>PPS - Queue Update Error</TITLE></HEAD>\n\n");
		printf("<BODY TEXT=\"#FFFFFF\" BGCOLOR=\"#000080\">");
		printf("<CENTER>\n<FONT SIZE=+2>DISPLAY/UPDATE QUEUE INFORMATION</FONT><BR><BR>\n");
		printf("Error with submitted arguments.<BR>Queue not modified.<BR><BR>\n");
		printf("Notify administrator.\n</CENTER>\n");

		printf("<FORM METHOD=POST ACTION=show_setup.cgi>\n");
		printf("<CENTER><INPUT TYPE=SUBMIT NAME=ACTION VALUE=OK></CENTER>\n");
		printf("</FORM>\n");

		printf("</BODY>\n</HTML>\n");

		exit(0);
	}
	/* */

	/* get posted data from environment variables */
	strncpy(record->queue.name, getenv("CGI_name"), sizeof(record->queue.name));
	record->queue.name[sizeof(record->queue.name)-1] = '\0';
	strncpy(record->queue.description, getenv("CGI_description"), sizeof(record->queue.description));
	record->queue.description[sizeof(record->queue.description)-1] = '\0';
	if ((sscanf(getenv("CGI_type"), "%d", &record->queue.type) == 0))
		record->queue.type = -1;
	if (strlen(getenv("CGI_charge")) == 0)
		record->queue.charge = -MAXFLOAT;
	else
	{
		if (sscanf(getenv("CGI_charge"), "%f", &record->queue.charge) == 0)
			record->queue.charge = -MAXFLOAT;
	}
	if ((sscanf(getenv("CGI_status"), "%d", &record->queue.status) == 0))
		record->queue.status = -1;
	if ((sscanf(getenv("CGI_keyval"), "%d", &record->keyval) == 0))
		record->keyval = -1;
	if (strcmp("UPDATE QUEUE", getenv("CGI_ACTION")) == 0)
		action = UPDATE;
	else
	{
		if (strcmp("DELETE QUEUE", getenv("CGI_ACTION")) == 0)
			action = DELETE;
		else
			action = -1;
	}
	/* */

	/* check first if we are deleting this queue, because if we are we don't really need to worry about a lot of the fields sent to us */
	if ((action == DELETE) && (record->keyval != -1) && (record->queue.status != -1))
		return DELETE;
	/* */

	/* examine the data given to us, to ensure it is acceptable */
	errtxt[0] = '\0';
	if (strlen(record->queue.name) == 0)
		strcat(errtxt, "Name field must not be blank.<BR>\n");
	else
	{
		if ((strstr(record->queue.name, " ") != NULL) || (strstr(record->queue.name, "'") != NULL))
		{
			if (strstr(record->queue.name, " ") != NULL)
				strcat(errtxt, "Name field must not contain spaces.<BR>\n");
			if (strstr(record->queue.name, "'") != NULL)
				strcat(errtxt, "Name field must not contain apostrophes.<BR>\n");
		}
		else
		{
			/* we're only doing this lookup, notice, if there are no apostrophes in the queue name. */
			/* If there are, the name is invalid and cannot possibly exist (created by this system). */
			/* We cannot just let the check go anyway, because a string with unescaped single quotes */
			/* will cause an error if sent to mSQL as is */
			switch (lookup_name(record->queue.name, record->keyval))
			{
				case QUEUE_EXISTS:
					strcat(errtxt, "A queue by this name already exists.<BR>\n");
					break;
				case MSQL_ERROR:
					printf("Content-type:  text/html\n\n");
					printf("<HTML>\n<HEAD><TITLE>PPS - Queue Update Error</TITLE></HEAD>\n\n");
					printf("<BODY TEXT=\"#FFFFFF\" BGCOLOR=\"#000080\">");
					printf("<CENTER>\n<FONT SIZE=+2>DISPLAY/UPDATE QUEUE INFORMATION</FONT><BR><BR>\n");
					printf("Error communicating with MSQL server.<BR>Queue not modified.<BR><BR>\n");
					printf("Please notify administrator.\n</CENTER>\n");

					printf("<FORM METHOD=POST ACTION=show_setup.cgi>\n");
					printf("<CENTER><INPUT TYPE=SUBMIT NAME=ACTION VALUE=OK></CENTER>\n");
					printf("</FORM>\n");

					printf("</BODY>\n</HTML>\n");

					exit(0);
			}
		}
	}
	if (record->queue.type == -1)
	{
		strcat(errtxt, "Invalid queue type specifier submitted.<BR>\n");
		notify_admin = 1;
	}
	if (record->queue.charge == -MAXFLOAT)
		strcat(errtxt, "Invalid charge amount submitted.<BR>\n");
	if (record->queue.status == -1)
	{
		strcat(errtxt, "Invalid status selection submitted.<BR>\n");
		notify_admin = 1;
	}
	if (record->keyval == -1)
	{
		strcat(errtxt, "Invalid key value submitted.<BR>\n");
		notify_admin = 1;
	}
	if (action != UPDATE)
	{
		strcat(errtxt, "Invalid action requested.<BR>\n");
		notify_admin = 1;
	}

	if (strlen(errtxt) > 0)
	{
		printf("Content-type:  text/html\n\n");
		printf("<HTML><HEAD><TITLE>PPS - Queue Update Error</TITLE></HEAD>\n");
		printf("<BODY TEXT=#FFFFFF BGCOLOR=#000080>\n");
		printf("<CENTER><FONT SIZE=+2>DISPLAY/UPDATE QUEUE INFORMATION</FONT><BR><BR>\n");

		printf(errtxt);
		printf("<BR>Queue not modified.<BR>\n");
		if (notify_admin == 1)
			printf("<BR>Notify administrator.<BR>\n");
		printf("</CENTER><BR>\n");

		printf("<FORM METHOD=POST ACTION=show_setup.cgi>\n");
		printf("<CENTER><INPUT TYPE=SUBMIT NAME=ACTION VALUE=OK></CENTER>\n");
		printf("</FORM>\n");

		printf("</BODY>\n</HTML>\n");

		exit(0);
	}
	/* */

	return UPDATE;
}



int lookup_name(char *name, int keyval)
{
	int sock;
	m_result *result;
	m_row row;
	char string[1024];


	/* connect to server */
	sock = mconnect();
	if (sock == -1)
		return MSQL_ERROR;
	/* */

	/* check if a queue by this name already exists in the system */
	sprintf(string, "SELECT name FROM queues WHERE name='%s' AND keyval<>%d", name, keyval);
	result = send_query(sock, string);
	if (msql_err)
		return MSQL_ERROR;
	if (msqlNumRows(result) > 0)
	{
		mclose(sock);

		return QUEUE_EXISTS;
	}
	/* */

	mclose(sock);

	return OKAY;
}



void delete_queue(struct record *record)
{
	int sock;
	m_result *result;
	char string[1024];
	char path[1024];
	int lock_status;


	/* check first to see if this queue is currently in use */
	/* if the file is not locked, or doesn't exist at all, the queue is not currently active */
	sprintf(path, "%s/spool/%d/.dpid", ROOT, record->keyval);

	if (((open_and_test_lock(path, &lock_status)) == -1) && (errno != ENOENT))
	{
		/* the test of the lock failed, and it's NOT because the file doesn't exist */
		printf("Content-type:  text/html\n\n");
		printf("<HTML>\n<HEAD><TITLE>PPS - Queue Update Error</TITLE></HEAD>\n\n");
		printf("<BODY TEXT=\"#FFFFFF\" BGCOLOR=\"#000080\">");
		printf("<CENTER>\n<FONT SIZE=+2>DISPLAY/UPDATE QUEUE INFORMATION</FONT><BR><BR>\n");
		printf("Error checking current status of queue.<BR>Queue not deleted.<BR><BR>\n");
		printf("Please notify administrator.\n</CENTER>\n");

		printf("<FORM METHOD=POST ACTION=show_setup.cgi>\n");
		printf("<CENTER><INPUT TYPE=SUBMIT NAME=ACTION VALUE=OK></CENTER>\n");
		printf("</FORM>\n");

		printf("</BODY>\n</HTML>\n");
		/* */

		exit(0);
	}
	/* */

	/* if this file is locked, the queue is currently printing something */
	if (lock_status == F_WRLCK)
	{
		printf("Content-type:  text/html\n\n");
		printf("<HTML>\n<HEAD><TITLE>PPS - Queue Update Error</TITLE></HEAD>\n\n");
		printf("<BODY TEXT=\"#FFFFFF\" BGCOLOR=\"#000080\">");
		printf("<CENTER>\n<FONT SIZE=+2>DISPLAY/UPDATE QUEUE INFORMATION</FONT><BR><BR>\n");
		printf("This queue is still currently active and cannot be deleted now.<BR>Queue not deleted.<BR><BR>\n");
		printf("</CENTER>\n");

		printf("<FORM METHOD=POST ACTION=show_setup.cgi>\n");
		printf("<CENTER><INPUT TYPE=SUBMIT NAME=ACTION VALUE=OK></CENTER>\n");
		printf("</FORM>\n");

		printf("</BODY>\n</HTML>\n");

		exit(0);
	}
	/* */

	/* connect to server */
	sock = mconnect();
	if (sock == -1)
	{
		printf("Content-type:  text/html\n\n");
		printf("<HTML>\n<HEAD><TITLE>PPS - Queue Update Error</TITLE></HEAD>\n\n");
		printf("<BODY TEXT=\"#FFFFFF\" BGCOLOR=\"#000080\">");
		printf("<CENTER>\n<FONT SIZE=+2>DISPLAY/UPDATE QUEUE INFORMATION</FONT><BR><BR>\n");
		printf("Error communicating with MSQL server.<BR>Queue not deleted.<BR><BR>\n");
		printf("Please notify administrator.\n</CENTER>\n");

		printf("<FORM METHOD=POST ACTION=show_setup.cgi>\n");
		printf("<CENTER><INPUT TYPE=SUBMIT NAME=ACTION VALUE=OK></CENTER>\n");
		printf("</FORM>\n");

		printf("</BODY>\n</HTML>\n");

		exit(0);
	}
	/* */

	/* determine whether there are any printers still attached to this queue */
	sprintf(string, "SELECT * FROM printers WHERE queue=%d", record->keyval);
	result = send_query(sock, string);
	if (msql_err)
	{
		printf("Content-type:  text/html\n\n");
		printf("<HTML>\n<HEAD><TITLE>PPS - Queue Update Error</TITLE></HEAD>\n\n");
		printf("<BODY TEXT=\"#FFFFFF\" BGCOLOR=\"#000080\">");
		printf("<CENTER>\n<FONT SIZE=+2>DISPLAY/UPDATE QUEUE INFORMATION</FONT><BR><BR>\n");
		printf("Error communicating with MSQL server.<BR>Queue not deleted.<BR><BR>\n");
		printf("Please notify administrator.\n</CENTER>\n");

		printf("<FORM METHOD=POST ACTION=show_setup.cgi>\n");
		printf("<CENTER><INPUT TYPE=SUBMIT NAME=ACTION VALUE=OK></CENTER>\n");
		printf("</FORM>\n");

		printf("</BODY>\n</HTML>\n");

		exit(0);
	}
	if (msqlNumRows(result) > 0)
	{
		mclose(sock);

		printf("Content-type:  text/html\n\n");
		printf("<HTML>\n<HEAD><TITLE>PPS - Queue Update Error</TITLE></HEAD>\n\n");
		printf("<BODY TEXT=\"#FFFFFF\" BGCOLOR=\"#000080\">");
		printf("<CENTER>\n<FONT SIZE=+2>DISPLAY/UPDATE QUEUE INFORMATION</FONT><BR><BR>\n");
		printf("A queue with printers still attached cannot be deleted.<BR>Queue not deleted.<BR><BR>\n");
		printf("</CENTER>\n");

		printf("<FORM METHOD=POST ACTION=show_setup.cgi>\n");
		printf("<CENTER><INPUT TYPE=SUBMIT NAME=ACTION VALUE=OK></CENTER>\n");
		printf("</FORM>\n");

		printf("</BODY>\n</HTML>\n");

		exit(0);
	}
	/* */

	/* try to delete the record for this queue */
	sprintf(string, "DELETE FROM queues WHERE keyval=%d", record->keyval);
	result = send_query(sock, string);

	if (msql_err)
	{
		printf("Content-type:  text/html\n\n");
		printf("<HTML>\n<HEAD><TITLE>PPS - Queue Update Error</TITLE></HEAD>\n\n");
		printf("<BODY TEXT=\"#FFFFFF\" BGCOLOR=\"#000080\">");
		printf("<CENTER>\n<FONT SIZE=+2>DISPLAY/UPDATE QUEUE INFORMATION</FONT><BR><BR>\n");
		printf("Error attempting to delete queue from database.<BR>Queue not deleted.<BR><BR>\n");
		printf("</CENTER>\n");

		printf("<FORM METHOD=POST ACTION=show_setup.cgi>\n");
		printf("<CENTER><INPUT TYPE=SUBMIT NAME=ACTION VALUE=OK></CENTER>\n");
		printf("</FORM>\n");

		printf("</BODY>\n</HTML>\n");

		exit(0);
	}
	/* */

	/* delete the spool directory for this queue */
	sprintf(path, "rm -r %s/spool/%d", ROOT, record->keyval);
	system(path);
	/* */

	/* notify the print server that we have changed the database */
	notify_parent_reload_database();
	/* */

	/* we deleted it ok, so we redirect the user back to the main screen */
	printf("Location:  show_setup.cgi\n\n");
	/* */
}



void update_queue(struct record *record)
{
	int sock;
	m_result *result;
	char string[1024];
	char path[1024];
	char esc_description[129];


	/* connect to server */
	sock = mconnect();
	if (sock == -1)
	{
		printf("Content-type:  text/html\n\n");
		printf("<HTML>\n<HEAD><TITLE>PPS - Queue Update Error</TITLE></HEAD>\n\n");
		printf("<BODY TEXT=\"#FFFFFF\" BGCOLOR=\"#000080\">");
		printf("<CENTER>\n<FONT SIZE=+2>DISPLAY/UPDATE QUEUE INFORMATION</FONT><BR><BR>\n");
		printf("Error communicating with MSQL server.<BR>Queue not updated.<BR><BR>\n");
		printf("Please notify administrator.\n</CENTER>\n");

		printf("<FORM METHOD=POST ACTION=show_setup.cgi>\n");
		printf("<CENTER><INPUT TYPE=SUBMIT NAME=ACTION VALUE=OK></CENTER>\n");
		printf("</FORM>\n");

		printf("</BODY>\n</HTML>\n");

		exit(0);
	}
	/* */

	/* try to update the record for this queue */
	escape_string(esc_description, record->queue.description);
	sprintf(string, "UPDATE queues SET name='%s',description='%s',type=%d,charge=%0.2f,status=%d WHERE keyval=%d",
		record->queue.name, esc_description, record->queue.type, record->queue.charge, record->queue.status, record->keyval);
	result = send_query(sock, string);
	if (msql_err)
	{
		mclose(sock);

		printf("Content-type:  text/html\n\n");
		printf("<HTML>\n<HEAD><TITLE>PPS - Queue Update Error</TITLE></HEAD>\n\n");
		printf("<BODY TEXT=\"#FFFFFF\" BGCOLOR=\"#000080\">");
		printf("<CENTER>\n<FONT SIZE=+2>DISPLAY/UPDATE QUEUE INFORMATION</FONT><BR><BR>\n");
		printf("Error communicating with MSQL server.<BR>Queue not updated.<BR><BR>\n");
		printf("Please notify administrator.\n</CENTER>\n");

		printf("<FORM METHOD=POST ACTION=show_setup.cgi>\n");
		printf("<CENTER><INPUT TYPE=SUBMIT NAME=ACTION VALUE=OK></CENTER>\n");
		printf("</FORM>\n");

		printf("</BODY>\n</HTML>\n");

		exit(0);
	}
	/* */

 	mclose(sock);

	/* notify the print server that we have changed the database */
	notify_parent_reload_database();
	notify_queue_reload_database(record->keyval);
	/* */

	/* we updated it ok, so we redirect the user back to the main screen */
	printf("Location:  show_setup.cgi\n\n");
	/* */
}

