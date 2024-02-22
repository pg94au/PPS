/*  continue_add_printer.c
 *
 *  Parse device independent information given for a new printer, then execute appropriate interface
 *  component which will request device dependent information for that printer.
 *
 *  (c)1998 Paul Grebenc
 *
 *  $Header: /usr/local/PPS_RCS/src/web/continue_add_printer.c,v 1.1 1999/12/10 03:30:30 paul Exp $
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "pps.h"
#include "parse.h"
#include "database.h"

#define OKAY		0
#define MSQL_ERROR	1
#define PRINTER_EXISTS	2
#define NO_SPACE	3

struct record
{
	char name[33];
	char description[65];
	int type;
	int queue;
	int status;
	int keyval;
};

int main();
int lookup_name(char *name, int queue);
int exec_printer_specific(int type, char *errtxt);


main()
{
	struct record record;
	char errtxt[4096];


	/* parse our posted variables */
	if ((parse_all() == -1)
	|| (getenv("CGI_name") == NULL)
	|| (getenv("CGI_description") == NULL)
	|| (getenv("CGI_type") == NULL)
	|| (getenv("CGI_status") == NULL)
	|| (getenv("CGI_queue") == NULL))
	{
		printf("Content-type:  text/html\n\n");
		printf("<HTML>\n<HEAD><TITLE>PPS - Printer Addition Error</TITLE></HEAD>\n\n");
		printf("<BODY TEXT=\"#FFFFFF\" BGCOLOR=\"#000080\">");
		printf("<CENTER>\n<FONT SIZE=+2>CREATE NEW PRINTER</FONT><BR><BR>\n");
		printf("Error parsing submitted form.<BR>Printer not created.<BR><BR>\n");
		printf("Notify administrator.\n</CENTER>\n");

		printf("<FORM METHOD=POST ACTION=show_setup.cgi>\n");
		printf("<CENTER><INPUT TYPE=SUBMIT NAME=ACTION VALUE=OK></CENTER>\n");
		printf("</FORM>\n");

		printf("</BODY>\n</HTML>\n");

		exit(0);
        }
        /* */

	/* get posted data from environment variables */
	strncpy(record.name, getenv("CGI_name"), sizeof(record.name));
	record.name[sizeof(record.name)-1] = '\0';
	strncpy(record.description, getenv("CGI_description"), sizeof(record.description));
	record.description[sizeof(record.description)-1] = '\0';
	sscanf(getenv("CGI_type"), "%d", &record.type);
	sscanf(getenv("CGI_queue"), "%d", &record.queue);
	sscanf(getenv("CGI_status"), "%d", &record.status);
        /* */

	/* examine the data given thus far, to ensure it is acceptable */
	errtxt[0] = '\0';
	if (strlen(record.name)==0)
		strcat(errtxt, "Name field must not be blank.<BR>\n");
	if (strstr(record.name, " ") != NULL)
		strcat(errtxt, "Name field must not contain spaces.<BR>\n");
	if (strstr(record.name, "'") != NULL)
	{
		strcat(errtxt, "Name field must not contain apostrophes.<BR>\n");
		/* as with continue_add_queue.c, we make the name a null string here because we cannot pass it */
		/* with apostrophes in it unescaped to lookup_name(), which ultimately passes it to mSQL, which */
		/* chokes on it */
		record.name[0] = '\0';
		/* */
	}
	switch(lookup_name(record.name, record.queue))
	{
		case PRINTER_EXISTS:
			strcat(errtxt, "A printer by this name is already attached to this queue.<BR>\n");
			break;
		case NO_SPACE:
			strcat(errtxt, "The maximum number of printers have already been attached to this queue.<BR>\n");
			break;
	}
	if (record.type == -1)
		strcat(errtxt, "Invalid printer type.<BR>\n");
	if (record.status == -1)
		strcat(errtxt, "Invalid printer status.<BR>\n");
	if (record.queue == -1)
		strcat(errtxt, "Invalid queue specifier.<BR>\n");
	if ((record.type == -1) || (record.status == -1) || (record.queue == -1))
		strcat(errtxt, "<BR>\nNotify administrator.\n");

	if (strlen(errtxt) > 0)
	{
		printf("Content-type:  text/html\n\n");
		printf("<HTML><HEAD><TITLE>PPS - Printer Addition Error</TITLE></HEAD>\n");
		printf("<BODY TEXT=#FFFFFF BGCOLOR = #000080>\n");
		printf("<CENTER><FONT SIZE=+2>CREATE NEW PRINTER</FONT><BR><BR>\n");

		printf(errtxt);
		printf("<BR>Printer not created.<BR>\n");
		printf("</CENTER><BR>\n");

		printf("<FORM METHOD=POST ACTION=show_setup.cgi>\n");
		printf("<CENTER><INPUT TYPE=SUBMIT NAME=ACTION VALUE=OK></CENTER>\n");
		printf("</FORM>\n");

		printf("</BODY>\n</HTML>\n");

		exit(0);
	}
	/* */

	/* everything passed to us looks ok, so we will exec the correct binary to take us to printer specific procedures */
	errtxt[0] = '\0';
	if (exec_printer_specific(record.type, errtxt) == -1)
	{
		printf("Content-type:  text/html\n\n");
		printf("<HTML>\n<HEAD><TITLE>PPS - Printer Addition Error</TITLE></HEAD>\n\n");
		printf("<BODY TEXT=\"#FFFFFF\" BGCOLOR=\"#000080\">");
		printf("<CENTER>\n<FONT SIZE=+2>CREATE NEW PRINTER</FONT><BR><BR>\n");
		printf("Error executing printer specific routines.<BR><BR>\n");
		if (strlen(errtxt) > 0)
			printf("%s<BR><BR>\n", errtxt);
		printf("Printer not created.<BR><BR>\n");
		printf("Notify administrator.\n</CENTER>\n");

		printf("<FORM METHOD=POST ACTION=show_setup.cgi>\n");
		printf("<CENTER><INPUT TYPE=SUBMIT NAME=ACTION VALUE=OK></CENTER>\n");
		printf("</FORM>\n");

		printf("</BODY>\n</HTML>\n");

		exit(0);
	}
	/* */

	printf("Content-type:  text/plain\n\nHere!\n");
}



