/*  add_printer.c
 *
 *  Present a form to the user in order to add a new printer to an existing queue.
 *
 *  (c)1998 Paul Grebenc
 *
 *  $Header: /usr/local/PPS_RCS/src/web/add_printer.c,v 1.1 1999/12/10 03:29:17 paul Exp $
 */


#include <stdio.h>
#include <stdlib.h>

#include "pps.h"
#include "database.h"

#define OKAY		1
#define MSQL_ERROR	2
#define ARG_ERROR	3

int main();
void print_form(void);
int get_queue_charge_type(int *charge_type);
void print_tmp(void);

FILE *tmp;


main()
{
	/* open a temporary file to contain all the data sent to the web client */
	tmp = tmpfile();
	if (tmp == NULL)
	{
		printf("Content-type:  text/html\n\n");
		printf("<HTML>\n<HEAD><TITLE>PPS - Error</TITLE></HEAD>\n\n");
		printf("<BODY TEXT=\"#FFFFFF\" BGCOLOR=\"#000080\">");
		printf("<CENTER>\n<FONT SIZE=+2>PPS</FONT><BR><BR>\n");
		printf("Error creating a temporary file.<BR><BR>\nNotify administrator.\n</CENTER>\n");
		printf("</BODY>\n</HTML>\n");

		exit(0);
	}
	/* */

	/* create a form that allows the user to enter model non-specific information about the printer */
	print_form();
	/* */

	/* send the created document to the user's browser */
	print_tmp();
	/* */
}



void print_form()
{
	int charge_type;


	fprintf(tmp, "Content-type:  text/html\n\n");

	fprintf(tmp, "<HTML>\n");
	fprintf(tmp, "<HEAD><TITLE>PPS - ADD PRINTER</TITLE></HEAD>\n");

	fprintf(tmp, "<BODY TEXT=#FFFFFF BGCOLOR=#000080>\n");
	fprintf(tmp, "<CENTER><FONT SIZE=+2>CREATE NEW PRINTER</FONT></CENTER>\n");
	fprintf(tmp, "<BR><BR>\n");

	fprintf(tmp, "<FORM METHOD=POST ACTION=continue_add_printer.cgi>\n");

	fprintf(tmp, "<TABLE BORDER=1 CELLSPACING=0 CELLPADDING=0 WIDTH=100% BGCOLOR=#000080>\n");
	fprintf(tmp, "<TR BGCOLOR=#87CEFA>\n");
	fprintf(tmp, "<TD BGCOLOR=#FFFFFF><FONT COLOR=#000000>NAME</FONT></TD>\n");
	fprintf(tmp, "<TD><INPUT NAME=name SIZE=32 MAXLENGTH=32></TD>\n");
	fprintf(tmp, "</TR>\n");

	fprintf(tmp, "<TR BGCOLOR=#87CEFA>\n");
	fprintf(tmp, "<TD BGCOLOR=#FFFFFF><FONT COLOR=#000000>DESCRIPTION</FONT></TD>\n");
	fprintf(tmp, "<TD><INPUT NAME=description SIZE=48 MAXLENGTH=64></TD>\n");
	fprintf(tmp, "</TR>\n");

	switch(get_queue_charge_type(&charge_type))
	{
		case MSQL_ERROR:
			fclose(tmp);

			printf("Content-type: text/html\n\n");
			printf("<HTML><HEAD><TITLE>PPS - Printer addition error</TITLE></HEAD>\n");
			printf("<BODY TEXT=#FFFFFF BGCOLOR=#000080>\n");
			printf("<CENTER><FONT SIZE=+2>CREATE NEW PRINTER</FONT><BR><BR>\n");
			printf("Error communicating with MSQL server.<BR>\n");
			printf("Printer not created.<BR>\n");
			printf("Please notify administrator.<BR><BR>\n");
			printf("<FORM METHOD=POST ACTION=show_setup.cgi>\n");
			printf("<INPUT TYPE=SUBMIT NAME=ACTION VALUE=OK></CENTER>\n");
			printf("</FORM>\n");
			printf("</BODY>\n</HTML>\n");

			exit(0);
		case ARG_ERROR:
			fclose(tmp);

			printf("Content-type: text/html\n\n");
			printf("<HTML><HEAD><TITLE>PPS - Printer addition error</TITLE></HEAD>\n");
			printf("<BODY TEXT=#FFFFFF BGCOLOR=#000080>\n");
			printf("<CENTER><FONT SIZE=+2>CREATE NEW PRINTER</FONT><BR><BR>\n");
			printf("Missing or invalid queue specification.<BR>\n");
			printf("Printer not created.<BR>\n");
			printf("Please notify administrator.<BR><BR>\n");
			printf("<FORM METHOD=GET ACTION=show_setup.cgi>\n");
			printf("<INPUT TYPE=SUBMIT NAME=ACTION VALUE=OK></CENTER>\n");
			printf("</FORM>\n");
			printf("</BODY>\n</HTML>\n");

			exit(0);
		default:
			break;
	}
	fprintf(tmp, "<TR BGCOLOR=#87CEFA>\n");
	fprintf(tmp, "<TD BGCOLOR=#FFFFFF><FONT COLOR=#000000>TYPE</FONT></TD>\n");
	fprintf(tmp, "<TD><FONT COLOR=#0000FF><SELECT NAME=type>\n");
	switch(charge_type)
	{
		case PPS_CHARGE_NONE:
			fprintf(tmp, "<OPTION SELECTED VALUE=\"%d\"> Generic\n", PPS_PRINTER_GENERIC);
			fprintf(tmp, "<OPTION VALUE=\"%d\"> PJL compatible w/ JetDirect interface\n", PPS_PRINTER_PJL_JETDIRECT);
			break;
		case PPS_CHARGE_PAGE:
			fprintf(tmp, "<OPTION VALUE=\"%d\"> PJL compatible w/ JetDirect interface\n", PPS_PRINTER_PJL_JETDIRECT);
			break;
		case PPS_CHARGE_JOB:
			fprintf(tmp, "<OPTION SELECTED VALUE=\"%d\"> Generic\n", PPS_PRINTER_GENERIC);
			fprintf(tmp, "<OPTION VALUE=\"%d\"> PJL compatible w/ JetDirect interface\n", PPS_PRINTER_PJL_JETDIRECT);
			break;
	}
	fprintf(tmp, "</SELECT></FONT>\n");
	fprintf(tmp, "</TR>\n");

	fprintf(tmp, "<TR BGCOLOR=#87CEFA>\n");
	fprintf(tmp, "<TD BGCOLOR=#FFFFFF><FONT COLOR=#000000>STATUS</FONT></TD>\n");
	fprintf(tmp, "<TD><FONT COLOR=#0000FF>\n");
	fprintf(tmp, "<INPUT TYPE=RADIO NAME=status VALUE=1> UP <INPUT CHECKED TYPE=RADIO NAME=status VALUE=0> DOWN\n");
	fprintf(tmp, "</FONT></TD>\n");
	fprintf(tmp, "</TR>\n");

	fprintf(tmp, "</TABLE>\n");

	fprintf(tmp, "<INPUT TYPE=HIDDEN NAME=queue VALUE=\"%s\">\n", getenv("QUERY_STRING"));

	fprintf(tmp, "<BR><BR>\n");
	fprintf(tmp, "<CENTER><INPUT TYPE=SUBMIT NAME=\"ACTION\" VALUE=\"CONTINUE\"></CENTER>\n");
	fprintf(tmp, "</FORM>\n");

	fprintf(tmp, "<FORM METHOD=GET ACTION=show_setup.cgi>\n");
	fprintf(tmp, "<CENTER><INPUT TYPE=SUBMIT NAME=\"ACTION\" VALUE=\"BACK\"></CENTER>\n");
	fprintf(tmp, "</FORM>\n");

	fprintf(tmp, "</BODY>\n");
	fprintf(tmp, "</HTML>\n");
}



