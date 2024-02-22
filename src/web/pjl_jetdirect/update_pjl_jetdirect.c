/*  pjl_jetdirect/update_printer.c
 *
 *  Parse information from the printer setup form for a PJL JetDirect printer and update or delete it.
 *
 *  (c)1998 Paul Grebenc
 *
 *  $Header: /usr/local/PPS_RCS/src/web/pjl_jetdirect/update_pjl_jetdirect.c,v 1.1 1999/12/10 03:31:52 paul Exp $
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "pps.h"
#include "database.h"
#include "locking.h"
#include "notify.h"
#include "parse.h"

#define UPDATE		1
#define DELETE		2
#define ARG_ERROR	3
#define MSQL_ERROR	4
#define PRINTER_EXISTS	5
#define OKAY		6

struct record
{
	char name[33];
	char description[65];
	int queue;
	int status;
	char address[256];
	int port;
	int keyval;
};


int main();
int examine_posted(struct record *record);
int lookup_name(char *name, int keyval, int queue);
void delete_printer(struct record *record);
void update_printer(struct record *record);


main()
{
	struct record record;


	/* get the data posted to us from the update form, and do with it what is required */
	switch (examine_posted(&record))
	{
		case UPDATE:
			update_printer(&record);
			exit(0);
		case DELETE:
			delete_printer(&record);
			exit(0);
		case ARG_ERROR:
			printf("Content-type:  text/html\n\n");
			printf("<HTML>\n<HEAD><TITLE>PPS - Printer Update Error</TITLE></HEAD>\n\n");
			printf("<BODY TEXT=\"#FFFFFF\" BGCOLOR=\"#000080\">");
			printf("<CENTER>\n<FONT SIZE=+2>DISPLAY/UPDATE PRINTER INFORMATION</FONT><BR><BR>\n");
			printf("Error with submitted arguments.<BR>Printer not modified.<BR><BR>\n");
			printf("Notify administrator.\n</CENTER>\n");

			printf("<FORM METHOD=POST ACTION=../show_setup.cgi>\n");
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
	|| (getenv("CGI_queue") == NULL)
	|| (getenv("CGI_status") == NULL)
	|| (getenv("CGI_address") == NULL)
	|| (getenv("CGI_port") == NULL)
	|| (getenv("CGI_keyval") == NULL)
	|| (getenv("CGI_name") == NULL)
	|| (getenv("CGI_ACTION") == NULL))
	{
		printf("Content-type:  text/html\n\n");
		printf("<HTML>\n<HEAD><TITLE>PPS - Printer Update Error</TITLE></HEAD>\n\n");
		printf("<BODY TEXT=\"#FFFFFF\" BGCOLOR=\"#000080\">");
		printf("<CENTER>\n<FONT SIZE=+2>DISPLAY/UPDATE PRINTER INFORMATION</FONT><BR><BR>\n");
		printf("Error parsing submitted form.<BR>Printer not modified.<BR><BR>\n");
		printf("Notify administrator.\n</CENTER>\n");

		printf("<FORM METHOD=POST ACTION=../show_setup.cgi>\n");
		printf("<CENTER><INPUT TYPE=SUBMIT NAME=ACTION VALUE=OK></CENTER>\n");
		printf("</FORM>\n");

		printf("</BODY>\n</HTML>\n");

		exit(0);
	}
	/* */

	/* get posted data from environment variables */
	strncpy(record->name, getenv("CGI_name"), sizeof(record->name));
	record->name[sizeof(record->name)-1] = '\0';
	strncpy(record->description, getenv("CGI_description"), sizeof(record->description));
	record->description[sizeof(record->description)-1] = '\0';
	if (sscanf(getenv("CGI_queue"), "%d", &record->queue) == 0)
		record->queue = -1;
	if (sscanf(getenv("CGI_status"), "%d", &record->status) == 0)
		record->status = -1;
	strncpy(record->address, getenv("CGI_address"), sizeof(record->address));
	if (sscanf(getenv("CGI_port"), "%d", &record->port) == 0)
		record->port = -1;
	if (sscanf(getenv("CGI_keyval"), "%d", &record->keyval) == 0)
		record->keyval = -1;
	if (strcmp("UPDATE PRINTER", getenv("CGI_ACTION")) == 0)
		action = UPDATE;
	else
	{
		if (strcmp("DELETE PRINTER", getenv("CGI_ACTION")) == 0)
			action = DELETE;
		else
			action = -1;
	}
	/* */

	/* check first if we are deleting this printer, because if we are we don't really need to worry about a lot of the fields sent to us */
	if ((action == DELETE) && (record->keyval != -1) && (record->queue != -1) && (record->status != -1))
		return DELETE;
	/* */

	/* examine the data given to us, to ensure it is acceptable */
	errtxt[0] = '\0';
	if (strlen(record->name) == 0)
		strcat(errtxt, "Name field must not be blank.<BR>\n");
	if ((strstr(record->name, " ") != NULL) || (strstr(record->name, "'") != NULL))
	{
		if (strstr(record->name, " ") != NULL)
			strcat(errtxt, "Name field must not contain spaces.<BR>\n");
		if (strstr(record->name, "'") != NULL)
			strcat(errtxt, "Name field must not contain apostrophes.<BR>\n");
	}
	else
	{
		switch(lookup_name(record->name, record->keyval, record->queue))
		{
			case PRINTER_EXISTS:
				strcat(errtxt, "A printer by this name is already attached to this queue.<BR>\n");
				break;
			case MSQL_ERROR:
				printf("Content-type:  text/html\n\n");
				printf("<HTML><HEAD><TITLE>PPS - Printer Update Error</TITLE></HEAD>\n");
				printf("<BODY TEXT=#FFFFFF BGCOLOR=#000080>\n");
				printf("<CENTER><FONT SIZE=+2>DISPLAY/UPDATE PRINTER INFORMATION</FONT></CENTER><BR><BR>\n");
				printf("<CENTER>\nError communicating with MSQL server.<BR><BR>\n");
				printf("Printer not modified.<BR><BR>\n");
				printf("Please notify administrator.</CENTER><BR><BR>\n");
				printf("<FORM METHOD=POST ACTION=../show_setup.cgi>\n");
				printf("<CENTER><INPUT TYPE=SUBMIT NAME=ACTION VALUE=OK></CENTER>\n");
				printf("</FORM>\n");
				printf("</BODY>\n</HTML>\n");

				exit(0);
		}
	}
	if (record->queue == -1)
	{
		strcat(errtxt, "Invalid queue specifier submitted.<BR>\n");
		notify_admin = 1;
	}
	if (record->status == -1)
	{
		strcat(errtxt, "Invalid status selection submitted.<BR>\n");
		notify_admin = 1;
	}
	if (strlen(record->address) == 0)
		strcat(errtxt, "Address field must not be blank.<BR>\n");
	if ((record->port < 1) || (record->port > 65535))
		strcat(errtxt, "Port value must be between 1 and 65535.<BR>\n");
	if (action != UPDATE)
	{
		strcat(errtxt, "Invalid action requested.<BR>\n");
		notify_admin = 1;
	}

	if (strlen(errtxt) > 0)
        {
		printf("Content-type:  text/html\n\n");
		printf("<HTML><HEAD><TITLE>PPS - Printer Update Error</TITLE></HEAD>\n");
		printf("<BODY TEXT=#FFFFFF BGCOLOR = #000080>\n");
		printf("<CENTER><FONT SIZE=+2>DISPLAY/UPDATE PRINTER INFORMATION</FONT><BR><BR>\n");

		printf(errtxt);
		printf("<BR>Printer not modified.<BR>\n");
		if (notify_admin == 1)
			printf("<BR>Notify administrator.<BR>\n");
		printf("</CENTER><BR>\n");

		printf("<FORM METHOD=POST ACTION=../show_setup.cgi>\n");
		printf("<CENTER><INPUT TYPE=SUBMIT NAME=ACTION VALUE=OK></CENTER>\n");
		printf("</FORM>\n");

		printf("</BODY>\n</HTML>\n");

		exit(0);
	}
	/* */

	return UPDATE;
}