int lookup_name(char *name, int queue)
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

	/*** check to see whether we can add another printer to this queue ***/
	sprintf(string, "SELECT queue FROM printers WHERE queue=%d", queue);
	result = send_query(sock, string);
	if (msql_err)
		return MSQL_ERROR;
	numprinters = msqlNumRows(result);
	if (numprinters > MAX_PRINTERS_PER_QUEUE - 1)
	{
		end_query(result);
		mclose(sock);
		return NO_SPACE;
	}
	/*** ***/

	/*** check to see if this printer name already exists in this queue ***/
	sprintf(string, "SELECT name FROM printers WHERE name='%s' AND queue=%d", name, queue);
	result = send_query(sock, string);
	if (msqlNumRows(result) > 0)
	{
		mclose(sock);

		return PRINTER_EXISTS;
	}
	/*** ***/

	return OKAY;
}



int exec_printer_specific(int type, char *errtxt)
{
	char path[1024];


	switch(type)
	{
		case PPS_PRINTER_GENERIC:
			printf("Content-type:  text/html\n\n");
			printf("<HTML>\n<HEAD><TITLE>PPS - Error</TITLE></HEAD>\n\n");
			printf("<BODY TEXT=\"#FFFFFF\" BGCOLOR=\"#000080\">");
			printf("<CENTER>\n<FONT SIZE=+2>PPS</FONT><BR><BR>\n");
			printf("Generic printer type not yet implemented.\n</CENTER>\n");
			printf("</BODY>\n</HTML>\n");

			printf("<FORM METHOD=POST ACTION=show_setup.cgi>\n");
			printf("<CENTER><INPUT TYPE=SUBMIT NAME=ACTION VALUE=OK></CENTER>\n");
			printf("</FORM>\n");

			exit(0);

		case PPS_PRINTER_PJL_JETDIRECT:
			sprintf(path, "%s/web_bin/pjl_jetdirect/add_pjl_jetdirect.cgi", ROOT);
			fflush(stdout);
			if (execl(path, path, (char *)0) == -1)
			{
				sprintf(errtxt, "[%s - %s]", path, strerror(errno));

				return -1;
			}

		default:
			printf("Content-type:  text/html\n\n");
			printf("<HTML>\n<HEAD><TITLE>PPS - Error</TITLE></HEAD>\n\n");
			printf("<BODY TEXT=\"#FFFFFF\" BGCOLOR=\"#000080\">");
			printf("<CENTER>\n<FONT SIZE=+2>PPS</FONT><BR><BR>\n");
			printf("Unkown printer type.<BR><BR>\nNotify administrator.\n</CENTER>\n");
			printf("</BODY>\n</HTML>\n");

			exit(0);
	}
}

