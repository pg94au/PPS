/*  show_user.c
 *
 *  Display the current setup of a specific user in the database.
 *
 *  (c)1998 Paul Grebenc
 *
 *  $Header: /usr/local/PPS_RCS/src/web/show_user.c,v 1.1 1999/12/10 03:33:20 paul Exp $
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pps.h"
#include "database.h"
#include "parse.h"

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


int main();
int get_user_data(struct user *user, char *usercode);
void print_form(struct user *user);


main()
{
	struct user user;
	char usercode[9];


	/* parse our posted variables */
	if ((parse_all() == -1)
	|| (getenv("CGI_usercode1") == NULL)
	|| (getenv("CGI_usercode2") == NULL))
	{
		printf("Content-type:  text/html\n\n");
		printf("<HTML>\n<HEAD><TITLE>PPS - User Update Error</TITLE></HEAD>\n\n");
		printf("<BODY TEXT=#FFFFFF BGCOLOR=#000080>\n");
		printf("<CENTER>\n<FONT SIZE=+2>DISPLAY/UPDATE USER INFORMATION</FONT><BR><BR>\n");
		printf("Error parsing submitted form.<BR>User not modified.<BR><BR>\n");
		printf("Notify administrator.\n</CENTER>\n");

		printf("<FORM METHOD=POST ACTION=user_menu.cgi>\n");
		printf("<CENTER><INPUT TYPE=SUBMIT NAME=ACTION VALUE=OK></CENTER>\n");
		printf("</FORM>\n");

		printf("</BODY>\n</HTML>\n");

		exit(0);
	}
	/* */

	/* determine which usercode we prefer (the one entered in the provided field has priority) */
	if (strlen(getenv("CGI_usercode1")) > 0)
	{
		strncpy(usercode, getenv("CGI_usercode1"), sizeof(usercode));
		usercode[sizeof(usercode)-1] = '\0';
	}
	else
	{
		if (strlen(getenv("CGI_usercode2")) > 0)
		{
			strncpy(usercode, getenv("CGI_usercode2"), sizeof(usercode));
			usercode[sizeof(usercode)-1] = '\0';
		}
		else
		{
			printf("Content-type:  text/html\n\n");
			printf("<HTML>\n<HEAD><TITLE>PPS - User Update Error</TITLE></HEAD>\n\n");
			printf("<BODY TEXT=#FFFFFF BGCOLOR=#000080>\n");
			printf("<CENTER>\n<FONT SIZE=+2>DISPLAY/UPDATE USER INFORMATION</FONT><BR><BR>\n");
			printf("A usercode must be entered in the provided field or chosen from the list.<BR>\n");
			printf("User not modified.<BR><BR>\n");
			printf("</CENTER>\n");

			printf("<FORM METHOD=POST ACTION=user_menu.cgi>\n");
			printf("<CENTER><INPUT TYPE=SUBMIT NAME=ACTION VALUE=OK></CENTER>\n");
			printf("</FORM>\n");

			printf("</BODY>\n</HTML>\n");

			exit(0);
		}
	}
	/* */

	/* get data for this user so we can build a form to allow user to update/delete */
	switch(get_user_data(&user, usercode))
	{
		case ARG_ERROR:
			printf("Content-type:  text/html\n\n");
			printf("<HTML>\n<HEAD><TITLE>PPS - User Update Error</TITLE></HEAD>\n\n");
			printf("<BODY TEXT=#FFFFFF BGCOLOR=#000080>\n");
			printf("<CENTER>\n<FONT SIZE=+2>DISPLAY/UPDATE USER INFORMATION</FONT><BR><BR>\n");
			printf("No user '%s' exists in the system.<BR>\n", usercode);
			printf("User not modified.<BR><BR>\n");
			printf("</CENTER>\n");

			printf("<FORM METHOD=POST ACTION=user_menu.cgi>\n");
			printf("<CENTER><INPUT TYPE=SUBMIT NAME=ACTION VALUE=OK></CENTER>\n");
			printf("</FORM>\n");

			printf("</BODY>\n</HTML>\n");

			exit(0);

		case MSQL_ERROR:
			printf("Content-type:  text/html\n\n");
			printf("<HTML>\n<HEAD><TITLE>PPS - User Update Error</TITLE></HEAD>\n\n");
			printf("<BODY TEXT=#FFFFFF BGCOLOR=#000080>\n");
			printf("<CENTER>\n<FONT SIZE=+2>DISPLAY/UPDATE USER INFORMATION</FONT><BR><BR>\n");
			printf("Error communicating with MSQL server.<BR>\n");
			printf("User not modified.<BR><BR>\n");
			printf("Please notify administrator.</CENTER><BR><BR>\n");

			printf("<FORM METHOD=POST ACTION=user_menu.cgi>\n");
			printf("<CENTER><INPUT TYPE=SUBMIT NAME=ACTION VALUE=OK></CENTER>\n");
			printf("</FORM>\n");

			printf("</BODY>\n</HTML>\n");

			exit(0);
	}
	/* */

	/* create a form that allows the user to update properties of this user */
	print_form(&user);
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



