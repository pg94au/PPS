/*
 *  user_interface.c
 *
 *  Interface to allow users to check either their print balance or history.
 *
 *  (c)1998,1999 Paul Grebenc
 *
 *  $Header: /usr/local/PPS_RCS/src/web_user/user_interface.c,v 1.1 1999/12/10 03:23:42 paul Exp $
 */


#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <shadow.h>
#include <unistd.h>
#include <netdb.h>
#include <errno.h>
#if HAVE_CRYPT_H
#include <crypt.h>
#endif

#include "pps.h"
#include "database.h"
#include "../web/parse.h"
#include "show_balance.h"
#include "show_history.h"

/*  Either the local or some remote server can be configured for password authentication.    */
/*  If, for example, you have your printing running on a server other than the one through   */
/*  which Windows clients are authenticated by samba, your users may not even know what      */
/*  their password is on the server that PPS is running on.  In such cases, you can have     */
/*  this balance display utility perform authentication through a remote server of your      */
/*  choosing.  Simply comment out PASSWORD_LOCAL, uncomment PASSWORD_REMOTE, and set the     */
/*  name of the server you wish to use.  Be aware that remote password checking uses         */
/*  rexec(), and so you will have to enable rexecd on the server which will perform password */
/*  authentication.                                                                          */

#define PASSWORD_LOCAL
/*#define PASSWORD_REMOTE		"some.remote.server"*/

/* */

#define OKAY		1

#define AUTH_NOUSER	4
#define AUTH_ERROR	5
#define AUTH_FAILED	6


int main();
int authenticate_user(char *usercode, char *password);


