/*  continue_add_queue.c
 *
 *  Take the information given by the add user form, and create a queue based on it.
 *
 *  (c)1998 Paul Grebenc
 *
 *  $Header: /usr/local/PPS_RCS/src/web/continue_add_queue.c,v 1.1 1999/12/10 03:30:55 paul Exp $
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <values.h>
#include <errno.h>

#include "pps.h"
#include "parse.h"
#include "database.h"
#include "notify.h"

#define OKAY		0
#define MSQL_ERROR	1
#define QUEUE_EXISTS	2
#define NO_SPACE	3
#define MKDIR_ERROR	4

int main();
int lookup_name(char *name);
int insert_queue(struct pps_queue *new_queue, char *errtxt);


main()
{
	struct pps_queue new_queue;
	char errtxt[4096];
	int ins_err;


	/* parse our posted variables */
	if ((parse_all() == -1) 
	|| (getenv("CGI_name") == NULL)
	|| (getenv("CGI_description") == NULL)
	|| (getenv("CGI_type") == NULL)
	|| (getenv("CGI_charge") == NULL)
	|| (getenv("CGI_status") == NULL))
	{
		printf("Content-type:  text/html\n\n");
		printf("<HTML>\n<HEAD><TITLE>PPS - Queue Addition Error</TITLE></HEAD>\n\n");
		printf("<BODY TEXT=\"#FFFFFF\" BGCOLOR=\"#000080\">");
		printf("<CENTER>\n<FONT SIZE=+2>CREATE NEW QUEUE</FONT><BR><BR>\n");
		printf("Error parsing submitted form.<BR>Queue not created.<BR><BR>\n");
		printf("Notify administrator.\n</CENTER>\n");

		printf("<FORM METHOD=POST ACTION=show_setup.cgi>\n");
		printf("<CENTER><INPUT TYPE=SUBMIT NAME=ACTION VALUE=OK></CENTER>\n");
		printf("</FORM>\n");

		printf("</BODY>\n</HTML>\n");

		exit(0);
	}
	/* */

	/* get posted data from environment variables */
	strncpy(new_queue.name, getenv("CGI_name"), sizeof(new_queue.name));
	new_queue.name[sizeof(new_queue.name)-1] = '\0';
	strncpy(new_queue.description, getenv("CGI_description"), sizeof(new_queue.description));
	new_queue.description[sizeof(new_queue.description)-1] = '\0';
	if (sscanf(getenv("CGI_type"), "%d", &new_queue.type) == 0)
		new_queue.type = -1;
	if (strlen(getenv("CGI_charge")) == 0)
		new_queue.charge = -MAXFLOAT;	/* defined in values.h */
	else
	{
		if (sscanf(getenv("CGI_charge"), "%f", &new_queue.charge) == 0)
			new_queue.charge = -MAXFLOAT;	/* defined in values.h */
	}
	if (sscanf(getenv("CGI_status"), "%d", &new_queue.status) == 0)
		new_queue.status = -1;
	/* */

	/* examine the data, to ensure it is acceptable */
	errtxt[0] = '\0';
	if (strlen(new_queue.name) == 0)
		strcat(errtxt, "Name field must not be blank.<BR>\n");
	if (strstr(new_queue.name, " ") != NULL)
		strcat(errtxt, "Name field must not contain spaces.<BR>\n");
	if (strstr(new_queue.name, "'") != NULL)
	{
		strcat(errtxt, "Name field must not contain apostrophes.<BR>\n");
		/* reset name to null string, because we cannot lookup a queue with an apostrophe in it, */
		/* which we do immediately following this, without converting it, which is not worth the */
		/* bother, since it's an invalid queue name anyway.  We do, however, wish to still see the */
		/* NO_SPACE message, if there is no space for even a validly named queue */
		new_queue.name[0] = '\0';
		/* */
	}
	switch(lookup_name(new_queue.name))
	{
		case QUEUE_EXISTS:
			strcat(errtxt, "A queue by this name already exists.<BR>\n");
			break;
		case NO_SPACE:
			strcat(errtxt, "The maximum number of queues have already been created.<BR>\n");
			break;
	}
	if (new_queue.type == -1)
		strcat(errtxt, "Invalid queue type.<BR>\n");
	if (new_queue.charge < 0.0)
	{
		if (new_queue.charge == -MAXFLOAT)
			strcat(errtxt, "Invalid charge value.<BR>\n");
		else
			strcat(errtxt, "The charge amount must be zero or greater.<BR>\n");
	}
	if (new_queue.status == -1)
		strcat(errtxt, "Invalid queue status.<BR>\n");
	if ((new_queue.type == -1) || (new_queue.status == -1))
		strcat(errtxt, "<BR>\nNotify administrator.\n");

	if (strlen(errtxt) > 0)
	{
		printf("Content-type:  text/html\n\n");
		printf("<HTML><HEAD><TITLE>PPS - Queue Addition Error</TITLE></HEAD>\n");
		printf("<BODY TEXT=#FFFFFF BGCOLOR = #000080>\n");
		printf("<CENTER><FONT SIZE=+2>CREATE NEW QUEUE</FONT><BR><BR>\n");

		printf(errtxt);
		printf("<BR>Queue not created.<BR>\n");
		printf("</CENTER><BR>\n");

		printf("<FORM METHOD=POST ACTION=show_setup.cgi>\n");
		printf("<CENTER><INPUT TYPE=SUBMIT NAME=ACTION VALUE=OK></CENTER>\n");
		printf("</FORM>\n");

		printf("</BODY>\n</HTML>\n");

		exit(0);
	}
	/* */

	/* everything passed to us looks ok, so we will try to create this queue */
	if ((ins_err = insert_queue(&new_queue, errtxt)) == OKAY)
	{
		/* notify the print server that we have changed the database */
		notify_parent_reload_database();
		/* */

		/* we added the queue ok, so we redirect the user back to the main screen */
		printf("Location:  show_setup.cgi\n\n");
		/* */
	}
	else
	{
		/* inform the user of errors that occurred */
		printf("Content-type:  text/html\n\n");
		printf("<HTML><HEAD><TITLE>PPS - Queue Addition Error</TITLE></HEAD>\n");
		printf("<BODY TEXT=#FFFFFF BGCOLOR=#000080>\n");
		printf("<CENTER><FONT SIZE=+2>CREATE NEW QUEUE</FONT></CENTER><BR><BR>\n");

		switch(ins_err)
		{
			case NO_SPACE:
				printf("<CENTER>\nMaximum number of queues reached for this system.<BR><BR>\n");
				printf("Queue not created.</CENTER><BR><BR>\n");
				break;
			case MSQL_ERROR:
				printf("<CENTER>\nError communicating with MSQL server.<BR><BR>\n");
				printf("Queue not created.<BR><BR>\n");
				printf("Please notify administrator.</CENTER><BR><BR>\n");
				break;
			case QUEUE_EXISTS:
				printf("<CENTER>\nA queue by this name already exists in this system.<BR><BR>\n");
				printf("Duplicate queue not created.</CENTER><BR><BR>\n");
				break;
			case MKDIR_ERROR:
				printf("<CENTER>\nError creating spool directory [%s] for this queue.<BR><BR>\n", errtxt);
				printf("Queue not created.<BR><BR>\n");
				printf("Please notify administrator.</CENTER><BR><BR>\n");
				break;
		}

		printf("<FORM METHOD=POST ACTION=show_setup.cgi>\n");
		printf("<CENTER><INPUT TYPE=SUBMIT NAME=ACTION VALUE=OK></CENTER>\n");
		printf("</FORM>\n");

		printf("</BODY>\n</HTML>\n");
		/* */
	}
	/* */
}



