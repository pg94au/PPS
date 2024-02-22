/*  show_queue.c
 *
 *  Present the configuration of a queue to the user.
 *
 *  (c)1998 Paul Grebenc
 *
 *  $Header: /usr/local/PPS_RCS/src/web/show_queue.c,v 1.1 1999/12/10 03:28:30 paul Exp $
 */


#include <stdio.h>
#include <stdlib.h>

#include "pps.h"
#include "database.h"
#include "parse.h"

#define OKAY		1
#define MSQL_ERROR	2
#define ARG_ERROR	3

struct charge
{
	int none;
	int page;
	int job;
};


int main();
int get_queue_data(struct pps_queue *queue, int *keyval);
int find_valid_charge_types(int keyval, struct charge *charge);
void print_form(struct pps_queue *queue, struct charge *charge, int keyval);


main()
{
	struct pps_queue queue;
	struct charge charge;
	int keyval;


	/* get data for this queue so we can build a form to allow user to update/delete */
	switch(get_queue_data(&queue, &keyval))
	{
		case ARG_ERROR:
			printf("Content-type:  text/html\n\n");
			printf("<HTML><HEAD><TITLE>PPS - Queue Update Error</TITLE></HEAD>\n");
			printf("<BODY TEXT=#FFFFFF BGCOLOR=#000080>\n");
			printf("<CENTER><FONT SIZE=+2>DISPLAY/UPDATE QUEUE INFORMATION</FONT></CENTER><BR><BR>\n");
			printf("<CENTER>\nError parsing submitted data.<BR><BR>\n");
			printf("Queue not modified.<BR><BR>\n");
			printf("Please notify administrator.</CENTER><BR><BR>\n");
			printf("<FORM METHOD=POST ACTION=show_setup.cgi>\n");
			printf("<CENTER><INPUT TYPE=SUBMIT NAME=ACTION VALUE=OK></CENTER>\n");
			printf("</FORM>\n");
			printf("</BODY>\n</HTML>\n");

			exit(0);

		case MSQL_ERROR:
			printf("Content-type:  text/html\n\n");
			printf("<HTML><HEAD><TITLE>PPS - Queue Update Error</TITLE></HEAD>\n");
			printf("<BODY TEXT=#FFFFFF BGCOLOR=#000080>\n");
			printf("<CENTER><FONT SIZE=+2>DISPLAY/UPDATE QUEUE INFORMATION</FONT></CENTER><BR><BR>\n");
			printf("<CENTER>\nError communicating with MSQL server.<BR><BR>\n");
			printf("Queue not modified.<BR><BR>\n");
			printf("Please notify administrator.</CENTER><BR><BR>\n");
			printf("<FORM METHOD=POST ACTION=show_setup.cgi>\n");
			printf("<CENTER><INPUT TYPE=SUBMIT NAME=ACTION VALUE=OK></CENTER>\n");
			printf("</FORM>\n");
			printf("</BODY>\n</HTML>\n");

			exit(0);
	}
	/* */

	/* determine how we can allow the user to modify the charging methods based on attached printer capability */
	switch(find_valid_charge_types(keyval, &charge))
	{
		case ARG_ERROR:
			printf("Content-type:  text/html\n\n");
			printf("<HTML><HEAD><TITLE>PPS - Queue Update Error</TITLE></HEAD>\n");
			printf("<BODY TEXT=#FFFFFF BGCOLOR=#000080>\n");
			printf("<CENTER><FONT SIZE=+2>DISPLAY/UPDATE QUEUE INFORMATION</FONT></CENTER><BR><BR>\n");
			printf("<CENTER>\nError communicating with MSQL server.<BR><BR>\n");
			printf("Queue not modified.<BR><BR>\n");
			printf("Please notify administrator.</CENTER><BR><BR>\n");
			printf("<FORM METHOD=POST ACTION=show_setup.cgi>\n");
			printf("<CENTER><INPUT TYPE=SUBMIT NAME=ACTION VALUE=OK></CENTER>\n");
			printf("</FORM>\n");
			printf("</BODY>\n</HTML>\n");

			exit(0);
	}
	/* */

	/* create a form that allows the user to update properties of this queue */
	print_form(&queue, &charge, keyval);
	/* */
}



int get_queue_data(struct pps_queue *queue, int *keyval)
{
	int sock;
	m_result *result;
	m_row row;
	char string[1024];
	char *query_string;


	/* get the queue index value passed to us */
	query_string = getenv("QUERY_STRING");
	if (strlen(query_string) == 0)
		return ARG_ERROR;
	if (sscanf(query_string, "%d", keyval) == 0)
		return ARG_ERROR;
	/* */

	/* connect to server */
	sock = mconnect();
	if (sock == -1)
		return MSQL_ERROR;
	/* */

	/* try to get information for this queue */
	sprintf(string, "SELECT name,description,type,charge,status FROM queues WHERE keyval=%d", *keyval);
	result = send_query(sock, string);
	if (msql_err)
		return MSQL_ERROR;
	/* */

	/* ensure we got a result */
	if (msqlNumRows(result) != 1)
		return ARG_ERROR;
	/* */

	/* get our data from this record */
	row = fetch_result(result);
	strcpy(queue->name, row[0]);
	strcpy(queue->description, row[1]);
	sscanf(row[2], "%d", &queue->type);
	sscanf(row[3], "%f", &queue->charge);
	sscanf(row[4], "%d", &queue->status);
	/* */

	end_query(result);
	mclose(sock);

	return OKAY;
}