main()
{
	char usercode[9], password[9];
int crap;


	/* parse our posted variables */
	if ((parse_all() == -1)
	|| (getenv("CGI_password") == NULL)
	|| (getenv("CGI_usercode") == NULL)
	|| (getenv("CGI_ACTION") == NULL))
	{
		printf("Content-type:  text/html\n\n");
		printf("<HTML>\n<HEAD><TITLE>PPS - USER BALANCE/HISTORY ERROR</TITLE></HEAD>\n\n");
		printf("<BODY TEXT=#FFFFFF BGCOLOR=#000080>\n");
		printf("<CENTER>\n<FONT SIZE=+2>PRINT ACCOUNTING SYSTEM</FONT><BR><BR>\n");
		printf("<FONT COLOR=#C0C0C0>USER BALANCE/HISTORY CHECK</FONT><BR><BR>\n");
		printf("<HR><BR>\n");
		printf("Error parsing submitted form.<BR><BR>\n");
		printf("Notify administrator.\n<BR>\n");
		printf("<BR><HR></CENTER>\n");

		printf("</BODY>\n</HTML>\n");

		exit(0);
	}
	/* */

	/* make sure the user entered a usercode */
	if (strlen(getenv("CGI_usercode")) > 0)
	{
		strncpy(usercode, getenv("CGI_usercode"), sizeof(usercode));
		usercode[sizeof(usercode)-1] = '\0';
	}
	else
	{
		printf("Content-type:  text/html\n\n");
		printf("<HTML>\n<HEAD><TITLE>PPS - USER BALANCE/HISTORY ERROR</TITLE></HEAD>\n\n");
		printf("<BODY TEXT=#FFFFFF BGCOLOR=#000080>\n");
		printf("<CENTER>\n<FONT SIZE=+2>PRINT ACCOUNTING SYSTEM</FONT><BR><BR>\n");
		printf("<FONT COLOR=#C0C0C0>USER BALANCE/HISTORY CHECK </FONT><BR><BR>\n");
		printf("<HR><BR>\n");
		printf("A usercode must be entered in the provided field.<BR>\n");
		printf("<BR><HR></CENTER>\n");

		printf("<FORM METHOD=POST ACTION=default.cgi>\n");
		printf("<CENTER><INPUT TYPE=SUBMIT NAME=ACTION VALUE=OK></CENTER>\n");
		printf("</FORM>\n");

		printf("</BODY>\n</HTML>\n");

		exit(0);
	}
	/* */

	/* make sure the user entered a password */
	if (strlen(getenv("CGI_password")) > 0)
	{
		strncpy(password, getenv("CGI_password"), sizeof(password));
		password[sizeof(password)-1] = '\0';
	}
	else
	{
		printf("Content-type:  text/html\n\n");
		printf("<HTML>\n<HEAD><TITLE>PPS - USER BALANCE/HISTORY ERROR</TITLE></HEAD>\n\n");
		printf("<BODY TEXT=#FFFFFF BGCOLOR=#000080>\n");
		printf("<CENTER>\n<FONT SIZE=+2>PRINT ACCOUNTING SYSTEM</FONT><BR><BR>\n");
		printf("<FONT COLOR=#C0C0C0>USER BALANCE/HISTORY CHECK</FONT><BR><BR>\n");
		printf("<HR><BR>\n");
		printf("A password must be entered in the provided field.<BR>\n");
		printf("<BR><HR></CENTER>\n");

		printf("<FORM METHOD=POST ACTION=default.cgi>\n");
		printf("<CENTER><INPUT TYPE=SUBMIT NAME=ACTION VALUE=OK></CENTER>\n");
		printf("</FORM>\n");

		printf("</BODY>\n</HTML>\n");

		exit(0);
	}
	/* */

	/* try to authenticate user's password */
	crap = authenticate_user(usercode, password);
	switch(crap)
	{
		case AUTH_NOUSER:
			printf("Content-type:  text/html\n\n");
			printf("<HTML>\n<HEAD><TITLE>PPS - USER BALANCE/HISTORY ERROR</TITLE></HEAD>\n\n");
			printf("<BODY TEXT=#FFFFFF BGCOLOR=#000080>\n");
			printf("<CENTER>\n<FONT SIZE=+2>PRINT ACCOUNTING SYSTEM</FONT><BR><BR>\n");
			printf("<FONT COLOR=#C0C0C0>USER BALANCE/HISTORY CHECK</FONT><BR><BR>\n");
			printf("<HR><BR>\n");
			printf("User '%s' does not have a printing account setup.<BR>\n", usercode);
			printf("<BR><HR></CENTER>\n");

			printf("<FORM METHOD=POST ACTION=default.cgi>\n");
			printf("<CENTER><INPUT TYPE=SUBMIT NAME=ACTION VALUE=OK></CENTER>\n");
			printf("</FORM>\n");

			printf("</BODY>\n</HTML>\n");

			exit(0);

		case AUTH_ERROR:
			printf("Content-type:  text/html\n\n");
			printf("<HTML>\n<HEAD><TITLE>PPS - USER BALANCE/HISTORY ERROR</TITLE></HEAD>\n\n");
			printf("<BODY TEXT=#FFFFFF BGCOLOR=#000080>\n");
			printf("<CENTER>\n<FONT SIZE=+2>PRINT ACCOUNTING SYSTEM</FONT><BR><BR>\n");
			printf("<FONT COLOR=#C0C0C0>USER BALANCE/HISTORY CHECK</FONT><BR><BR>\n");
			printf("<HR><BR>\n");
			printf("Error authenticating password.<BR><BR>\n");
			printf("Please notify administrator.<BR>\n");
			printf("<BR><HR></CENTER>\n");

			printf("</BODY>\n</HTML>\n");

			exit(0);

		case AUTH_FAILED:
			printf("Content-type:  text/html\n\n");
			printf("<HTML>\n<HEAD><TITLE>PPS - USER BALANCE/HISTORY ERROR</TITLE></HEAD>\n\n");
			printf("<BODY TEXT=#FFFFFF BGCOLOR=#000080>\n");
			printf("<CENTER>\n<FONT SIZE=+2>PRINT ACCOUNTING SYSTEM</FONT><BR><BR>\n");
			printf("<FONT COLOR=#C0C0C0>USER BALANCE/HISTORY CHECK</FONT><BR><BR>\n");
			printf("<HR><BR>\n");
			printf("The usercode or password provided are incorrect.<BR>\n", crap);
			printf("<BR><HR></CENTER>\n");

			printf("<FORM METHOD=POST ACTION=default.cgi>\n");
			printf("<CENTER><INPUT TYPE=SUBMIT NAME=ACTION VALUE=OK></CENTER>\n");
			printf("</FORM>\n");

			printf("</BODY>\n</HTML>\n");

			exit(0);
	}
	/* */

	/* check what was requested by the user */
	if (strcmp(getenv("CGI_ACTION"), "CHECK ACCOUNT BALANCE") == 0)
	{
		do_print_balance(usercode);
		exit(0);
	}
	if (strcmp(getenv("CGI_ACTION"), "VIEW ACCOUNT HISTORY") == 0)
	{
		do_account_history(usercode);
		exit(0);
	}
	/* */

	/* if we are here, then no valid request was made */
	printf("Content-type:  text/html\n\n");
	printf("<HTML>\n<HEAD><TITLE>PPS - USER BALANCE/HISTORY ERROR</TITLE></HEAD>\n\n");
	printf("<BODY TEXT=#FFFFFF BGCOLOR=#000080>\n");
	printf("<CENTER>\n<FONT SIZE=+2>PRINT ACCOUNTING SYSTEM</FONT><BR><BR>\n");
	printf("<FONT COLOR=#C0C0C0>USER BALANCE/HISTORY CHECK</FONT><BR><BR>\n");
	printf("<HR><BR>\n");
	printf("Error parsing submitted form.<BR><BR>\n");
	printf("Notify administrator.\n<BR>\n");
	printf("<BR><HR></CENTER>\n");

	printf("</BODY>\n</HTML>\n");
	/* */

	exit(0);
}



int authenticate_user(char *usercode, char *password)
{
	#ifdef PASSWORD_LOCAL

	struct spwd *spass;
	char salt[3];


	/* get the shadow password entry for this user */
	if ((spass = getspnam(usercode)) == NULL)
	{
		if (errno == 0)
			return AUTH_NOUSER;	/* username does not exist */
		else
			return AUTH_ERROR;	/* problem looking up usercode/password */
	}
	else
	{
		/* check password */
		salt[0] = spass->sp_pwdp[0];
		salt[1] = spass->sp_pwdp[1];
		salt[2] = '\0';

		if (strcmp(spass->sp_pwdp, crypt(password, salt)) == 0)
			return OKAY;
		else
			return AUTH_FAILED;
		/* */
	}
	/* */

	#endif

	#ifdef PASSWORD_REMOTE

	char host[256], *hostptr;
	struct servent *sp;
	int result;


	sprintf(host, PASSWORD_REMOTE);
	hostptr = host;
	sp = getservbyname("exec", "tcp");
	if (sp == NULL)
	{
		return AUTH_ERROR;
	}

	result = rexec(&hostptr, sp->s_port, usercode, password, "/bin/true", 0);
	if (result == -1)
	{
		return AUTH_FAILED;
	}

	close(result);

	return OKAY;

	#endif
}