int lookup_name(char *name, int keyval, int queue)
{
	int sock;	
	m_result *result;
	m_row row;
	char string[1024];
	int numprinters;


	/* connect to server */
	sock = mconnect();
	if (sock == -1)
		return MSQL_ERROR;
	/* */

	/* check if a printer by this name is already attached to this queue */
	sprintf(string, "SELECT name FROM printers WHERE name='%s' AND keyval<>%d AND queue=%d", name, keyval, queue);
	result = send_query(sock, string);
	if (msql_err)
		return MSQL_ERROR;
	if (msqlNumRows(result) > 0)
	{
		mclose(sock);

		return PRINTER_EXISTS;
	}
	/* */

	mclose(sock);

	return OKAY;
}



void delete_printer(struct record *record)
{
	int sock;
	m_result *result;
	char string[1024];
	char path[1024];
	int lock_status;


	/* check first to see if this printer is currently in use */
	/* if the file is not locked, or doesn't exist at all, the printer is not currently active */
	sprintf(path, "%s/spool/%d/.%d", ROOT, record->queue, record->keyval);

	if (((open_and_test_lock(path, &lock_status)) == -1) && (errno != ENOENT))
	{
		/* the test of the lock failed, and it's NOT because the file doesn't exist */
                printf("Content-type: text/html\n\n");
                printf("<HTML><HEAD><TITLE>PPS - Printer update error</TITLE></HEAD>\n");
                printf("<BODY TEXT=\"#FFFFFF\" BGCOLOR=\"#000080\">\n");
                printf("<CENTER><FONT SIZE=+2>DISPLAY/UPDATE PRINTER INFORMATION</FONT></CENTER><BR><BR>\n");

                printf("<CENTER>\nError checking current status of printer.<BR>\n");
                printf("Printer not deleted.<BR>\n");
                printf("Please notify administrator.</CENTER><BR><BR>\n");

                printf("<FORM METHOD=GET ACTION=../show_setup.cgi>\n");
                printf("<CENTER><INPUT TYPE=SUBMIT NAME=\"ACTION\" VALUE=\"OK\"></CENTER>\n");
                printf("</FORM>\n");

                printf("</BODY>\n</HTML>\n");
                /* */

                exit(0);
	}
	/* */

	/* if this file is locked, the printer is currently printing something */
	if (lock_status == F_WRLCK)
	{
		printf("Content-type: text/html\n\n");
		printf("<HTML><HEAD><TITLE>PPS - Printer update error</TITLE></HEAD>\n");
		printf("<BODY TEXT=\"#FFFFFF\" BGCOLOR=\"#000080\">\n");
		printf("<CENTER><FONT SIZE=+2>DISPLAY/UPDATE PRINTER INFORMATION</FONT></CENTER><BR>\n");

		printf("<CENTER>\nThis printer is still currently active and cannot be deleted now.<BR><BR>\n");
		printf("Printer not deleted.</CENTER><BR><BR>\n");

		printf("<FORM METHOD=GET ACTION=../show_setup.cgi>\n");
		printf("<CENTER><INPUT TYPE=SUBMIT NAME=\"ACTION\" VALUE=\"OK\"></CENTER>\n");
		printf("</FORM>\n");

		printf("</BODY>\n</HTML>\n");

		exit(0);
	}
	/* */

	/* connect to server */
	sock = mconnect();
	if (sock == -1)
	{
		printf("Content-type: text/html\n\n");
		printf("<HTML><HEAD><TITLE>PPS - Printer Update Error</TITLE></HEAD>\n");
		printf("<BODY TEXT=\"#FFFFFF\" BGCOLOR=\"#000080\">\n");
		printf("<CENTER><FONT SIZE=+2>DISPLAY/UPDATE PRINTER INFORMATION</FONT></CENTER><BR><BR>\n");

		printf("<CENTER>\nError communicating with MSQL server.<BR>\n");
		printf("Printer not deleted.<BR>\n");
		printf("Please notify administrator.</CENTER><BR><BR>\n");

		printf("<FORM METHOD=GET ACTION=../show_setup.cgi>\n");
		printf("<CENTER><INPUT TYPE=SUBMIT NAME=\"ACTION\" VALUE=\"OK\"></CENTER>\n");
		printf("</FORM>\n");

		printf("</BODY>\n</HTML>\n");

		exit(0);
	}
	/* */

	/* try to delete the non-specific printer record first */
	sprintf(string, "DELETE FROM printers WHERE keyval=%d", record->keyval);
	result = send_query(sock, string);

	if (msql_err)
	{
		printf("Content-type: text/html\n\n");
		printf("<HTML><HEAD><TITLE>PPS - Printer Update Error</TITLE></HEAD>\n");
		printf("<BODY TEXT=\"#FFFFFF\" BGCOLOR=\"#000080\">\n");
		printf("<CENTER><FONT SIZE=+2>DISPLAY/UPDATE PRINTER INFORMATION</FONT></CENTER><BR><BR>\n");

		printf("<CENTER>\nError attempting to delete printer from database.<BR>\n");
		printf("Printer not deleted.<BR>\n");
		printf("Please notify administrator.</CENTER><BR><BR>\n");

		printf("<FORM METHOD=GET ACTION=../show_setup.cgi>\n");
		printf("<CENTER><INPUT TYPE=SUBMIT NAME=\"ACTION\" VALUE=\"OK\"></CENTER>\n");
		printf("</FORM>\n");

		printf("</BODY>\n</HTML>\n");

		exit(0);
	}
	/* */

	/* try to delete the model-specific printer record second */
	sprintf(string, "DELETE FROM pjl_jetdirect WHERE keyval=%d", record->keyval);
	result = send_query(sock, string);

	if (msql_err)
	{
		printf("Content-type: text/html\n\n");
		printf("<HTML><HEAD><TITLE>PPS - Printer Update Error</TITLE></HEAD>\n");
		printf("<BODY TEXT=\"#FFFFFF\" BGCOLOR=\"#000080\">\n");
		printf("<CENTER><FONT SIZE=+2>DISPLAY/UPDATE PRINTER INFORMATION</FONT></CENTER><BR><BR>\n");

		printf("<CENTER>\nError attempting to delete printer from database.<BR>\n");
		printf("Printer deleted.<BR>\n");
		printf("Please notify administrator.</CENTER><BR><BR>\n");

		printf("<FORM METHOD=GET ACTION=../show_setup.cgi>\n");
		printf("<CENTER><INPUT TYPE=SUBMIT NAME=\"ACTION\" VALUE=\"OK\"></CENTER>\n");
		printf("</FORM>\n");

		printf("</BODY>\n</HTML>\n");

		/* even though the model-specific record could not be deleted, the generic one was, which means the */
		/* model-specific record has been orphaned, so to speak.  That's not a good thing to have happend, but */
		/* the only way around it appears to be to have the server do periodic "garbage collection" in the */
		/* database...  That could be done, but orphaned records in the specific database won't harm anything */
		/* so for now I'll just leave them.  In any case, we still must let the system know the database has */
		/* changed.  Because the generic record is gone, when the database is reloaded, the model-specific one */
		/* will not be looked at. */
		notify_parent_reload_database();
		notify_queue_reload_database(record->queue);
		/* */

		/* delete the lock file used by this printer in the spool directory of its queue */
		sprintf(path, "rm %s/spool/%d/.%d", ROOT, record->queue, record->keyval);
		system(path);
		/* */

		exit(0);
	}
	/* */

	/* notify the print server that we have changed the database */
	notify_parent_reload_database();
	notify_queue_reload_database(record->queue);
	/* */

	/* delete the lock file used by this printer in the spool directory of its queue */
	sprintf(path, "rm %s/spool/%d/.%d", ROOT, record->queue, record->keyval);
	system(path);
	/* */

	/* we deleted it ok, so we redirect the user back to the main screen */
	printf("Location:  ../show_setup.cgi\n\n");
	/* */
}