int lookup_name(char *name)
{
	int sock;
	m_result *result;
	m_row row;
	char string[1024];
	int numqueues;


	/* connect to server */
	sock = mconnect();
	if (sock == -1)
		return MSQL_ERROR;
	/* */

	/*** check to see whether we can add another queue to this system ***/
	sprintf(string, "SELECT * FROM queues");
	result = send_query(sock, string);
	if (msql_err)
		return MSQL_ERROR;
	numqueues = msqlNumRows(result);
	if (numqueues > MAX_QUEUES - 1)
	{
		end_query(result);
		mclose(sock);
		return NO_SPACE;
	}
	/*** ***/

	/*** check to see if this queue name already exists ***/
	sprintf(string, "SELECT name FROM queues WHERE name='%s'", name);
	result = send_query(sock, string);
	if (msqlNumRows(result) > 0)
	{
		mclose(sock);

		return QUEUE_EXISTS;
	}
	/*** ***/

	return OKAY;
}



int insert_queue(struct pps_queue *new_queue, char *errtxt)
{
	int sock;
	m_result *result;
	m_row row;
	char string[1024];
	char path[1024];
	int numqueues, keyval, newkeyval;
	char esc_description[129];


	/* connect to server */
	sock = mconnect();
	if (sock == -1)
		return MSQL_ERROR;
	/* */

	/* check to see if we can add another queue to this system */
	result = send_query(sock, "SELECT * FROM queues");
	if (msql_err)
		return MSQL_ERROR;
	numqueues = msqlNumRows(result);
	if (numqueues >= MAX_QUEUES)
	{
		end_query(result);
		mclose(sock);
		return NO_SPACE;
	}
	/* */

	/* check to see if this queue name already exists in the system */
	sprintf(string, "SELECT name FROM queues WHERE name='%s'", new_queue->name);
	result = send_query(sock, string);
	if (msqlNumRows(result) > 0)
	{
		end_query(result);
		mclose(sock);
		return QUEUE_EXISTS;
	}
	/* */

	/* try to find an unused key value for this new queue */
	result = send_query(sock, "SELECT keyval FROM queues ORDER BY keyval");
	if (msql_err)
	{
		mclose(sock);
		return MSQL_ERROR;
	}
	numqueues = msqlNumRows(result);

	newkeyval = 0;
	while (newkeyval < numqueues)
	{
		row = fetch_result(result);
		sscanf(row[0], "%d", &keyval);
		if (newkeyval != keyval)
			break;
		newkeyval++;
	}
	/* */

	/* try to create the spool directory for this queue */
	/* delete any existing directory first... although there should not be one */
	sprintf(path, "rm -r %s/spool/%d", ROOT, newkeyval);
	system(path);
	sprintf(path, "%s/spool/%d", ROOT, newkeyval);
	if (mkdir(path, 0700) == -1)
	{
		mclose(sock);
		strcpy(errtxt, path);
		return MKDIR_ERROR;
	}
	/* */

	/* try to insert the queue record */
	escape_string(esc_description, new_queue->description);
	sprintf(string, "INSERT INTO queues (name,description,type,charge,status,keyval) VALUES ('%s','%s',%d,%0.2f,%d,%d)",
		new_queue->name, esc_description, new_queue->type, new_queue->charge, new_queue->status, newkeyval);
	result = send_query(sock, string);
	if (msql_err)
	{
		/* remove the spool directory for this queue */
		sprintf(path, "rm -r %s/spool/%s", ROOT, newkeyval);
		system(path);
		/* */

		mclose(sock);
		return MSQL_ERROR;
	}
	/* */

	mclose(sock);

	return OKAY;
}

