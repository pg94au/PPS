/*  pjl_jetdirect/continue_add_pjl_jetdirect.c 
 *
 *  Parse all information given for a PJL JetDirect printer, and add a record in the database for it.
 *
 *  (c)1998 Paul Grebenc
 *
 *  $Header: /usr/local/PPS_RCS/src/web/pjl_jetdirect/continue_add_pjl_jetdirect.c,v 1.1 1999/12/10 03:32:18 paul Exp $
 */


#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>

#include "pps.h"
#include "database.h"
#include "locking.h"
#include "notify.h"
#include "parse.h"

#define OKAY		1
#define NO_SPACE	2
#define MSQL_ERROR	3
#define PRINTER_EXISTS	4

struct posted
{
	int type;
	char name[33];
	char description[65];
	int queue;
	int status;
	char address[256];
	int port;
};

int main();
void check_input(struct posted *posted);
int insert_printer(struct posted *posted);


main()
{
	struct posted posted;
	int ins_err;


	/* check the values submitted to us */
	check_input(&posted);
	/* */

	/* try to add the new printer */
	if ((ins_err = insert_printer(&posted)) == OKAY)
	{
		notify_parent_reload_database();
		notify_queue_reload_database(posted.queue);

		/*** normally, go back to queue setup screen ***/
		printf("Location: ../show_setup.cgi\n\n");
	}
	else
	{
		/* inform the user of error that occurred */
		printf("Content-type:  text/html\n\n");
		printf("<HTML><HEAD><TITLE>PPS - Printer Addition Error</TITLE></HEAD>\n");
		printf("<BODY TEXT=#FFFFFF BGCOLOR=#000080>\n");
		printf("<CENTER><FONT SIZE=+2>CREATE NEW PRINTER</FONT></CENTER><BR><BR>\n");

		switch(ins_err)
		{
			case NO_SPACE:
				printf("<CENTER>\nMaximum number of printers reached for this queue.<BR><BR>\n");
				printf("Printer not created.</CENTER><BR><BR>\n");
				break;
			case MSQL_ERROR:
				printf("<CENTER>\nError communicating with MSQL server.<BR><BR>\n");
				printf("Printer not created.<BR><BR>\n");
				printf("Please notify administrator.</CENTER><BR><BR>\n");
				break;
			case PRINTER_EXISTS:
				printf("<CENTER>\nA printer by this name already exists on this queue.<BR><BR>\n");
				printf("Duplicate printer not created.</CENTER><BR><BR>\n");
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



void check_input(struct posted *posted)
{
	char errtxt[4096];

	/* parse our posted variables */
	if ((parse_all() == -1)
	|| (getenv("CGI_name") == NULL)
	|| (getenv("CGI_description") == NULL)
	|| (getenv("CGI_queue") == NULL)
	|| (getenv("CGI_status") == NULL)
	|| (getenv("CGI_address") == NULL)
	|| (getenv("CGI_port") == NULL))
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
	strcpy(posted->name, getenv("CGI_name"));
	strcpy(posted->description, getenv("CGI_description"));
	posted->type = PPS_PRINTER_PJL_JETDIRECT;
	sscanf(getenv("CGI_status"), "%d", &posted->status);
	sscanf(getenv("CGI_queue"), "%d", &posted->queue);
	strcpy(posted->address, getenv("CGI_address"));
	if (sscanf(getenv("CGI_port"), "%d", &posted->port) == 0)
		posted->port = -1;
	/* */

	/* examine the new data given, to ensure it is also acceptable */
	errtxt[0] = '\0';
	if (strlen(posted->address) == 0)
		strcat(errtxt, "Address field cannot be left blank.<BR>\n");
	if ((posted->port < 1) || (posted->port > 65535))
		strcat(errtxt, "Port value must be between 1 and 65535.<BR>\n");

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
}



int insert_printer(struct posted *posted)
{
	int sock;
	m_result *result;
	m_row row;
	char string[1024];
	int numprinters, keyval, newkeyval;
	char esc_description[129];


	/* connect to server */
	sock = mconnect();
	if (sock == -1)
		return MSQL_ERROR;
	/* */

	/* check to see if we can add another printer to this queue */
	sprintf(string, "SELECT queue FROM printers WHERE queue=%d", posted->queue);
	result = send_query(sock, string);
	if (msql_err)
		return MSQL_ERROR;
	numprinters = msqlNumRows(result);
	if (numprinters >= MAX_PRINTERS_PER_QUEUE)
	{
		end_query(result);
		mclose(sock);
		return NO_SPACE;
	}
	/* */

	/* check to see if this printer name already exists in this queue */
	sprintf(string, "SELECT name FROM printers WHERE name='%s' AND queue=%d", posted->name, posted->queue);
	result = send_query(sock, string);
	if (msqlNumRows(result) > 0)
	{
		end_query(result);
		mclose(sock);
		return PRINTER_EXISTS;
	}
	/* */

	/* try to find an unused key value for this new printer */
	result = send_query(sock, "SELECT keyval FROM printers ORDER BY keyval");
	if (msql_err)
	{
		mclose(sock);
		return MSQL_ERROR;
	}
	numprinters = msqlNumRows(result);

	newkeyval = 0;
	while (newkeyval < numprinters)
	{
		row = fetch_result(result);
		sscanf(row[0], "%d", &keyval);
		if (newkeyval != keyval)
			break;
		newkeyval++;
	}
	/* */

	/* delete any current record in the specific pjl_jetdirect table with the current keyval if there is one */
	sprintf(string, "DELETE FROM pjl_jetdirect WHERE keyval=%d", newkeyval);
	result = send_query(sock, string);
	if (msql_err)
	{
		mclose(sock);
		return MSQL_ERROR;
	}
	/* */

	/* try to insert the new specific pjl_jetdirect table printer record */
	sprintf(string, "INSERT INTO pjl_jetdirect (address,port,keyval) VALUES ('%s',%d,%d)", posted->address, posted->port, newkeyval);
	result = send_query(sock, string);
	if (msql_err)
	{
		mclose(sock);
		return MSQL_ERROR;
	}
	/* */

	/* try to insert the generic printers table printer record */
	escape_string(esc_description, posted->description);
	sprintf(string, "INSERT INTO printers (type,name,description,queue,status,keyval) VALUES (%d,'%s','%s',%d,%d,%d)",
		posted->type, posted->name, esc_description, posted->queue, posted->status, newkeyval);
	result = send_query(sock, string);
	if (msql_err)
	{
		mclose(sock);
		return MSQL_ERROR;
	}
	/* */

	mclose(sock);

	return OKAY;
}

