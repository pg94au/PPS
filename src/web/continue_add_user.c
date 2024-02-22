/*  continue_add_user.c
 *
 *  Parse the add user form submitted by the user, and create a new user record in the database.
 *
 *  (c)1998 Paul Grebenc
 *
 *  $Header: /usr/local/PPS_RCS/src/web/continue_add_user.c,v 1.1 1999/12/10 03:32:40 paul Exp $
 */


#include <stdio.h>
#include <stdlib.h>
#include <values.h>
#include <string.h>

#include "pps.h"
#include "parse.h"
#include "database.h"

#define OKAY		0
#define MSQL_ERROR	1
#define USER_EXISTS	2


struct user
{
	char usercode[9];
	char realname[65];
	int studentid;
	float initbalance;
	int status;
	char notes[81];
};


int main();
int lookup_usercode(char *usercode);
int insert_user(struct user *new_user, char *errtxt);


main()
{
	struct user new_user;
	char errtxt[4096];
	int ins_err;


	/* parse our posted variables */
	if ((parse_all() == -1)
	|| (getenv("CGI_usercode") == NULL)
	|| (getenv("CGI_realname") == NULL)
	|| (getenv("CGI_studentid") == NULL)
	|| (getenv("CGI_initbalance") == NULL)
	|| (getenv("CGI_status") == NULL)
	|| (getenv("CGI_notes") == NULL))
	{
		printf("Content-type:  text/html\n");
		printf("<HTML>\n<HEAD><TITLE>PPS - User Addition Error</TITLE></HEAD>\n\n");
		printf("<BODY TEXT=#FFFFFF BGCOLOR=#000080>");
		printf("<CENTER>\n<FONT SIZE=+2>CREATE NEW USER</FONT><BR><BR>\n");
		printf("Error parsing submitted form.<BR>User not created.<BR><BR>\n");
		printf("Notify administrator.\n</CENTER>\n");

		printf("<FORM METHOD=POST ACTION=user_menu.cgi>\n");
		printf("<CENTER><INPUT TYPE=SUBMIT NAME=ACTION VALUE=OK></CENTER>\n");
		printf("</FORM>\n");

		printf("</BODY>\n</HTML>\n");

		exit(0);
	}
	/* */

	/* get posted data from environment variables */
	strncpy(new_user.usercode, getenv("CGI_usercode"), sizeof(new_user.usercode));
	new_user.usercode[sizeof(new_user.usercode)-1] = '\0';
	strncpy(new_user.realname, getenv("CGI_realname"), sizeof(new_user.realname));
	new_user.realname[sizeof(new_user.realname)-1] = '\0';
	if (strlen(getenv("CGI_studentid")) == 0)
	{
		new_user.studentid = 0;
	}
	else
	{
		if (sscanf(getenv("CGI_studentid"), "%d", &new_user.studentid) == 0)
			new_user.studentid = -1;
	}
	if (strlen(getenv("CGI_initbalance")) == 0)
	{
		new_user.initbalance = 0.00;
	}
	else
	{
		if (sscanf(getenv("CGI_initbalance"), "%f", &new_user.initbalance) == 0)
			new_user.initbalance = -MAXFLOAT;
	}
	if ((strlen(getenv("CGI_status")) == 0) || (sscanf(getenv("CGI_status"), "%d", &new_user.status) == 0))
		new_user.status = -1;
	strncpy(new_user.notes, getenv("CGI_notes"), sizeof(new_user.notes));
	new_user.notes[sizeof(new_user.notes)-1] = '\0';
	/* */

	/* examine the data, to ensure it is acceptable */
	errtxt[0] ='\0';
	if (strlen(new_user.usercode) == 0)
		strcat(errtxt, "Usercode field must not be blank.<BR>\n");
	if (strstr(new_user.usercode, " ") != NULL)
		strcat(errtxt, "Usercode field must not contain spaces.<BR>\n");
	if (lookup_usercode(new_user.usercode))
		strcat(errtxt, "A user by this usercode already exists.<BR>\n");
	if (strlen(new_user.realname) == 0)
		strcat(errtxt, "Real name field must not be blank.<BR>\n");
	if (new_user.studentid == -1)
		strcat(errtxt, "Student id must be a positive integer.<BR>\n");
	if (new_user.initbalance == -MAXFLOAT)
		strcat(errtxt, "Invalid initial balance.<BR>\n");
	if (new_user.status == -1)
	{
		strcat(errtxt, "Invalid user accounting status.<BR>\n");
		strcat(errtxt, "<BR>\nNotify administrator.\n");
	}

	if (strlen(errtxt) > 0)
	{
		printf("Content-type:  text/html\n\n");
		printf("<HTML><HEAD><TITLE>PPS - User Addition Error</TITLE></HEAD>\n");
		printf("<BODY TEXT=#FFFFFF BGCOLOR=#000080>\n");
		printf("<CENTER><FONT SIZE=+2>CREATE NEW USER</FONT><BR><BR>\n");

		printf(errtxt);
		printf("<BR>Queue not created.<BR>\n");
		printf("</CENTER><BR>\n");

		printf("<FORM METHOD=POST ACTION=user_menu.cgi>\n");
		printf("<CENTER><INPUT TYPE=SUBMIT NAME=ACTION VALUE=OK></CENTER>\n");
		printf("</FORM>\n");

		printf("</BODY>\n</HTML>\n");

		exit(0);
	}
	/* */

	/* everything passed to us looks ok, so we will try to create this user */
	if ((ins_err = insert_user(&new_user, errtxt)) == OKAY)
	{
		/* we added the user ok, so we redirect the user back to the main screen */
		printf("Location:  user_menu.cgi\n\n");
		/* */
	}
	else
	{
		/* inform the user of errors that occured */
		printf("Content-type:  text/html\n\n");
		printf("<HTML><HEAD><TITLE>PPS - User Addition Error</TITLE></HEAD>\n");
		printf("<BODY TEXT=#FFFFFF BGCOLOR=#000080>\n");
		printf("<CENTER><FONT SIZE=+2>CREATE NEW USER</FONT></CENTER><BR><BR>\n");

		switch(ins_err)
		{
			case MSQL_ERROR:
				printf("<CENTER>\nError communicating with MSQL server.<BR><BR>\n");
				printf("Queue not created.<BR><BR>\n");
				printf("Please notify administrator.</CENTER><BR><BR>\n");
				break;
			case USER_EXISTS:
				printf("<CENTER>\nA user by this usercode already exists in the system.<BR><BR>\n");
				printf("Duplicate user not created.</CENTER><BR><BR>\n");
				break;
		}

		printf("<FORM METHOD=POST ACTION=user_menu.cgi>\n");
		printf("<CENTER><INPUT TYPE=SUBMIT NAME=ACTION VALUE=OK></CENTER>\n");
		printf("</FORM>\n");

		printf("</BODY>\n</HTML>\n");
		/* */
	}
	/* */
}



