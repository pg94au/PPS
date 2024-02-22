/*
 *  database.c
 *
 *  Functions providing an interface between the MSQL database libraries and the rest of the system.
 *
 *  (c)1997,1998 Paul Grebenc
 *
 *  $Header: /usr/local/PPS_RCS/src/msql/database.c,v 1.1 1999/12/10 03:24:40 paul Exp $
 */


#include <config.h>
#include <time.h>
#include <msql.h>


int mconnect();
m_result *send_query(int sock, char *query);
m_row fetch_result(m_result *result);
void end_query(m_result *result);
void mclose(int sock);
int escape_string(char *new, char *old);


int msql_err;
char msql_errtxt[1024];


int mconnect()
{
	int sock;	/* the handle returned from connecting to server */
	int err;


	/* open a connection to the mSQL server */
	sock = msqlConnect(NULL);
	if (sock == -1)
	{
		return -1;
	}
	/* */

	/* select the "pps" database */
	err = msqlSelectDB(sock, "pps");
	if (err == -1)
	{
		return -1;
	}
	/* */

	return sock;
}



m_result *send_query(int sock, char *query)
{
	int err;


	/* send the query to the database server */
	err = msqlQuery(sock, query);
	if (err == -1)
	{
		msql_err = 1;
		return NULL;
	}
	msql_err = 0;
	/* */

	/* store the results and return the result code */
	return msqlStoreResult();
	/* */
}



m_row fetch_result(m_result *result)
{
	return msqlFetchRow(result);
}



void end_query(m_result *result)
{
	msqlFreeResult(result);
}



void mclose(int sock)
{
	msqlClose(sock);
}



int escape_string(char *new, char *old)
{
	int i,j;


	i = 0;
	j = 0;

	while (i < strlen(old))
	{
		if (old[i] == '\'')
		{
			new[j++] = '\\';
			new[j] = '\'';
		}
		else
		{
			new[j] = old[i];
		}

		i++;
		j++;
	}
	new[j] = '\0';

	return strlen(new);
}

