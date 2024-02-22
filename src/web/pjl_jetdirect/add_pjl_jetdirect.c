/*  pjl_jetdirect/add_pjl_jetdirect.c
 *
 *  Present a form for the user to enter the device dependent information for a PJL JetDirect printer.
 *
 *  (c)1998 Paul Grebenc
 *
 *  $Header: /usr/local/PPS_RCS/src/web/pjl_jetdirect/add_pjl_jetdirect.c,v 1.1 1999/12/10 03:31:30 paul Exp $
 */


#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>

#include "pps.h"
#include "database.h"
#include "parse.h"

int main();
void print_specific_form(void);
void print_tmp(void);


FILE *tmp;


main()
{
	/* open a temporary file to contain all the data sent to the web client */
	tmp = tmpfile();
	if (tmp == NULL)
	{
		printf("Content-type:  text/html\n\n");
		printf("<HTML>\n<HEAD><TITLE>PPS - Printer Addition Error</TITLE></HEAD>\n\n");
		printf("<BODY TEXT=#FFFFFF BGCOLOR=#000080>\n");
		printf("<CENTER>\n<FONT SIZE=+2>CREATE NEW PRINTER</FONT><BR><BR>\n");
		printf("Error creating a temporary file.<BR>Printer not created.<BR><BR>\n");
		printf("Notify administrator.\n</CENTER>\n");
		printf("</BODY>\n</HTML>\n");

		exit(0);
	}
	/* */

	/* create a form that allows the user to enter model specific information about the printer */
	print_specific_form();
	/* */

	/* send the created document to the user's browser */
	print_tmp();
	/* */
}



void print_specific_form()
{
	fprintf(tmp, "Content-type:  text/html\n\n");

	fprintf(tmp, "<HTML>\n");
	fprintf(tmp, "<HEAD><TITLE>PPS - CREATE NEW PRINTER</TITLE></HEAD>\n");

	fprintf(tmp, "<BODY TEXT=#FFFFFF BGCOLOR=#000080>\n");
	fprintf(tmp, "<CENTER><FONT SIZE=+2>CREATE NEW PRINTER</FONT></CENTER><BR>\n");
	fprintf(tmp, "<CENTER><FONT SIZE=-1>PJL COMPATIBLE JETDIRECT</FONT></CENTER>\n");

	fprintf(tmp, "<FORM METHOD=POST ACTION=pjl_jetdirect/continue_add_pjl_jetdirect.cgi>\n");

	fprintf(tmp, "<TABLE BORDER=1 CELLSPACING=0 CELLPADDING=0 WIDTH=100% BGCOLOR=#000080>\n");
	fprintf(tmp, "<TR BGCOLOR=#87CEFA>\n");
	fprintf(tmp, "<TD BGCOLOR=#FFFFFF><FONT COLOR=#000000>NAME</FONT></TD>\n");
	fprintf(tmp, "<TD><FONT COLOR=#000000>%s</FONT></TD>\n", getenv("CGI_name"));
	fprintf(tmp, "</TR>\n");

	fprintf(tmp, "<TR BGCOLOR=#87CEFA>\n");
	fprintf(tmp, "<TD BGCOLOR=#FFFFFF><FONT COLOR=#000000>DESCRIPTION</FONT></TD>\n");
	fprintf(tmp, "<TD><FONT COLOR=#000000>%s</FONT></TD>\n", getenv("CGI_description"));
	fprintf(tmp, "</TR>\n");

	fprintf(tmp, "<TR BGCOLOR=#87CEFA>\n");
	fprintf(tmp, "<TD BGCOLOR=#FFFFFF><FONT COLOR=#000000>STATUS</FONT></TD>\n");
	if (strcmp(getenv("CGI_status"), "1") == 0)
		fprintf(tmp, "<TD><FONT COLOR=#000000>UP</FONT></TD>\n");
	else
		fprintf(tmp, "<TD><FONT COLOR=#000000>DOWN</FONT></TD>\n");
	fprintf(tmp, "</TR>\n");

	fprintf(tmp, "<TR BGCOLOR=#87CEFA>\n");
	fprintf(tmp, "<TD BGCOLOR=#FFFFFF><FONT COLOR=#000000>HOST NAME or<BR>IP ADDRESS</FONT></TD>\n");
	fprintf(tmp, "<TD><INPUT NAME=address SIZE=48 MAXLENGTH=255></TD>\n");
	fprintf(tmp, "</TR>\n");

	fprintf(tmp, "<TR BGCOLOR=#87CEFA>\n");
	fprintf(tmp, "<TD BGCOLOR=#FFFFFF><FONT COLOR=#000000>TCP PORT</FONT></TD>\n");
	fprintf(tmp, "<TD><INPUT NAME=port VALUE=9100 SIZE=5 MAXLENGTH=5></TD>\n");
	fprintf(tmp, "</TR>\n");

	fprintf(tmp, "</TABLE>\n");

	fprintf(tmp, "<BR><BR>\n");

	fprintf(tmp, "<INPUT TYPE=HIDDEN NAME=name VALUE=\"%s\">\n", getenv("CGI_name"));
	fprintf(tmp, "<INPUT TYPE=HIDDEN NAME=description VALUE=\"%s\">\n", getenv("CGI_description"));
	fprintf(tmp, "<INPUT TYPE=HIDDEN NAME=type VALUE=\"%s\">\n", getenv("CGI_type"));
	fprintf(tmp, "<INPUT TYPE=HIDDEN NAME=status VALUE=\"%s\">\n", getenv("CGI_status"));
	fprintf(tmp, "<INPUT TYPE=HIDDEN NAME=queue VALUE=\"%s\">\n", getenv("CGI_queue"));

	fprintf(tmp, "<CENTER><INPUT TYPE=SUBMIT NAME=ACTION VALUE=\"CREATE PRINTER\"></CENTER>\n");
	fprintf(tmp, "</FORM>\n");

	fprintf(tmp, "<FORM METHOD=POST ACTION=show_setup.cgi>\n");
	fprintf(tmp, "<CENTER><INPUT TYPE=SUBMIT NAME=ACTION VALUE=CANCEL></CENTER>\n");
	fprintf(tmp, "</FORM>\n");

	fprintf(tmp, "</BODY>\n");
	fprintf(tmp, "</HTML>\n");
}



/* print_tmp
**
** Arg: none
** Ret: none
**
**      Procedure which rewinds to the beginning of the temporary file to which all output
**      has been sent and sends it all at once to stdout.  The procedure is used so that
**      if an error occurs part way through the creation of a web page, we can display
**      an appropriate error message alone, not having to deal with the fact that half of
**      our now invalid output has already been sent to the client.
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