void update_printer(struct record *record)
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
		printf("Content-type: text/html\n\n");
		printf("<HTML><HEAD><TITLE>PPS - Printer Update Error</TITLE></HEAD>\n");
		printf("<BODY TEXT=\"#FFFFFF\" BGCOLOR=\"#000080\">\n");
		printf("<CENTER><FONT SIZE=+2>DISPLAY/UPDATE PRINTER INFORMATION</FONT></CENTER><BR><BR>\n");

		printf("<CENTER>\nError communicating with MSQL server.<BR>\n");
		printf("Printer not updated.<BR>\n");
		printf("Please notify administrator.</CENTER><BR><BR>\n");

		printf("<FORM METHOD=GET ACTION=../show_setup.cgi>\n");
		printf("<CENTER><INPUT TYPE=SUBMIT NAME=\"ACTION\" VALUE=\"OK\"></CENTER>\n");
		printf("</FORM>\n");

		printf("</BODY>\n</HTML>\n");

		exit(0);
	}
	/* */

	/* try to update the model specific information for this printer first */
	sprintf(string, "UPDATE pjl_jetdirect SET address='%s',port=%d WHERE keyval=%d", record->address, record->port, record->keyval);
	result = send_query(sock, string);
	if (msql_err)
	{
		mclose(sock);

		printf("Content-type: text/html\n\n");
		printf("<HTML><HEAD><TITLE>PPS - Printer Update Error</TITLE></HEAD>\n");
		printf("<BODY TEXT=\"#FFFFFF\" BGCOLOR=\"#000080\">\n");
		printf("<CENTER><FONT SIZE=+2>DISPLAY/UPDATE PRINTER INFORMATION</FONT></CENTER><BR><BR>\n");

		printf("<CENTER>\nError communicating with MSQL server.<BR>\n");
		printf("Printer not updated.<BR>\n");
		printf("Please notify administrator.</CENTER><BR><BR>\n");

		printf("<FORM METHOD=GET ACTION=../show_setup.cgi>\n");
		printf("<CENTER><INPUT TYPE=SUBMIT NAME=\"ACTION\" VALUE=\"OK\"></CENTER>\n");
		printf("</FORM>\n");

		printf("</BODY>\n</HTML>\n");

		exit(0);
	}
	/* */

	/* try to update the generic information for this printer second */
	escape_string(esc_description, record->description);
	sprintf(string, "UPDATE printers SET name='%s',description='%s',status=%d WHERE keyval=%d", record->name, esc_description, 
		record->status, record->keyval);
	result = send_query(sock, string);
	if (msql_err)
	{
		mclose(sock);

		printf("Content-type: text/html\n\n");
		printf("<HTML><HEAD><TITLE>PPS - Printer Update Error</TITLE></HEAD>\n");
		printf("<BODY TEXT=\"#FFFFFF\" BGCOLOR=\"#000080\">\n");
		printf("<CENTER><FONT SIZE=+2>DISPLAY/UPDATE PRINTER INFORMATION</FONT></CENTER><BR><BR>\n");

		printf("<CENTER>\nError communicating with MSQL server.<BR>\n");
		printf("Printer only partially updated.<BR>\n");
		printf("Please notify administrator.</CENTER><BR><BR>\n");

		printf("<FORM METHOD=GET ACTION=../show_setup.cgi>\n");
		printf("<CENTER><INPUT TYPE=SUBMIT NAME=\"ACTION\" VALUE=\"OK\"></CENTER>\n");
		printf("</FORM>\n");

		printf("</BODY>\n</HTML>\n");

		exit(0);
	}
	/* */

	mclose(sock);

	/* notify the print server that we have changed the database */
	notify_parent_reload_database();
	notify_queue_reload_database(record->queue);
	/* */

	/* we deleted it ok, so we redirect the user back to the main screen */
	printf("Location:  ../show_setup.cgi\n\n");
	/* */
}

