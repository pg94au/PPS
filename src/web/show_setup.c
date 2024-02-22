/*  show_setup.c
 *
 *  Show the association of queues and printers in the database.
 *
 *  (c)1998 Paul Grebenc
 *
 *  $Header: /usr/local/PPS_RCS/src/web/show_setup.c,v 1.1 1999/12/10 03:28:49 paul Exp $
 */


#include <stdio.h>

#include "database.h"

#include "pps.h"


int main();
void fancy(int count);
void show_data(void);
void print_tmp(void);

FILE *tmp;


main()
{
	FILE *fp;
	char line[1024];
	int count;


	/* open a temporary file to contain all the data sent to the web client */
	tmp = tmpfile();
	if (tmp == NULL)
	{
		printf("Content-type:  text/html\n\n");
		printf("<HTML>\n<HEAD><TITLE>PPS - Error</TITLE></HEAD>\n\n");
		printf("<BODY TEXT=\"#FFFFFF\" BGCOLOR=\"#000080\">\n");
		printf("<CENTER>\n<FONT SIZE=+2>PPS</FONT><BR><BR>\n");
		printf("Error creating a temporary file.<BR><BR>\nNotify administrator.\n</CENTER>\n</BODY>\n</HTML>\n");

		exit(0);
	}
	/* */

	fprintf(tmp, "Content-type:  text/html\n\n");

	/* open the HTML source file */
	fp = fopen("html/show_setup.html", "r");
	if (fp == NULL)
	{
		fclose(tmp);

		printf("Content-type:  text/html\n\n");
		printf("<HTML>\n<HEAD><TITLE>Error</TITLE></HEAD>\n\n");
		printf("<BODY TEXT=\"#FFFFFF\" BGCOLOR=\"#000080\">\n");
		printf("<CENTER>\n<FONT SIZE=+2>PPS</FONT><BR><BR>\n");
		printf("Error opening HTML source file.<BR><BR>\nNotify administrator.\n</CENTER>\n");
		printf("</BODY>\n</HTML>\n");

		exit(0);
	}
	/* */

	/* spit out HTML source file, stopping at the <> lines to do 'fancy' stuff */
	count = 0;
	fgets(line, 1024, fp);
	while(!feof(fp))
	{
		if (strncmp("<>", line, 2) == 0)
		{
			fancy(count++);
		}
		else
		{
			line[strlen(line)-1] = '\0';
			fputs(line, tmp);
		}

		fgets(line, 1024, fp);
	}

	fclose(fp);

	print_tmp();
}



/* fancy
**
** Arg:	count value to determine which fancy thing to do
** Ret:	none
**
**	Procedure which calls appropriate procedures to insert non-static output into
**	the html code sent to the client's browser.
*/
void fancy(int count)
{
	switch(count)
	{
		case 0:
			show_data();
		break;
	}
}



/* show_data
**
** Arg:	none
** Ret:	none
**
**	Procedure which prints out a table containing all of the queues and printers
**	setup up in the SQL "pps" database.
*/
void show_data()
{
	int sock;
	m_result *presult, *qresult;
	m_row row;
	int numqueues, q, numprinters, p, qkeyval;
	char string[1024];


	/* connect to MSQL */
	sock = mconnect();
	if (sock == -1)
	{
		fclose(tmp);

		printf("Content-type:  text/html\n\n");
		printf("<HTML>\n<HEAD><TITLE>Error</TITLE></HEAD>\n\n");
		printf("<BODY TEXT=\"#FFFFFF\" BGCOLOR=\"#000080\">\n");
		printf("<CENTER>\n<FONT SIZE=+2>PPS</FONT><BR><BR>\n");
		printf("Error communicating with Mini SQL.<BR><BR>\nNotify administrator.\n</CENTER>\n");
		printf("</BODY>\n</HTML>\n");

		exit(0);
	}
	/* */

	/* get all of the names and key values of queues */
	qresult = send_query(sock, "SELECT name,keyval FROM queues ORDER BY name");
	numqueues = msqlNumRows(qresult);
	/* */

	/* create a table row for each queue */
	for(q=0; q<numqueues; q++)
	{
		/* pull the next queue key value */
		row = fetch_result(qresult);
		sscanf(row[1], "%d", &qkeyval);
		/* */

		/* get all of the printers' names and key values which are attached to the current queue */
		sprintf(string, "SELECT name,keyval,queue FROM printers WHERE queue=%s ORDER BY name", row[1]);
		presult = send_query(sock, string);
		numprinters = msqlNumRows(presult);
		/* */

		/* print out the table cell for the current queue, adding blank lines below for each printer */
		fprintf(tmp, "<TR>\n<TD><FONT COLOR=\"#0000FF\"><A HREF=show_queue.cgi?%s>%s</A>",row[1], row[0]);
		if (numprinters < MAX_PRINTERS_PER_QUEUE)
		{
			/* insert extra lines for the "Add Printer" which will be present */
			for(p=0; p<numprinters+2; p++)
				fprintf(tmp, "<BR>\n");
			/* */
		}
		else
		{
			/* insert blank lines only for the number of printers which do exist */
			for(p=0; p<numprinters; p++)
				fprintf(tmp, "<BR>\n");
			/* */
		}
		fprintf(tmp, "</FONT></TD>\n");
		/* */

		/* print out the table cell containing all of the printers which we have on this queue */
		fprintf(tmp, "<TD><FONT COLOR=\"#0000FF\">");
		for(p=0; p<numprinters; p++)
		{
			/* pull the next printer from the database and print it */
			row = fetch_result(presult);
			fprintf(tmp, "<A HREF=show_printer.cgi?%s>%s</A><BR>", row[1], row[0]);
			/* */
		}
		if (numprinters < MAX_PRINTERS_PER_QUEUE)
		{
			/* add the "Add Printer" line if we have not already got the maximum number added */
			fprintf(tmp, "<BR><FONT COLOR=\"#000000\"><I><A HREF=add_printer.cgi?%d>Add Printer</A></I></FONT>", qkeyval);
			/* */
		}
		fprintf(tmp, "</FONT></TD>\n</TR>\n");
		/* */
	}
	/* */

	/* add the "Add Queue" line at the bottom if we haven't already got the maximum number */
	if (numqueues < MAX_QUEUES)
	{
		fprintf(tmp, "<TR>\n<TD><I><A HREF=add_queue.cgi>Add Queue</A></I></TD>\n");
		fprintf(tmp, "<TD></TD>\n</TR>\n");
	}
	/* */

	mclose(sock);
}



/* print_tmp
**
** Arg:	none
** Ret:	none
**
**	Procedure which rewinds to the beginning of the temporary file to which all output
**	has been sent and sends it all at once to stdout.  This procedure is used so that
**	if an error occurs part way through the creation of a web page, we can display
**	an appropriate error message alone, not having to deal with the fact that half of
**	our now invalid output has already been sent to the client.
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