int lookup_usercode(char *usercode)
{
	int sock;
	m_result *result;
	m_row row;
	char string[1024];
	int numqueues;


	/* connect to server */
	sock = mconnect();
	if (sock == -1)
		return MSQL_ERROR;
	/* */

	/*** check to see whether this usercode already exists ***/
	sprintf(string, "SELECT usercode FROM users WHERE usercode='%s'", usercode);
	result = send_query(sock, string);
	if (msqlNumRows(result) > 0)
	{
		mclose(sock);

		return USER_EXISTS;
	}
	/*** ***/

	return OKAY;
}



int insert_user(struct user *new_user, char *errtxt)
{
	int sock;
	m_result *result;
	m_row row;
	char string[1024];
	char esc_realname[129], esc_notes[161];


	/* connect to server */
	sock = mconnect();
	if (sock == -1)
		return MSQL_ERROR;
	/* */

	/* check to see if this usercode already exists in the system */
	sprintf(string, "SELECT usercode FROM users WHERE usercode='%s'", new_user->usercode);
	result = send_query(sock, string);
	if (msqlNumRows(result) > 0)
	{
		end_query(result);
		mclose(sock);
		return USER_EXISTS;
	}
	/* */

	/* try to insert the user record */
	escape_string(esc_realname, new_user->realname);
	escape_string(esc_notes, new_user->notes);
	sprintf(string, "INSERT INTO users (usercode,name,id,balance,status,notes) VALUES ('%s','%s',%d,%0.2f,%d,'%s')",
		new_user->usercode, esc_realname, new_user->studentid, new_user->initbalance, new_user->status, esc_notes);
	result = send_query(sock, string);
	if (msql_err)
	{
		mclose(sock);
		return MSQL_ERROR;
	}

	mclose(sock);

	return OKAY;
}

