/*  add_queue.c
 *
 *  Present a form to the user allowing a new queue to be created in the system.
 *
 *  (c)1998 Paul Grebenc
 *
 *  $Header: /usr/local/PPS_RCS/src/web/add_queue.c,v 1.1 1999/12/10 03:29:05 paul Exp $
 */


#include <stdio.h>
#include <stdlib.h>

#include "pps.h"


int main();
void print_form();



main()
{
	/* create a form that allows the user to enter information about the queue to create */
	print_form();
	/* */
}



void print_form()
{
	printf("Content-type:  text/html\n\n");

	printf("<HTML>\n");

	printf("<HEAD>\n");
	printf("<TITLE>PPS - ADD QUEUE</TITLE>\n");
	printf("</HEAD>\n");

	printf("<BODY TEXT=#FFFFFF BGCOLOR=#000080>\n");
	printf("<CENTER><FONT SIZE=+2>CREATE NEW QUEUE</FONT></CENTER><BR><BR>\n");
	printf("<FORM METHOD=POST ACTION=continue_add_queue.cgi>\n");
	printf("<TABLE BORDER=1 CELLSPACING=0 CELLPADDING=0 WIDTH=100% BGCOLOR=#000080>\n");
	printf("<TR BGCOLOR=#87CEFA>\n");
	printf("<TD BGCOLOR=#FFFFFF><FONT COLOR=#000000>NAME</FONT></TD>\n");
	printf("<TD><INPUT NAME=name SIZE=32 MAXLENGTH=32></TD>\n");
	printf("</TR>\n");

	printf("<TR BGCOLOR=#87CEFA>\n");
	printf("<TD BGCOLOR=#FFFFFF><FONT COLOR=#000000>DESCRIPTION</FONT></TD>\n");
	printf("<TD><INPUT NAME=description SIZE=48 MAXLENGTH=64></TD>\n");
	printf("</TR>\n");

	printf("<TR BGCOLOR=#87CEFA>\n");
	printf("<TD BGCOLOR=#FFFFFF><FONT COLOR=#000000>TYPE</FONT></TD>\n");
	printf("<TD><FONT COLOR=#0000FF><SELECT NAME=type>\n");
	printf("<OPTION SELECTED VALUE=%d> No charge\n", PPS_CHARGE_NONE);
	printf("<OPTION VALUE=%d> Charge per page\n", PPS_CHARGE_PAGE);
	printf("<OPTION VALUE=%d> Charge per job\n", PPS_CHARGE_JOB);
	printf("</SELECT></FONT>\n");
	printf("</TR>\n");

	printf("<TR BGCOLOR=#87CEFA>\n");
	printf("<TD BGCOLOR=#FFFFFF><FONT COLOR=#000000>CHARGE</FONT></TD>\n");
	printf("<TD><INPUT NAME=charge SIZE=5 MAXLENGTH=5 VALUE=\"0.00\"></TD>\n");
	printf("</TR>\n");

	printf("<TR BGCOLOR=#87CEFA>\n");
	printf("<TD BGCOLOR=#FFFFFF><FONT COLOR=#000000>STATUS</FONT></TD>\n");
	printf("<TD><FONT COLOR=#0000FF><INPUT TYPE=HIDDEN NAME=status VALUE=0>DOWN</FONT></TD>\n");
	printf("</TR>\n");

	printf("</TABLE><BR><BR>\n");
	printf("</CENTER>\n");

	printf("<CENTER><INPUT TYPE=SUBMIT NAME=ACTION VALUE=\"CREATE QUEUE\"></CENTER>\n");
	printf("</FORM>\n");
	printf("<FORM METHOD=POST ACTION=show_setup.cgi>\n");
	printf("<CENTER><INPUT TYPE=SUBMIT NAME=ACTION VALUE=\"BACK\"></CENTER>\n");
	printf("</FORM>\n");
	printf("</BODY>\n");
	printf("</HTML>\n");
}

