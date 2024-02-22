/*
 *  parse.c
 *
 *  Functions used to parse forms submitted from web clients.
 *
 *  (c)1997-1999 Paul Grebenc
 *
 *  $Header: /usr/local/PPS_RCS/src/web/parse.c,v 1.2 2000/08/11 22:56:49 paul Exp $
 */


#include <stdio.h>
#include <stdlib.h>


void parse_post(char *variable, char *value);

int parse_all(void);
int get_pair(char *pair);
int parse_pair(char *name, char *value);


char post[1024];
int post_read = 0;



void parse_post(char *variable, char *value)
{
	char *location;
	char search_value[32];
	int x,y;
	char temp;


	/* we have to get the posted data from stdin the first time this is called */
	if (post_read == 0)
	{
		post[0] = '&';
		fgets(post+1, strlen(post), stdin);
		strcat(post, "&");
		post_read = 1;
	}
	/* */

	/* this is what we want to find to find the variable in the post */
	sprintf(search_value, "&%s=", variable);
	/* */

	/* find the variable in the posted string of data */
	location = (char *)strstr(post, search_value);

	if (location == NULL)
	{
		/* couldn't find it */
		*value = '\0';
		return;
		/* */
	}
	else
	{
		/* copy the contents over, parsing special chars as necessary */
		x = strlen(search_value);
		y = 0;
		while (location[x] != '&')
		{
			/* we haven't reached the end yet - '&' */
			if (location[x] != '%')
			{
				/* it's not a hex value (starting with '%') */
				if (location[x] == '+')
				{
					/* a '+' is a space */
					value[y++] = ' ';
					x++;
					/* */
				}
				else
				{
					/* otherwise copy it directly */
					value[y++] = location[x++];
					/* */
				}
			}
			else
			{
				temp = 0;
				x++;
				/* get the first hex digit */
				if (location[x] <= 48+9)
					temp += (location[x++] - 48) * 16;
				else
					temp += (location[x++] - 55) * 16;
				/* get the second hex digit */
				if (location[x] <= 48+9)
					temp += location[x++] - 48;
				else
					temp += location[x++] - 55;
				/* store it in the value */
				value[y++] = temp;
			}
		}
		value[y] = '\0';
	}
}



int parse_all()
{
	int len, query_len;
	char *query, *location;

	/* query will point to string passed to us */
	if (strcmp(getenv("REQUEST_METHOD"), "POST") == 0)
	{
		sscanf(getenv("CONTENT_LENGTH"), "%d", &query_len);
		if ((query = (char *)malloc(query_len+1)) == NULL)
			return -1;
		if (fgets(query, query_len+1, stdin) == NULL)
			return -1;
	}
	else
	{
		if (strcmp(getenv("REQUEST_METHOD"), "GET") == 0)
		{
			query_len = strlen(getenv("QUERY_STRING"));
			if ((query = (char *)malloc(query_len+1)) == NULL)
				return -1;
			strncpy(query, getenv("QUERY_STRING"), query_len);
		}
		else
			return -1;
	}
	/* */


	location = query;

	for (;;)
	{
		len = 0;

		if (location > query + query_len)
		{
			free(query);
			return 0;
		}

		while ((location[len] != '&') && (location[len] != '\0'))
		{
			len++;
		}

		if (len == 0)
		{
			free(query);
			return 0;
		}

		location[len] = '\0';

		if (get_pair(location) == -1)
		{
			free(query);
			return -1;
		}

		location = location + len + 1;
	}
}


int get_pair(char *pair)
{
	char *name, *value;
	int len;


	name = pair;
	len = 0;

	while (pair[len] != '=')
	{
		len++;
	}

	if (len == 0)
		return 0;

	pair[len] = '\0';

	value = pair + len + 1;

	if (parse_pair(name, value) == -1)
		return -1;

	return 0;
}



int parse_pair(char *name, char *value)
{
	int num_encoded, x, y, temp;
	char *decoded_name, *decoded_value, *pair;


	/* parse name */
	num_encoded = 0;
	for (x=0; x < strlen(name); x++)
	{
		if (name[x] == '%')
			num_encoded++;
	}

	if ((decoded_name = (char *)malloc(strlen("CGI_")+strlen(name)-num_encoded+1)) == NULL)
		return -1;

	strcpy(decoded_name, "CGI_");
	x = 0;
	y = 4;

	while (x < strlen(name))
	{
		if (name[x] != '%')
		{
			if (name[x] == '+')
			{
				/* a '+' is a space */
				decoded_name[y++] = ' ';
				x++;
				/* */
			}
			else
			{
				/* else copy it directly */
				decoded_name[y++] = name[x++];
				/* */
			}
		}
		else
		{
			temp = 0;
			x++;
			/* get the first hex digit */
			if (name[x] <= 48+9)
				temp += (name[x++] - 48) * 16;
			else
				temp += (name[x++] - 55) * 16;
			/* get the second hex digit */
			if (name[x] <= 48+9)
				temp += name[x++] - 48;
				else
				temp += name[x++] - 55;
			/* store it in the value */
			decoded_name[y++] = temp;
		}
	}
	decoded_name[y] = '\0';
	/* */

	/* parse value */
	num_encoded = 0;
	for (x=0; x < strlen(value); x++)
	{
		if (value[x] == '%')
			num_encoded++;
	}

	if ((decoded_value = (char *)malloc(strlen(value)-num_encoded+1)) == NULL)
		return -1;

	x = 0;
	y = 0;

	while (x < strlen(value))
	{
		if (value[x] != '%')
		{
			if (value[x] == '+')
			{
				/* a '+' is a space */
				decoded_value[y++] = ' ';
				x++;
				/* */
			}
			else
			{
				/* else copy it directly */
				decoded_value[y++] = value[x++];
				/* */
			}
		}
		else
		{
			temp = 0;
			x++;
			/* get the first hex digit */
			if (value[x] <= 48+9)
				temp += (value[x++] - 48) * 16;
			else
				temp += (value[x++] - 55) * 16;
			/* get the second hex digit */
			if (value[x] <= 48+9)
				temp += value[x++] - 48;
			else
				temp += value[x++] - 55;
			/* store it in the value */
			decoded_value[y++] = temp;
		}
	}
	decoded_value[y] = '\0';
	/* */

	/* store values in environment variables */
	pair = (char *)malloc(strlen(decoded_name)+1+strlen(decoded_value)+1);
	if (pair == NULL)
		return -1;
	sprintf(pair, "%s=%s", decoded_name, decoded_value);
	if (putenv(pair) != 0)
		return -1;
	/* */

	return 0;
}

