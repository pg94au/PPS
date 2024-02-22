/*  pjl_jetdirect/show_pjl_jetdirect.c
 *
 *  Show the printer setup for a PJL JetDirect printer record.
 *
 *  (c)1998 Paul Grebenc
 *
 *  $Header: /usr/local/PPS_RCS/src/web/pjl_jetdirect/show_pjl_jetdirect.c,v 1.1 1999/12/10 03:32:03 paul Exp $
 */


#include <stdio.h>
#include <stdlib.h>

#include "pps.h"
#include "database.h"
#include "parse.h"

#define OKAY		1
#define MSQL_ERROR	2
#define ARG_ERROR	3

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
int get_printer_data(struct record *record);
void print_form(struct record *record);


main()
{
	struct record record;


	/* get data for this printer so we can build a form to allow user to update/delete */
	switch (get_printer_data(&record))
	{
		case ARG_ERROR:
			printf("Content-type:  text/html\n\n");
			printf("<HTML><HEAD><TITLE>PPS - Printer Update Error</TITLE></HEAD>\n");
			printf("<BODY TEXT=#FFFFFF BGCOLOR=#000080>\n");
			printf("<CENTER><FONT SIZE=+2>DISPLAY/UPDATE PRINTER INFORMATION</FONT></CENTER><BR><BR>\n");
			printf("<CENTER>\nError parsing submitted data.<BR><BR>\n");
			printf("Printer not modified.<BR><BR>\n");
			printf("Please notify administrator.</CENTER><BR><BR>\n");
			printf("<FORM METHOD=POST ACTION=show_setup.cgi>\n");
			printf("<CENTER><INPUT TYPE=SUBMIT NAME=ACTION VALUE=OK></CENTER>\n");
			printf("</FORM>\n");
			printf("</BODY>\n</HTML>\n");

			exit(0);

		case MSQL_ERROR:
			printf("Content-type:  text/html\n\n");
			printf("<HTML><HEAD><TITLE>PPS - Printer Update Error</TITLE></HEAD>\n");
			printf("<BODY TEXT=#FFFFFF BGCOLOR=#000080>\n");
			printf("<CENTER><FONT SIZE=+2>DISPLAY/UPDATE PRINTER INFORMATION</FONT></CENTER><BR><BR>\n");
			printf("<CENTER>\nError communicating with MSQL server.<BR><BR>\n");
			printf("Printer not modified.<BR><BR>\n");
			printf("Please notify administrator.</CENTER><BR><BR>\n");
			printf("<FORM METHOD=POST ACTION=show_setup.cgi>\n");
			printf("<CENTER><INPUT TYPE=SUBMIT NAME=ACTION VALUE=OK></CENTER>\n");
			printf("</FORM>\n");
			printf("</BODY>\n</HTML>\n");

			exit(0);
	}
	/* */

	/* create a form that allows the user to update properties of this printer */
	print_form(&record);
	/* */
}



int get_printer_data(struct record *record)
{
	int sock;
	m_result *result;
	m_row row;
	char string[1024];
	int printer_index;
	char *query_string;


	/* get the printer index value passed to us */
	query_string = getenv("QUERY_STRING");
	if (strlen(query_string) == 0)
		return ARG_ERROR;
	if (sscanf(query_string, "%d", &printer_index) == 0)
		return ARG_ERROR;
	record->keyval = printer_index;
	/* */

	/* connect to server */
	sock = mconnect();
	if (sock == -1)
		return MSQL_ERROR;
	/* */

	/* try to get non-specific data first for this printer */
	sprintf(string, "SELECT name,description,queue,status,keyval FROM printers WHERE keyval=%d", printer_index);
	result = send_query(sock, string);
	if (msql_err)
		return MSQL_ERROR;
	/* */

	/* ensure we got a result */
	if (msqlNumRows(result) != 1)
		return MSQL_ERROR;
	/* */

	/* get our non-specific data from this record */
	row = fetch_result(result);
	strcpy(record->name, row[0]);
	strcpy(record->description, row[1]);
	sscanf(row[2], "%d", &record->queue);
	sscanf(row[3], "%d", &record->status);
	/* */

	/* try to get model-specific data for this printer now */
	sprintf(string, "SELECT address,port,keyval FROM pjl_jetdirect WHERE keyval=%d", printer_index);
	result = send_query(sock, string);
	if (msql_err)
		return MSQL_ERROR;
	/* */

	/* ensure we got a result */
	if (msqlNumRows(result) != 1)
		return MSQL_ERROR;
	/* */

	/* get our model-specific data from this record */
	row = fetch_result(result);
	strcpy(record->address, row[0]);
	sscanf(row[1], "%d", &record->port);
	/* */

	end_query(result);
	mclose(sock);

	return OKAY;
}