int get_queue_charge_type(int *charge_type)
{
	int sock;
	m_result *result;
	m_row row;
	char string[1024];
	int queue_index;
	char *query_string;
	int x;


	/* get the queue index value passed to us */
	query_string = getenv("QUERY_STRING");
	if (strlen(query_string) == 0)
		return ARG_ERROR;
	for (x=0; x<strlen(query_string); x++)
		if (!isdigit(query_string[x]))
			return ARG_ERROR;
	sscanf(query_string, "%d", &queue_index);
	/* */

	/* connect to server */
	sock = mconnect();
	if (sock == -1)
		return MSQL_ERROR;
	/* */

	/* try to find the charge type of the given queue */
	sprintf(string, "SELECT type,keyval FROM queues WHERE keyval=%d", queue_index);
	result = send_query(sock, string);
	if (msql_err)
		return MSQL_ERROR;
	/* */

	/* ensure we got a result */
	if (msqlNumRows(result) != 1)
		return ARG_ERROR;
	/* */

	/* get type of queue from result */
	row = fetch_result(result);
	sscanf(row[0], "%d", charge_type);
	/* */

	return OK;
}



/* print_tmp
**
** Arg:	none
** Ret:	none
**
**	Procedure which rewinds to the beginning of the temporary file to which all output
**	has been sent and sends it all at once to stdout.  The procedure is used so that
**	if an error occurs part way through the creation of a web page, we can display
**	an appropriate error message alone, not having to deal with the fact that half of
**	our now invalid output has already been sent to the client.
*/
void print_tmp()
{
	int c;


	/* print the contents of the current temporary file to stdout */
	rewind(tmp);

	c = fgetc(tmp);
	while(!feof(tmp))
	{
		putchar(c);
		c = fgetc(tmp);
	}

	fclose(tmp);
	/* */
}

