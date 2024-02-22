/*
 *  show_history.c
 *
 *  Display a log of print jobs via HTML for a user.
 *
 *  (c)1997-1999 Paul Grebenc, Andrew Rose
 *
 *  $Header: /usr/local/PPS_RCS/src/web_user/show_history.c,v 1.1 1999/12/10 03:23:22 paul Exp $
 */


#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


void do_account_history(char *usercode)
{
	FILE *log_fp, *tmp;
	int pagetotal = 0;
	int jobtotal = 0;
	float costtotal = 0.0;
	char path[256], line[1024], c;
	char *line_date, *line_usercode, *line_queue, *line_printer, *line_pages, *line_charge,
		*line_balance, *line_result, *line_filename;


	/* open a temporary file to contain all the data sent to the web client */
	tmp = tmpfile();
	if (tmp == NULL)
	{
		printf("Content-type: text/html\n\n");
		printf("<HTML>\n<HEAD><TITLE>PPS - Error</TITLE></HEAD>\n\n");
		printf("<BODY TEXT=#FFFFFF BGCOLOR=#000080>");
		printf("<CENTER>\n<FONT SIZE=+2>PPS</FONT><BR><BR>\n");
		printf("Error creating a temporary file.<BR><BR>\nNotify administrator.\n</CENTER>\n");
		printf("</BODY>\n</HTML>\n");

		exit(0);
	}
	/* */

	/* build pathname from which we extract log entries */
	sprintf(path, "%s/logs/print_log", ROOT);
	/* */

	/* open the log file for reading */
	log_fp = fopen(path, "r");
	if (log_fp == NULL)
	{
		fclose(tmp);

		printf("Content-type: text/html\n\n");
		printf("<HTML>\n<HEAD><TITLE>PPS - USER PRINT HISTORY ERROR</TITLE></HEAD>\n\n");
		printf("<BODY TEXT=#FFFFFF BGCOLOR=#000080>\n");
		printf("<CENTER>\n<FONT SIZE=+2>PRINT ACCOUNTING SYSTEM</FONT><BR><BR>\n");
		printf("<FONT COLOR=#C0C0C0>USER PRINT HISTORY</FONT><BR><BR>\n");
		printf("<HR>\n");
		printf("Unable to open print log file for reading.<BR><BR>\n");
		printf("Please notify administrator.\n");
		printf("<BR><HR></CENTER>\n");

		printf("</BODY>\n</HTML>\n");

		exit(0);
	}
	/* */

	fprintf(tmp, "Content-type: text/html\n\n");
	fprintf(tmp, "<HTML>\n");
	fprintf(tmp, "<HEAD><TITLE>PPS - SHOW USER PRINT HISTORY</TITLE></HEAD>\n");
	fprintf(tmp, "<BODY TEXT=#C0C0C0 BGCOLOR=#000080>\n");

	fprintf(tmp, "<CENTER><P><B><FONT COLOR=#FFFFFF><FONT SIZE=+2>PRINT ACCOUNTING SYSTEM</FONT></FONT></B></P>\n");

	fprintf(tmp, "<CENTER><P><FONT COLOR=#C0C0C0>USER PRINT HISTORY</FONT></P></CENTER>\n");
	fprintf(tmp, "<BR><HR><BR>\n");

	fprintf(tmp, "<CENTER>\n");
	fprintf(tmp, "<TABLE CELLSPACING=0 CELLPADDING=0 WIDTH=100% BGCOLOR=#000080>\n");
	fprintf(tmp, "<TR BGCOLOR=#FFFFFF><TD><TT><B><FONT COLOR=#000000>Date</FONT></B></TT></FONT></TD>\n");
	fprintf(tmp, "<TD><TT><B><FONT COLOR=#000000>Queue</FONT></B></TT></TD>\n");
	fprintf(tmp, "<TD><TT><B><FONT COLOR=#000000>Pages</FONT></B></TT></TD>\n");
	fprintf(tmp, "<TD><TT><B><FONT COLOR=#000000>Cost</FONT></B></TT></TD>\n");
	fprintf(tmp, "<TD><TT><B><FONT COLOR=#000000>Balance</FONT></B></TT></TD>\n");
	fprintf(tmp, "<TD><TT><B><FONT COLOR=#000000>File Printed</FONT></B></TT></TD>\n");

	/* go through the print log file, deciphering entries for ones pertaining to this user */
	for (;;)
	{
		/* get a line from the print logs to be deciphered */
		if (fgets(line, 1024, log_fp) == NULL)
		{
			if (feof(log_fp))
			{
				fclose(log_fp);
				break;
			}
			else
			{
				fclose(log_fp);
				fclose(tmp);

				printf("Content-type: text/html\n\n");
				printf("<HTML>\n<HEAD><TITLE>PPS - USER PRINT HISTORY ERROR</TITLE></HEAD>\n\n");
				printf("<BODY TEXT=#FFFFFF BGCOLOR=#000080>\n");
				printf("<CENTER>\n<FONT SIZE=+2>PRINT ACCOUNTING SYSTEM</FONT><BR><BR>\n");
				printf("<FONT COLOR=#C0C0C0>USER PRINT HISTORY</FONT><BR><BR>\n");
				printf("<HR>\n");
				printf("Error reading print log file.<BR><BR>\n");
				printf("Please notify administrator.\n");
				printf("<BR><HR></CENTER>\n");

				printf("</BODY>\n</HTML>\n");

				exit(0);
			}
		}
		/* */

		/* split out fields of this entry */
		line_date = strtok(line, "|");
		line_usercode = strtok(NULL, "|");
		line_queue = strtok(NULL, "|");
		line_printer = strtok(NULL, "|");
		line_pages = strtok(NULL, "|");
		line_charge = strtok(NULL, "|");
		line_balance = strtok(NULL, "|");
		line_result = strtok(NULL, "|");
		line_filename = strtok(NULL, "|");
		/* */

		/* check usercode versus the usercode we are looking for */
		if (strncmp(usercode, line_usercode, strlen(usercode)) == 0)
		{
			/* print table entry */
			fprintf(tmp, "<TR BGCOLOR=#87CEFA>\n");
			fprintf(tmp, "<TD><TT><FONT COLOR=#000000>%s</FONT></TT></TD>", line_date);
			fprintf(tmp, "<TD><TT><FONT COLOR=#000000>%s</FONT></TT></TD>", line_queue);
			fprintf(tmp, "<TD ALIGN=RIGHT><TT><FONT COLOR=#000000>%s</FONT></TT></TD>", line_pages);
			fprintf(tmp, "<TD ALIGN=RIGHT><TT><FONT COLOR=#000000>%s</FONT></TT></TD>", line_charge);
			fprintf(tmp, "<TD ALIGN=RIGHT><TT><FONT COLOR=#000000>%s</FONT></TT></TD>", line_balance);
			fprintf(tmp, "<TD><TT><FONT COLOR=#000000>%s</FONT></TT></TD>", line_filename);
			fprintf(tmp, "</TR>\n");
			/* */

			/* add to totals */
			pagetotal = pagetotal + atoi(line_pages);
			costtotal = costtotal + (float)atof(line_charge);
			jobtotal++;
			/* */
		}
		/* */
	}
	/* */

	/* print out tail end of table and totals below */
	fprintf(tmp, "</TABLE>\n");

	fprintf(tmp, "<BR><BR>\n");
	fprintf(tmp, "<FONT COLOR=#FFFFFF><TT><B>Total Jobs:</B></TT></FONT> \n");
	fprintf(tmp, "<FONT COLOR=#C0C0C0><TT><B>%d</B></TT></FONT><BR>\n", jobtotal);
	fprintf(tmp, "<FONT COLOR=#FFFFFF><TT><B>Total Pages:</B></TT></FONT>\n");
	fprintf(tmp, "<FONT COLOR=#C0C0C0><TT><B>%d</B></TT></FONT><BR>\n", pagetotal);
	fprintf(tmp, "<FONT COLOR=#FFFFFF><TT><B>Total Cost:</B></TT></FONT>\n");
	fprintf(tmp, "<FONT COLOR=#C0C0C0><TT><B>%0.2f</B></TT></FONT><BR>\n", costtotal);

	fprintf(tmp, "</CENTER>\n");

	fprintf(tmp, "<BR><HR><BR>\n");
	fprintf(tmp, "<CENTER><FONT COLOR=#C0C0C0><FONT SIZE=-2>(C)1997-1999 Paul Grebenc, Andrew Rose</FONT></FONT></CENTER>\n");

	fprintf(tmp, "</BODY>\n");
	fprintf(tmp, "</HTML>\n");
	/* */

	/* print the table, written thus far to a temporary file only, to the client */
	rewind(tmp);

	c = fgetc(tmp);
	while (!feof(tmp))
	{
		putchar(c);
		c = fgetc(tmp);
	}

	fclose(tmp);
	/* */
}