void print_form(struct record *record)
{
	printf("Content-type:  text/html\n\n");

	printf("<HTML>\n<HEAD><TITLE>PPS - SHOW PRINTER INFORMATION</TITLE></HEAD>\n");
	printf("<BODY TEXT=#FFFFFF BGCOLOR=#000080>\n");
	printf("<CENTER><FONT SIZE=+2>DISPLAY/UPDATE PRINTER INFORMATION</FONT></CENTER><BR>\n");
	printf("<CENTER><FONT SIZE=-1>PJL COMPATIBLE JETDIRECT</FONT></CENTER><BR><BR>\n");
	printf("<FORM METHOD=POST ACTION=pjl_jetdirect/update_pjl_jetdirect.cgi>\n");
	printf("<CENTER><TABLE BORDER=1 CELLSPACING=0 CELLPADDING=0 WIDTH=100% BGCOLOR=#000080>\n");
	printf("<TR BGCOLOR=#87CEFA>\n");
	printf("<TD BGCOLOR=#FFFFFF><FONT COLOR=#000000>NAME</FONT></TD>\n");
	printf("<TD><INPUT NAME=name SIZE=32 MAXLENGTH=32 VALUE=\"%s\"></TD>\n", record->name);
	printf("</TR>\n");
	printf("<TR BGCOLOR=#87CEFA>\n");
	printf("<TD BGCOLOR=#FFFFFF><FONT COLOR=#000000>DESCRIPTION</FONT></TD>\n");
	printf("<TD><INPUT NAME=description SIZE=48 MAXLENGTH=64 VALUE=\"%s\"></TD>\n", record->description);
	printf("</TR>\n");
	printf("<TR BGCOLOR=#87CEFA>\n");
	printf("<TD BGCOLOR=#FFFFFF><FONT COLOR=#000000>HOSTNAME or IP ADDRESS</FONT></TD>\n");
	printf("<TD><INPUT NAME=address SIZE=48 MAXLENGTH=255 VALUE=\"%s\"></TD>\n", record->address);
	printf("</TR>\n");
	printf("<TR BGCOLOR=#87CEFA>\n");
	printf("<TD BGCOLOR=#FFFFFF><FONT COLOR=#000000>TCP PORT</FONT></TD>\n");
	printf("<TD><INPUT NAME=port SIZE=5 MAXLENGTH=5 VALUE=\"%d\"></TD>\n", record->port);
	printf("</TR>\n");
	printf("<TR BGCOLOR=#87CEFA>\n");
	printf("<TD BGCOLOR=#FFFFFF><FONT COLOR=#000000>STATUS</FONT></TD>\n");
	if (record->status == 1)
		printf("<TD><FONT COLOR=#0000FF><INPUT CHECKED TYPE=RADIO NAME=status VALUE=1> UP <INPUT TYPE=RADIO NAME=status VALUE=0> DOWN</FONT></TD>\n");
	else
		printf("<TD><FONT COLOR=#0000FF><INPUT TYPE=RADIO NAME=status VALUE=1> UP <INPUT CHECKED TYPE=RADIO NAME=status VALUE=0> DOWN</FONT></TD>\n");
	printf("</TR>\n");

	printf("</TABLE></CENTER>\n");

	printf("<INPUT TYPE=HIDDEN NAME=queue VALUE=%d>\n", record->queue);
	printf("<INPUT TYPE=HIDDEN NAME=keyval VALUE=%d>\n", record->keyval);

	printf("<BR><BR>\n<CENTER>\n");
	printf("<INPUT TYPE=SUBMIT NAME=ACTION VALUE=\"UPDATE PRINTER\">\n");
	if (record->status == 0)
		printf("<INPUT TYPE=SUBMIT NAME=ACTION VALUE=\"DELETE PRINTER\">\n");
	printf("</CENTER>\n");

	printf("</FORM>\n");

	printf("<FORM METHOD=POST ACTION=show_setup.cgi><CENTER><INPUT TYPE=SUBMIT NAME=ACTION VALUE=BACK></CENTER></FORM>\n");

	printf("</BODY>\n</HTML>\n");
}

