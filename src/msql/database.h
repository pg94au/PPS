/*
 *  database.h
 *
 *  Functions providing an interface between the MSQL database libraries and the rest of the system.
 *
 *  (c)1997 Paul Grebenc
 *
 *  $Header: /usr/local/PPS_RCS/src/msql/database.h,v 1.1 1999/12/10 03:24:50 paul Exp $
 */


#include <time.h>
#include <msql.h>


int mconnect();
m_result *send_query(int sock, char *query);
m_row fetch_result(m_result *result);
void end_query(m_result *result);
void mclose(int sock);


extern int msql_err;
extern char msql_errtxt[1024];

