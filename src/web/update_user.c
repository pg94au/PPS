/*  update_user.c
 *
 *  Parse the user setup form and update the user record in the database.
 *
 *  (c)1998 Paul Grebenc
 *
 *  $Header: /usr/local/PPS_RCS/src/web/update_user.c,v 1.1 1999/12/10 03:33:32 paul Exp $
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <values.h>
#include <errno.h>

#include "pps.h"
#include "database.h"
#include "parse.h"

#define UPDATE		1
#define DELETE		2
#define ARG_ERROR	3
#define MSQL_ERROR	4
#define USER_EXISTS	5
#define OKAY		6

struct record
{
	char usercode[9];
	char realname[65];
	int studentid;
	float balance;
	float oldbalance;
	int status;
	char notes[81];
	char oldusercode[9];
};

int main();
int examine_posted(struct record *record);
int lookup_usercode(char *new, char *old);
void delete_user(struct record *record);
void update_user(struct record *record);


main()
{
	struct record record;
	int result;


	/* get the data posted to us from the update form, and do with it what is required */
	switch (examine_posted(&record))
	{
		case UPDATE:
			/* update this user's database record (program exits on error) */
			update_user(&record);

			/* if we get here, update the admin logs */
			admin_log(record.usercode, &record.oldbalance, &record.balance);

			exit(0);
		case DELETE:
			delete_user(&record);
			exit(0);
		case ARG_ERROR:
			printf("Content-type:  text/html\n\n");
			printf("<HTML>\n<HEAD><TITLE>PPS - Queue Update Error</TITLE></HEAD>\n\n");
			printf("<BODY TEXT=\"#FFFFFF\" BGCOLOR=\"#000080\">");
			printf("<CENTER>\n<FONT SIZE=+2>DISPLAY/UPDATE USER INFORMATION</FONT><BR><BR>\n");
			printf("Error with submitted arguments.<BR>User not modified.<BR><BR>\n");
			printf("Notify administrator.\n</CENTER>\n");

			printf("<FORM METHOD=POST ACTION=user_menu.cgi>\n");
			printf("<CENTER><INPUT TYPE=SUBMIT NAME=ACTION VALUE=OK></CENTER>\n");
			printf("</FORM>\n");

			printf("</BODY>\n</HTML>\n");

			exit(0);
	}
	/* */
}



