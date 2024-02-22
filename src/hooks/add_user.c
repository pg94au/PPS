/*
 *  add_user.c
 *
 *  Command line utility to allow for the addition of users to the database without
 *  having to go through the web interface.  This utility can be used as a more
 *  efficient way to add a large number of users in one batch operation than having
 *  to add each individually through the web interface.  While the web interface is
 *  simple enough and friendly enough to be used by anyone, this utility is designed
 *  for use by administrators.
 *
 *  (c)1998 Paul Grebenc
 *
 *  $Header: /usr/local/PPS_RCS/src/hooks/add_user.c,v 1.1 1999/12/10 03:35:38 paul Exp $
 */


#include <stdio.h>


#include "../msql/database.h"
#include "../pps.h"


int main();
void get_information(void);
void insert_user(void);


char usercode[9];
char realname[65];
int id;
float initbalance;
int status;
char notes[81];


main()
{
	get_information();

	insert_user();
}



void get_information()
{
	char temp[256];


	/* get information from user */
	printf("PPS - Create New User Account\n\n");
	printf("Enter the following information:\n\n");

	printf("Usercode [8]: ");
	gets(temp);
	strncpy(usercode, temp, 8);

	printf("Real name (Last, First) [64]: ");
	gets(temp);
	strncpy(realname, temp, 64);

	printf("Student ID [7]: ");
	gets(temp);
	sscanf(temp, "%d", &id);

	printf("Initial balance [###.##]: ");
	gets(temp);
	sscanf(temp, "%f", &initbalance);

	printf("Accounting enabled (y/n): ");
	gets(temp);
	if ((temp[0] == 'y') || (temp[0] == 'Y'))
		status = 1;
	else
		status = 0;

	printf("Notes [80]: ");
	gets(temp);
	strncpy(notes, temp, 80);
	/* */

	/* check information for errors */
	if (strlen(usercode)==0)
	{
		fprintf(stderr, "Usercode field must not be blank.  User record not created.\n\n");
		exit(0);
	}
	if ((char *)strstr(usercode, " ") != NULL)
	{
		fprintf(stderr, "Usercode field must not contain spaces.  User record not created.\n\n");
		exit(0);
	}
	if (strlen(realname)==0)
	{
		fprintf(stderr, "Real name field must not be blank.  User not created.\n\n");
		exit(0);
	}
	/* */
}



void insert_user()
{
	int sock;
	m_result *result;
	char string[1024];


	/* connect to server */
	sock = mconnect();
	if (sock == -1)
	{
		fprintf(stderr, "Error connecting to mSQL daemon.  User not created.\n\n");
		exit(0);
	}
	/* */

	/* first, check to see if this usercode already exists */
	sprintf(string, "SELECT usercode FROM users WHERE usercode='%s'", usercode);
	result = send_query(sock, string);
	if (msqlNumRows(result) > 0)
	{
		mclose(sock);

		fprintf(stderr, "A user with this usercode already exists.  Duplicate user not created.\n\n");
		exit(0);
	}
	/* */

	/* second, try to insert the new user record */
	sprintf(string, "INSERT INTO users (usercode,name,id,balance,status,notes) VALUES ('%s','%s',%d,%0.2f,%d,'%s')",
		usercode, realname, id, initbalance, status, notes);
	result = send_query(sock, string);

	if (msql_err)
	{
		mclose(sock);

		fprintf(stderr, "Error inserting new record.  Check status of mSQL daemon and pps database.\n\n");
		exit(0);
	}
	/* */

	printf("User %s inserted into database.\n\n", usercode);
}

