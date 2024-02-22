/*  add_user.c
 *
 *  Present a form allowing the addition of a new user to the system.
 *
 *  (c)1998 Paul Grebenc
 *
 *  $Header: /usr/local/PPS_RCS/src/web/add_user.c,v 1.1 1999/12/10 03:29:47 paul Exp $
 */


#include <stdio.h>
#include <stdlib.h>

#include "pps.h"


int main();
void print_form();


main()
{
	/* create a form that allows the user to enter information about the user to create */
	print_form();
	/* */
}


void print_form()
{
	printf("Content-type:  text/html\n\n");

	printf("<HTML>\n");

	printf("<HEAD>\n");
	printf("<TITLE>PPS - ADD USER</TITLE>\n");
	printf("</HEAD>\n");

	printf("<BODY TEXT=#FFFFFF BGCOLOR=#000080>\n");
	printf("<CENTER><FONT SIZE=+2>CREATE NEW USER</FONT></CENTER><BR><BR>\n");
	printf("<FORM METHOD=POST ACTION=continue_add_user.cgi>\n");
	printf("<TABLE BORDER=1 CELLSPACING=0 CELLSPADDING=0 WIDTH=100% BGCOLOR=#000080>\n");
	printf("<TR BGCOLOR=#87CEFA>\n");
	printf("<TD BGCOLOR=#FFFFFF><FONT COLOR=#000000>USERCODE</FONT></TD>\n");
	printf("<TD><INPUT NAME=\"usercode\" SIZE=8 MAXLENGTH=8></TD>\n");
	printf("</TR>\n");

	printf("<TR BGCOLOR=#87CEFA>\n");
	printf("<TD BGCOLOR=#FFFFFF><FONT COLOR=#000000>REAL NAME (Last, First)</FONT></TD>\n");
	printf("<TD><INPUT NAME=\"realname\" SIZE=48 MAXLENGTH=64></TD>\n");
	printf("</TR>\n");

	printf("<TR BGCOLOR=#87CEFA>\n");
	printf("<TD BGCOLOR=#FFFFFF><FONT COLOR=#000000>STUDENT ID</FONT></TD>\n");
	printf("<TD><INPUT NAME=\"studentid\" SIZE=7 MAXLENGTH=7></TD>\n");
	printf("</TR>\n");

	printf("<TR BGCOLOR=#87CEFA>\n");
	printf("<TD BGCOLOR=#FFFFFF><FONT COLOR=#000000>INITIAL BALANCE</FONT></TD>\n");
	printf("<TD><INPUT NAME=\"initbalance\" SIZE=6 MAXLENGTH=6 VALUE=\"0.00\"></TD>\n");
	printf("</TR>\n");

	printf("<TR BGCOLOR=#87CEFA>\n");
	printf("<TD BGCOLOR=#FFFFFF><FONT COLOR=#000000>ACCOUNTING ACTIVE</FONT></TD>\n");
	printf("<TD><FONT COLOR=#0000FF><INPUT CHECKED TYPE=RADIO NAME=\"status\" VALUE=1> YES <INPUT TYPE=RADIO NAME=\"status\" VALUE=0> NO</FONT></TD>\n");
	printf("</TR>\n");

	printf("<TR BGCOLOR=#87CEFA>\n");
	printf("<TD BGCOLOR=#FFFFFF><FONT COLOR=#000000>NOTES</FONT></TD>\n");
	printf("<TD><INPUT NAME=\"notes\" SIZE=48 MAXLENGTH=80></TD>\n");
	printf("</TR>\n");

	printf("</TABLE><BR><BR>\n");
	printf("</CENTER>\n");

	printf("<CENTER><INPUT TYPE=SUBMIT NAME=ACTION VALUE=\"CREATE USER\"></CENTER>\n");
	printf("</FORM>\n");

	printf("<FORM METHOD=POST ACTION=user_menu.cgi>\n");
	printf("<CENTER><INPUT TYPE=SUBMIT NAME=ACTION VALUE=BACK></CENTER>\n");
	printf("</FORM>\n");

	printf("</BODY>\n");
	printf("</HTML>\n");
}

