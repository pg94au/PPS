/*
 *  show_balance.c 
 *
 *  Allow users to display the balance in their print accounts.
 *
 *  (c)1998,1999 Paul Grebenc
 *
 *  $Header: /usr/local/PPS_RCS/src/web_user/show_balance.c,v 1.1 1999/12/10 03:23:32 paul Exp $
 */


#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <shadow.h>
#include <unistd.h>
#include <netdb.h>

#include "pps.h"
#include "database.h"
#include "../web/parse.h"

#define OKAY		1
#define MSQL_ERROR	2
#define ARG_ERROR	3

struct user
{
	char usercode[9];
	char realname[65];
	int studentid;
	float balance;
	int status;
	char notes[81];
};


void do_print_balance(char *usercode);
int get_user_data(struct user *user, char *usercode);
void print_balance(struct user *user);


void do_print_balance(char *usercode)
{
	struct user user;


	/* get data for this user so we can display the user's account balance */
	switch(get_user_data(&user, usercode))
	{
		case ARG_ERROR:
			printf("Content-type:  text/html\n\n");
			printf("<HTML>\n<HEAD><TITLE>PPS - USER BALANCE CHECK ERROR</TITLE></HEAD>\n\n");
			printf("<BODY TEXT=#FFFFFF BGCOLOR=#000080>\n");
			printf("<CENTER>\n<FONT SIZE=+2>PRINT ACCOUNTING SYSTEM</FONT><BR><BR>\n");
			printf("<FONT COLOR=#C0C0C0>USER BALANCE CHECK</FONT><BR><BR>\n");
			printf("<HR><BR>\n");
			printf("User '%s' does not have a printing account setup.<BR>\n", usercode);
			printf("<BR><HR></CENTER>\n");

			printf("<FORM METHOD=POST ACTION=default.cgi>\n");
			printf("<CENTER><INPUT TYPE=SUBMIT NAME=ACTION VALUE=OK></CENTER>\n");
			printf("</FORM>\n");

			printf("</BODY>\n</HTML>\n");

			exit(0);

		case MSQL_ERROR:
			printf("Content-type:  text/html\n\n");
			printf("<HTML>\n<HEAD><TITLE>PPS - USER BALANCE CHECK ERROR</TITLE></HEAD>\n\n");
			printf("<BODY TEXT=#FFFFFF BGCOLOR=#000080>\n");
			printf("<CENTER>\n<FONT SIZE=+2>PRINT ACCOUNTING SYSTEM</FONT><BR><BR>\n");
			printf("<FONT COLOR=#C0C0C0>USER BALANCE CHECK</FONT><BR><BR>\n");
			printf("<HR><BR>\n");
			printf("Error communicating with MSQL server.<BR>\n");
			printf("Unable to display user balance.<BR><BR>\n");
			printf("Please notify administrator.\n");
			printf("<BR><HR></CENTER>\n");

			printf("</BODY>\n</HTML>\n");

			exit(0);
	}
	/* */

	/* create a form that allows the user to update properties of this user */
	print_balance(&user);
	/* */
}



int get_user_data(struct user *user, char *usercode)
{
	int sock;
	m_result *result;
	m_row row;
	char string[1024];


	/* connect to server */
	sock = mconnect();
	if (sock == -1)
		return MSQL_ERROR;
	/* */

	/* try to get information for this user */
	sprintf(string, "SELECT usercode,name,id,balance,status,notes FROM users WHERE usercode='%s'", usercode);
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
	strcpy(user->usercode, row[0]);
	strcpy(user->realname, row[1]);
	sscanf(row[2], "%d", &user->studentid);
	sscanf(row[3], "%f", &user->balance);
	sscanf(row[4], "%d", &user->status);
	strcpy(user->notes, row[5]);
	/* */

	end_query(result);
	mclose(sock);

	return OKAY;
}



void print_balance(struct user *user)
{
	printf("Content-type:  text/html\n\n");

	printf("<HTML>\n");
	printf("<HEAD>\n");
	printf("<TITLE>PPS - USER BALANCE CHECK</TITLE>\n");
	printf("</HEAD>\n");

	printf("<BODY TEXT=#FFFFFF BGCOLOR=#000080 LINK=#0000FF VLINK=#0000FF ALINK=#0000FF>\n");
	printf("<CENTER>\n");
	printf("<P><B><FONT COLOR=#FFFFFF><FONT SIZE=+2>PRINT ACCOUNTING SYSTEM</FONT></FONT></B></P>\n");
	printf("</CENTER>\n");

	printf("<CENTER><P><FONT COLOR=#C0C0C0>USER BALANCE CHECK</FONT></P></CENTER>\n");

	printf("<BR>\n");
	printf("<HR>\n");
	printf("<BR>\n");

	printf("<CENTER>\n");
	printf("<TABLE BORDER=1 CELLSPACING=0 CELLPADDING=0 WIDTH=100% BGCOLOR=#000080>\n");
	printf("<TR BGCOLOR=#87CEFA>\n");
	printf("<TD BGCOLOR=#FFFFFF><FONT COLOR=#000000>USERCODE</FONT></TD>\n");
	printf("<TD><FONT COLOR=#000000>%s</FONT></TD>\n", user->usercode);
	printf("</TR>\n");

	printf("<TR BGCOLOR=#87CEFA>\n");
	printf("<TD BGCOLOR=#FFFFFF><FONT COLOR=#000000>REAL NAME</FONT></TD>\n");
	printf("<TD><FONT COLOR=#000000>%s</FONT></TD>\n", user->realname);
	printf("</TR>\n");

	printf("<TR BGCOLOR=#87CEFA>\n");
	printf("<TD BGCOLOR=#FFFFFF><FONT COLOR=#000000>STUDENT ID</FONT></TD>\n");
	printf("<TD><FONT COLOR=#000000>%d</FONT></TD>\n", user->studentid);
	printf("</TR>\n");

	printf("<TR BGCOLOR=#87CEFA>\n");
	printf("<TD BGCOLOR=#FFFFFF><FONT COLOR=#000000>CURRENT BALANCE</FONT></TD>\n");
	printf("<TD><FONT COLOR=#000000>%0.2f</FONT></TD>\n", user->balance);
	printf("</TR>\n");

	printf("<TR BGCOLOR=#87CEFA>\n");
	printf("<TD BGCOLOR=#FFFFFF><FONT COLOR=#000000>ACCOUNTING STATUS</FONT></TD>\n");
	if (user->status ==  PPS_ACCOUNTING_ACTIVE)
		printf("<TD><FONT COLOR=#000000>Page accounting active</FONT></TD>\n");
	else
		printf("<TD><FONT COLOR=#000000>Page accounting inactive</FONT></TD>\n");
	printf("</TR>\n");

	printf("</TABLE>\n");

	printf("<BR><HR><BR>\n");

	printf("<CENTER><FONT COLOR=#C0C0C0><FONT SIZE=-2>(C)1998 Paul Grebenc</FONT></FONT></CENTER>\n");

	printf("</BODY>\n");

	printf("</HTML>\n");
}