int examine_posted(struct record *record)
{
	int action;
	char errtxt[4096];
	int notify_admin = 0;
	int lookup_result;


	/* parse all posted variables */
	if ((parse_all() == -1)
	|| (getenv("CGI_usercode") == NULL)
	|| (getenv("CGI_name") == NULL)
	|| (getenv("CGI_id") == NULL)
	|| (getenv("CGI_balance") == NULL)
	|| (getenv("CGI_oldbalance") == NULL)
	|| (getenv("CGI_status") == NULL)
	|| (getenv("CGI_notes") == NULL)
	|| (getenv("CGI_oldusercode") == NULL))
	{
		printf("Content-type:  text/html\n\n");
		printf("<HTML>\n<HEAD><TITLE>PPS - User Update Error</TITLE></HEAD>\n\n");
		printf("<BODY TEXT=\"#FFFFFF\" BGCOLOR=\"#000080\">");
		printf("<CENTER>\n<FONT SIZE=+2>DISPLAY/UPDATE USER INFORMATION</FONT><BR><BR>\n");
		printf("Error with submitted arguments.<BR>User not modified.<BR><BR>\n");
		printf("Notify administrator.\n</CENTER>\n");

		printf("<FORM METHOD=POST ACTION=user_menu.cgi>\n");
		printf("<CENTER><INPUT TYPE=SUBMIT NAME=ACTION VALUE=OK></CENTER>\n");
		printf("</FORM>\n");

		printf("</BODY>\n</HTML>\n");

		exit(0);
	}
	/* */

	/* get posted data from environment variables */
	strncpy(record->usercode, getenv("CGI_usercode"), sizeof(record->usercode));
	record->usercode[sizeof(record->usercode)-1] = '\0';
	strncpy(record->realname, getenv("CGI_name"), sizeof(record->realname));
	record->realname[sizeof(record->realname)-1] = '\0';
	if ((sscanf(getenv("CGI_id"), "%d", &record->studentid) == 0))
		record->studentid = 0;
	if (strlen(getenv("CGI_balance")) == 0)
		record->balance = -MAXFLOAT;
	else
	{
		if (sscanf(getenv("CGI_balance"), "%f", &record->balance) == 0)
			record->balance = -MAXFLOAT;
	}
	if (strlen(getenv("CGI_oldbalance")) == 0)
		record->oldbalance = -MAXFLOAT;
	else
	{
		if (sscanf(getenv("CGI_oldbalance"), "%f", &record->oldbalance) == 0)
			record->oldbalance = -MAXFLOAT;
	}
	if ((sscanf(getenv("CGI_status"), "%d", &record->status) == 0))
		record->status = -1;
	strncpy(record->notes, getenv("CGI_notes"), sizeof(record->notes));
	record->notes[sizeof(record->notes)-1] = '\0';
	strncpy(record->oldusercode, getenv("CGI_oldusercode"), sizeof(record->oldusercode));
	record->oldusercode[sizeof(record->oldusercode)-1] = '\0';
	if (strcmp("UPDATE USER", getenv("CGI_ACTION")) == 0)
		action = UPDATE;
	else
	{
		if (strcmp("REMOVE USER", getenv("CGI_ACTION")) == 0)
			action = DELETE;
		else
			action = -1;
	}
	/* */

	/* check first if we are deleting this queue, because if we are we don't really need to worry about a lot of the fields sent to us */
	if ((action == DELETE) && (strlen(record->usercode) > 0))
		return DELETE;
	/* */

	/* examine the data given to us, to ensure it is acceptable */
	errtxt[0] = '\0';
	if (strlen(record->usercode) == 0)
		strcat(errtxt, "Usercode field must not be blank.<BR>\n");
	if (strstr(record->usercode, " ") != NULL)
		strcat(errtxt, "Usercode field must not contain spaces.<BR>\n");
	if (strlen(record->realname) == 0)
		strcat(errtxt, "Real name field must not be blank.<BR>\n");
	switch (lookup_usercode(record->usercode, record->oldusercode))
	{
		case USER_EXISTS:
			strcat(errtxt, "A user by this usercode already exists.<BR>\n");
			break;
		case MSQL_ERROR:
			printf("Content-type:  text/html\n\n");
			printf("<HTML>\n<HEAD><TITLE>PPS - User Update Error</TITLE></HEAD>\n\n");
			printf("<BODY TEXT=\"#FFFFFF\" BGCOLOR=\"#000080\">");
			printf("<CENTER>\n<FONT SIZE=+2>DISPLAY/UPDATE USER INFORMATION</FONT><BR><BR>\n");
			printf("Error communicating with MSQL server.<BR>User not modified.<BR><BR>\n");
			printf("Please notify administrator.\n</CENTER>\n");

			printf("<FORM METHOD=POST ACTION=user_menu.cgi>\n");
			printf("<CENTER><INPUT TYPE=SUBMIT NAME=ACTION VALUE=OK></CENTER>\n");
			printf("</FORM>\n");

			printf("</BODY>\n</HTML>\n");

			exit(0);
	}
	if (record->studentid == -1)
	{
		strcat(errtxt, "Student id must be a positive integer.<BR>\n");
		notify_admin = 1;
	}
	if (record->balance == -MAXFLOAT)
		strcat(errtxt, "Invalid balance amount submitted.<BR>\n");
	if (record->status == -1)
	{
		strcat(errtxt, "Invalid status selection submitted.<BR>\n");
		notify_admin = 1;
	}
	if (action != UPDATE)
	{
		strcat(errtxt, "Invalid action requested.<BR>\n");
		notify_admin = 1;
	}

	if (strlen(errtxt) > 0)
	{
		printf("Content-type:  text/html\n\n");
		printf("<HTML><HEAD><TITLE>PPS - User Update Error</TITLE></HEAD>\n");
		printf("<BODY TEXT=#FFFFFF BGCOLOR=#000080>\n");
		printf("<CENTER><FONT SIZE=+2>DISPLAY/UPDATE USER INFORMATION</FONT><BR><BR>\n");

		printf(errtxt);
		printf("<BR>User not modified.<BR>\n");
		if (notify_admin == 1)
			printf("<BR>Notify administrator.<BR>\n");
		printf("</CENTER><BR>\n");

		printf("<FORM METHOD=POST ACTION=user_menu.cgi>\n");
		printf("<CENTER><INPUT TYPE=SUBMIT NAME=ACTION VALUE=OK></CENTER>\n");
		printf("</FORM>\n");

		printf("</BODY>\n</HTML>\n");

		exit(0);
	}
	/* */

	return UPDATE;
}



int lookup_usercode(char *new, char *old)
{
	int sock;
	m_result *result;
	m_row row;
	char string[1024];


	/* if it hasn't changed, everything is fine */
	if (strcmp(new, old) == 0)
		return OKAY;
	/* */

	/* connect to server */
	sock = mconnect();
	if (sock == -1)
		return MSQL_ERROR;
	/* */

	/* check if a user by this usercode already exists in the system */
	sprintf(string, "SELECT usercode FROM users WHERE usercode='%s'", new);
	result = send_query(sock, string);
	if (msql_err)
		return MSQL_ERROR;
	if (msqlNumRows(result) > 0)
	{
		mclose(sock);

		return USER_EXISTS;
	}
	/* */

	mclose(sock);

	return OKAY;
}



