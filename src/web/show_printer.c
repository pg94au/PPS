/*  show_printer.c
 *
 *  Determine the type of printer to be viewed, and then execute the device specific code to display
 *  the printer information, based on its type.
 *
 *  (c)1998 Paul Grebenc
 *
 *  $Header: /usr/local/PPS_RCS/src/web/show_printer.c,v 1.1 1999/12/10 03:31:07 paul Exp $
 */


#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#include "pps.h"
#include "parse.h"
#include "database.h"

#define OKAY		0
#define MSQL_ERROR	1
#define ARG_ERROR	2

int main();
int get_printer_type(int *type);
int exec_printer_specific(int type, char *errtxt);


main()
{
	int type, err;
	char errtxt[4096];


	/* figure out what kind of printer we are looking at */
	err = get_printer_type(&type);

	if (err != OKAY)
	{
		switch(err)
		{
			case MSQL_ERROR:
				printf("Content-type:  text/html\n\n");
				printf("<HTML>\n<HEAD><TITLE>PPS - Printer Update/Delete Error</TITLE></HEAD>\n\n");
				printf("<BODY TEXT=#FFFFFF BGCOLOR=#000080>\n");
				printf("<CENTER>\n<FONT SIZE=+2>UPDATE/DELETE PRINTER</FONT><BR><BR>\n");
				printf("Error communicating with MSQL server.<BR><BR>\n");
				printf("Notify administrator.\n</CENTER>\n");

				printf("<FORM METHOD=POST ACTION=show_setup.cgi>\n");
				printf("<CENTER><INPUT TYPE=SUBMIT NAME=ACTION VALUE=OK></CENTER>\n");
				printf("</FORM>\n");

				printf("</BODY>\n</HTML>\n");

				exit(0);
			case ARG_ERROR:
				printf("Content-type:  text/html\n\n");
				printf("<HTML>\n<HEAD><TITLE>PPS - Printer Update/Delete Error</TITLE></HEAD>\n\n");
				printf("<BODY TEXT=#FFFFFF BGCOLOR=#000080>\n");
				printf("<CENTER>\n<FONT SIZE=+2>UPDATE/DELETE PRINTER</FONT><BR><BR>\n");
				printf("Error parsing submitted printer selection.<BR><BR>\n");
				printf("Notify administrator.\n</CENTER>\n");

				printf("<FORM METHOD=POST ACTION=show_setup.cgi>\n");
				printf("<CENTER><INPUT TYPE=SUBMIT NAME=ACTION VALUE=OK></CENTER>\n");
				printf("</FORM>\n");

				printf("</BODY>\n</HTML>\n");

				exit(0);
		}
	}
	/* */

	/* attempt to execute the proper binary, based on the type of printer selected */
	if (exec_printer_specific(type, errtxt) == -1)
	{
		printf("Content-type:  text/html\n\n");
		printf("<HTML>\n<HEAD><TITLE>PPS - Printer Update/Delete Error</TITLE></HEAD>\n\n");
		printf("<BODY TEXT=#FFFFFF BGCOLOR=#000080>\n");
		printf("<CENTER>\n<FONT SIZE=+2>UPDATE/DELETE PRINTER</FONT><BR><BR>\n");
		printf("Error executing printer specific routines.<BR><BR>\n");
		if (strlen(errtxt) > 0)
			printf("%s<BR><BR>\n", errtxt);
		printf("Notify administrator.\n</CENTER>\n");

		printf("<FORM METHOD=POST ACTION=show_setup.cgi>\n");
		printf("<CENTER><INPUT TYPE=SUBMIT NAME=ACTION VALUE=OK></CENTER>\n");
		printf("</FORM>\n");

		printf("</BODY>\n</HTML>\n");

		exit(0);
	}
	/* */
}



int get_printer_type(int *printer_type)
{
	int sock;
	m_result *result;
	m_row row;
	char string[1024];
	int printer_index;
	char *query_string;
	int x;


	/* get the printer index value passed to us */
	query_string = getenv("QUERY_STRING");
	if (strlen(query_string) == 0)
		return ARG_ERROR;
	for (x=0; x<strlen(query_string); x++)
		if (!isdigit(query_string[x]))
			return ARG_ERROR;
	sscanf(query_string, "%d", &printer_index);
	/* */

	/* connect to server */
	sock = mconnect();
	if (sock == -1)
		return MSQL_ERROR;
	/* */

	/* try to find the type of this given printer */
	sprintf(string, "SELECT type,keyval FROM printers WHERE keyval=%d", printer_index);
	result = send_query(sock, string);
	if (msql_err)
		return MSQL_ERROR;
	/* */

	/* ensure we got a result */
	if (msqlNumRows(result) != 1)
		return ARG_ERROR;
	/* */

	/* get type of printer from result */
	row = fetch_result(result);
	sscanf(row[0], "%d", printer_type);
	/* */

	return OKAY;
}



int exec_printer_specific(int type, char *errtxt)
{
	char path[1024];

	switch(type)
	{
		case PPS_PRINTER_GENERIC:
			printf("Content-type:  text/html\n\n");
			printf("<HTML>\n<HEAD><TITLE>PPS - Printer Update/Delete Error</TITLE></HEAD>\n\n");
			printf("<BODY TEXT=#FFFFFF BGCOLOR=#000080>\n");
			printf("<CENTER>\n<FONT SIZE=+2>UPDATE/DELETE PRINTER</FONT><BR><BR>\n");
			printf("Generic printer type not yet implemented.<BR><BR>\n");
			printf("You should not have been able to cause this error...<BR><BR>\n");
			printf("Notify administrator.\n</CENTER>\n");

			printf("<FORM METHOD=POST ACTION=show_setup.cgi>\n");
			printf("<CENTER><INPUT TYPE=SUBMIT NAME=ACTION VALUE=OK></CENTER>\n");
			printf("</FORM>\n");

			printf("</BODY>\n</HTML>\n");

			exit(0);

		case PPS_PRINTER_PJL_JETDIRECT:
			sprintf(path, "%s/web_bin/pjl_jetdirect/show_pjl_jetdirect.cgi", ROOT);
			fflush(stdout);
			if (execl(path, path, (char *)0) == -1)
			{
				sprintf(errtxt, "[%s - %s]", path, strerror(errno));

				return -1;
			}

		default:
			printf("Content-type:  text/html\n\n");
			printf("<HTML>\n<HEAD><TITLE>PPS - Printer Update/Delete Error</TITLE></HEAD>\n\n");
			printf("<BODY TEXT=#FFFFFF BGCOLOR=#000080>\n");
			printf("<CENTER>\n<FONT SIZE=+2>UPDATE/DELETE PRINTER</FONT><BR><BR>\n");
			printf("Unknown printer type.<BR><BR>\n");
			printf("You should not have been able to cause this error...<BR><BR>\n");
			printf("Notify administrator.\n</CENTER>\n");

			printf("<FORM METHOD=POST ACTION=show_setup.cgi>\n");
			printf("<CENTER><INPUT TYPE=SUBMIT NAME=ACTION VALUE=OK></CENTER>\n");
			printf("</FORM>\n");

			printf("</BODY>\n</HTML>\n");

			exit(0);
	}
}

