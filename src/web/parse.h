/*  parse.h
 *
 *  Functions used to parse variables posted from a web browser.
 *
 *  (c)1997,1998 Paul Grebenc
 *
 *  $Header: /usr/local/PPS_RCS/src/web/parse.h,v 1.1 1999/12/10 03:28:18 paul Exp $
 */


#include <stdio.h>


void parse_post(char *variable, char *value);

int parse_all(void);

