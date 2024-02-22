/*  user_menu.c
 *
 *  Show a list of current users in the system, and allow for their update, or the creation of a new user.
 *
 *  (c)1998 Paul Grebenc
 *
 *  $Header: /usr/local/PPS_RCS/src/web/user_menu.c,v 1.1 1999/12/10 03:30:15 paul Exp $
 */


#include <stdio.h>

#include "database.h"
#include "pps.h"


int main();
void fancy(int count);
void insert_names(void);


main()
{
	FILE *fp;
	char line[1024];
	int count;

	printf("Content-type: text/html\n\n");

	fp = fopen("html/user_menu.html","r");
	if (fp==NULL)
	{
		printf("<HTML>\n<HEAD><TITLE>Error</TITLE></HEAD>\n\n");
		printf("<BODY>\nError opening file for read.\n</BODY>\n</HTML>\n");

		exit(1);
	}

	count = 0;
	fgets(line,1024,fp);
	while(!feof(fp))
	{
		if (strncmp("<>",line,2)==0)
		{
			fancy(count++);	/* put fancy stuff in */
		}
		else
		{
			line[strlen(line)-1]='\0';
			puts(line);
		}

		fgets(line,1024,fp);
	}
}



void fancy(int count)
{
	switch(count)
	{
		case 0:
			insert_names();
			break;
	}
}



void insert_names()
{
	int sock;
	m_result *result;
	m_row row;


	/* connect to MSQL */
	sock = mconnect();
	if (sock == -1)
		return;
	/* */

	/* get all of the user names out of the database */
	result = send_query(sock, "SELECT usercode, name, id FROM users ORDER BY name, id");
	/* */

	/* print out the results in a list in the form */
	for (;;)
	{
		row = fetch_result(result);
		if (row != NULL)
		{
			if (strcmp(row[2],"0")==0)
				printf("<OPTION VALUE=\"%s\">%s [%s]\n", row[0], row[1], row[0]);
			else
				printf("<OPTION VALUE=\"%s\">%s [%s] (%s)\n", row[0], row[1], row[0], row[2]);
		}
		else
		{
			break;
		}
	}
	/* */

	/* close database */
	end_query(result);
	mclose(sock);
	/* */
}