void delete_user(struct record *record)
{
	int sock;
	m_result *result;
	char string[1024];


	/* connect to server */
	sock = mconnect();
	if (sock == -1)
	{
		printf("Content-type:  text/html\n\n");
		printf("<HTML>\n<HEAD><TITLE>PPS - User Update Error</TITLE></HEAD>\n\n");
		printf("<BODY TEXT=\"#FFFFFF\" BGCOLOR=\"#000080\">");
		printf("<CENTER>\n<FONT SIZE=+2>DISPLAY/UPDATE USER INFORMATION</FONT><BR><BR>\n");
		printf("Error communicating with MSQL server.<BR>User not deleted.<BR><BR>\n");
		printf("Please notify administrator.\n</CENTER>\n");

		printf("<FORM METHOD=POST ACTION=user_menu.cgi>\n");
		printf("<CENTER><INPUT TYPE=SUBMIT NAME=ACTION VALUE=OK></CENTER>\n");
		printf("</FORM>\n");

		printf("</BODY>\n</HTML>\n");

		exit(0);
	}
	/* */

	/* try to delete the record for this user */
	sprintf(string, "DELETE FROM users WHERE usercode='%s'", record->usercode);
	result = send_query(sock, string);

	if (msql_err)
	{
		printf("Content-type:  text/html\n\n");
		printf("<HTML>\n<HEAD><TITLE>PPS - Queue Update Error</TITLE></HEAD>\n\n");
		printf("<BODY TEXT=\"#FFFFFF\" BGCOLOR=\"#000080\">");
		printf("<CENTER>\n<FONT SIZE=+2>DISPLAY/UPDATE QUEUE INFORMATION</FONT><BR><BR>\n");
		printf("Error attempting to delete user from database.<BR>Queue not deleted.<BR><BR>\n");
		printf("</CENTER>\n");

		printf("<FORM METHOD=POST ACTION=show_setup.cgi>\n");
		printf("<CENTER><INPUT TYPE=SUBMIT NAME=ACTION VALUE=OK></CENTER>\n");
		printf("</FORM>\n");

		printf("</BODY>\n</HTML>\n");

		exit(0);
	}
	/* */

	/* we deleted it ok, so we redirect the user back to the main screen */
	printf("Location:  user_menu.cgi\n\n");
	/* */
}



void update_user(struct record *record)
{
	int sock;
	m_result *result;
	char string[1024];
	char esc_realname[129], esc_notes[161];


	/* connect to server */
	sock = mconnect();
	if (sock == -1)
	{
		printf("Content-type:  text/html\n\n");
		printf("<HTML>\n<HEAD><TITLE>PPS - User Update Error</TITLE></HEAD>\n\n");
		printf("<BODY TEXT=\"#FFFFFF\" BGCOLOR=\"#000080\">");
		printf("<CENTER>\n<FONT SIZE=+2>DISPLAY/UPDATE USER INFORMATION</FONT><BR><BR>\n");
		printf("Error communicating with MSQL server.<BR>User not updated.<BR><BR>\n");
		printf("Please notify administrator.\n</CENTER>\n");

		printf("<FORM METHOD=POST ACTION=user_menu.cgi>\n");
		printf("<CENTER><INPUT TYPE=SUBMIT NAME=ACTION VALUE=OK></CENTER>\n");
		printf("</FORM>\n");

		printf("</BODY>\n</HTML>\n");

		exit(0);
	}
	/* */

	/* try to update the record for this queue */
	escape_string(esc_realname, record->realname);
	escape_string(esc_notes, record->notes);
	sprintf(string, "UPDATE users SET usercode='%s',name='%s',id=%d,balance=%0.2f,status=%d,notes='%s' WHERE usercode='%s'",
		record->usercode, esc_realname, record->studentid, record->balance, record->status, esc_notes, record->oldusercode);
	result = send_query(sock, string);
	if (msql_err)
	{
		mclose(sock);

		printf("Content-type:  text/html\n\n");
		printf("<HTML>\n<HEAD><TITLE>PPS - User Update Error</TITLE></HEAD>\n\n");
		printf("<BODY TEXT=\"#FFFFFF\" BGCOLOR=\"#000080\">");
		printf("<CENTER>\n<FONT SIZE=+2>DISPLAY/UPDATE USER INFORMATION</FONT><BR><BR>\n");
		printf("Error communicating with MSQL server.<BR>User not updated.<BR><BR>\n");
		printf("Please notify administrator.\n</CENTER>\n");

		printf("<FORM METHOD=POST ACTION=user_menu.cgi>\n");
		printf("<CENTER><INPUT TYPE=SUBMIT NAME=ACTION VALUE=OK></CENTER>\n");
		printf("</FORM>\n");

		printf("</BODY>\n</HTML>\n");

		exit(0);
	}
	/* */

 	mclose(sock);

	/* we updated it ok, so we redirect the user back to the main screen */
	printf("Location:  user_menu.cgi\n\n");
	/* */
}