int find_valid_charge_types(int keyval, struct charge *charge)
{
	int sock;
	m_result *result;
	m_row row;
	int num_printers, x, type;
	char string[1024];


	/* start all types as valid */
	charge->none = 1;
	charge->page = 1;
	charge->job = 1;
	/* */

	/* connect to MSQL */
	sock = mconnect();
	if (sock == -1)
		return MSQL_ERROR;
	/* */

	/* fetch all printers attached to this queue */
	sprintf(string, "SELECT type FROM printers WHERE queue=%d", keyval);
	result = send_query(sock, string);
	num_printers = msqlNumRows(result);
	/* */

	/* go through all printers, determining which charging methods can be supported */
	for (x=0; x < num_printers; x++)
	{
		row = fetch_result(result);

		sscanf(row[0], "%d", &type);

		switch(type)
		{
			case PPS_PRINTER_GENERIC:
				charge->page = 0;
				break;
			case PPS_PRINTER_PJL_JETDIRECT:
				break;
		}
	}
	/* */

	/* close connection to MSQL server */
	end_query(result);
	mclose(sock);
	/* */

	return OKAY;
}



void print_form(struct pps_queue *queue, struct charge *charge, int keyval)
{
	printf("Content-type:  text/html\n\n");

	printf("<HTML>\n<HEAD><TITLE>PPS - SHOW QUEUE INFORMATION</TITLE></HEAD>\n");
	printf("<BODY TEXT=#FFFFFF BGCOLOR=#000080>\n");
	printf("<CENTER><FONT SIZE=+2>DISPLAY/UPDATE QUEUE INFORMATION</FONT></CENTER><BR><BR>\n");
	printf("<FORM METHOD=POST ACTION=update_queue.cgi>\n");
	printf("<CENTER><TABLE BORDER=1 CELLSPACING=0 CELLPADDING=0 WIDTH=100% BGCOLOR=#000080>\n");

	printf("<TR BGCOLOR=#87CEFA>\n");
	printf("<TD BGCOLOR=#FFFFFF><FONT COLOR=#000000>NAME</FONT></TD>\n");
	printf("<TD><INPUT NAME=name SIZE=32 MAXLENGTH=32 VALUE=\"%s\"></TD>\n", queue->name);
	printf("</TR>\n");

	printf("<TR BGCOLOR=#87CEFA>\n");
	printf("<TD BGCOLOR=#FFFFFF><FONT COLOR=#000000>DESCRIPTION</FONT></TD>\n");
	printf("<TD><INPUT NAME=description SIZE=48 MAXLENGTH=64 VALUE=\"%s\"></TD>\n", queue->description);
	printf("</TR>\n");

	printf("<TR BGCOLOR=#87CEFA>\n");
	printf("<TD BGCOLOR=#FFFFFF><FONT COLOR=#000000>TYPE</FONT></TD>\n");
	printf("<TD><FONT COLOR=#0000FF><SELECT NAME=type>\n");

	if (charge->none)
	{
		printf("<OPTION");
		if (queue->type == PPS_CHARGE_NONE)
			printf(" SELECTED");
		printf(" VALUE=%d> No charge\n", PPS_CHARGE_NONE);
	}

	if (charge->page)
	{
		printf("<OPTION");
		if (queue->type == PPS_CHARGE_PAGE)
			printf(" SELECTED");
		printf(" VALUE=%d> Charge per page\n", PPS_CHARGE_PAGE);
	}

	if (charge->job)
	{
		printf("<OPTION");
		if (queue->type == PPS_CHARGE_JOB)
			printf(" SELECTED");
		printf(" VALUE=%d> Charge per job\n", PPS_CHARGE_JOB);
	}

	printf("</SELECT></FONT>\n");
	printf("</TR>\n");

	printf("<TR BGCOLOR=#87CEFA>\n");
	printf("<TD BGCOLOR=#FFFFFF><FONT COLOR=#000000>CHARGE</FONT></TD>\n");
	printf("<TD><INPUT NAME=charge SIZE=5 MAXLENGTH=5 VALUE=\"%0.2f\"></TD>\n", queue->charge);
	printf("</TR>\n");

	printf("<TR BGCOLOR=#87CEFA>\n");
	printf("<TD BGCOLOR=#FFFFFF><FONT COLOR=#000000>STATUS</FONT></TD>\n");
	printf("<TD><FONT COLOR=#0000FF>");
	if (queue->status == 0)
		printf("<INPUT TYPE=RADIO NAME=status VALUE=1> UP <INPUT CHECKED TYPE=RADIO NAME=status VALUE=0> DOWN");
	else
		printf("<INPUT CHECKED TYPE=RADIO NAME=status VALUE=1> UP <INPUT TYPE=RADIO NAME=status VALUE=0> DOWN");
	printf("</FONT></TD>\n");
	printf("</TR>\n");
	printf("</TABLE>\n");

	printf("<INPUT TYPE=HIDDEN NAME=keyval VALUE=%s>\n", getenv("QUERY_STRING"));

	printf("<BR><BR>\n");
	printf("<CENTER>\n");
	printf("<INPUT TYPE=SUBMIT NAME=ACTION VALUE=\"UPDATE QUEUE\">\n");
	if (queue->status == 0)
		printf("<INPUT TYPE=SUBMIT NAME=ACTION VALUE=\"DELETE QUEUE\">\n");
	printf("</CENTER>\n");

	printf("</FORM>\n");

	printf("<FORM METHOD=POST ACTION=show_setup.cgi>\n");
	printf("<CENTER><INPUT TYPE=SUBMIT NAME=ACTION VALUE=BACK></CENTER>\n");
	printf("</FORM>\n");

	printf("</BODY>\n");
	printf("</HTML>\n");
}