void print_form(struct user *user)
{
	printf("Content-type:  text/html\n\n");

	printf("<HTML>\n");

	printf("<HEAD>\n");
	printf("<TITLE>PPS - SHOW USER INFORMATION</TITLE>\n");
	printf("</HEAD>\n");

	printf("<BODY TEXT=#FFFFFF BGCOLOR=#000080>\n");
	printf("<CENTER><FONT SIZE=+2>DISPLAY/UPDATE USER INFORMATION</FONT></CENTER><BR><BR>\n");
	printf("<FORM METHOD=POST ACTION=update_user.cgi>\n");
	printf("<TABLE BORDER=1 CELLSPACING=0 CELLPADDING=0 WIDTH=100% BGCOLOR=#000080>\n");
	printf("<TR BGCOLOR=#87CEFA>\n");
	printf("<TD BGCOLOR=#FFFFFF><FONT COLOR=#000000>USERCODE</FONT></TD>\n");
	printf("<TD><INPUT NAME=usercode SIZE=8 MAXLENGTH=8 VALUE=\"%s\"></TD>\n", user->usercode);
	printf("</TR>\n");

	printf("<TR BGCOLOR=#87CEFA>\n");
	printf("<TD BGCOLOR=#FFFFFF><FONT COLOR=#000000>NAME</FONT></TD>\n");
	printf("<TD><INPUT NAME=name SIZE=48 MAXLENGTH=64 VALUE=\"%s\"></TD>\n", user->realname);
	printf("</TR>\n");

	printf("<TR BGCOLOR=#87CEFA>\n");
	printf("<TD BGCOLOR=#FFFFFF><FONT COLOR=#000000>STUDENT ID</FONT></TD>\n");
	printf("<TD><INPUT NAME=id SIZE=8 MAXLENGTH=8 VALUE=\"%d\"></TD>\n", user->studentid);
	printf("</TR>\n");

	printf("<TR BGCOLOR=#87CEFA>\n");
	printf("<TD BGCOLOR=#FFFFFF><FONT COLOR=#000000>CURRENT BALANCE</FONT></TD>\n");
	printf("<TD><INPUT NAME=balance SIZE=6 MAXLENGTH=6 VALUE=\"%0.2f\"></TD>\n", user->balance);
	printf("</TR>\n");

	printf("<TR BGCOLOR=#87CEFA>\n");
	printf("<TD BGCOLOR=#FFFFFF><FONT COLOR=#000000>ACCOUNTING ACTIVE</FONT></TD>\n");
	if (user->status == 1)
		printf("<TD><FONT COLOR=#0000FF><INPUT CHECKED TYPE=RADIO NAME=status VALUE=1> YES <INPUT TYPE=RADIO NAME=status VALUE=0> NO</FONT></TD>\n");
	else
		printf("<TD><FONT COLOR=#0000FF><INPUT TYPE=RADIO NAME=status VALUE=1> YES <INPUT CHECKED TYPE=RADIO NAME=status VALUE=0> NO</FONT></TD>\n");
	printf("</TR>\n");

	printf("<TR BGCOLOR=#87CEFA>\n");
	printf("<TD BGCOLOR=#FFFFFF><FONT COLOR=#000000>NOTES</FONT></TD>\n");
	printf("<TD><INPUT NAME=notes SIZE=48 MAXLENGTH=80 VALUE=\"%s\"></TD>\n", user->notes);
	printf("</TR>\n");

	printf("</TABLE><BR><BR>\n");
	printf("</CENTER>\n");

	printf("<INPUT TYPE=HIDDEN NAME=oldusercode VALUE=\"%s\">\n", user->usercode);
	printf("<INPUT TYPE=HIDDEN NAME=oldbalance VALUE=\"%0.2f\">\n", user->balance);
	printf("<CENTER>\n");
	printf("<INPUT TYPE=SUBMIT NAME=ACTION VALUE=\"UPDATE USER\">\n");
	printf("<INPUT TYPE=SUBMIT NAME=ACTION VALUE=\"REMOVE USER\">\n");
	printf("</CENTER>\n");
	printf("</FORM>\n");

	printf("<FORM METHOD=GET ACTION=user_menu.cgi>\n");
	printf("<CENTER>\n");
	printf("<INPUT TYPE=SUBMIT NAME=ACTION VALUE=BACK>\n");
	printf("</CENTER>\n");
	printf("</FORM>\n");

	printf("</BODY>\n");
	printf("</HTML>\n");
}

