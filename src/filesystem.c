/*
 *  filesystem.c
 *
 *  Functions to provide filesystem operations.
 *
 *  $Header: /usr/local/PPS_RCS/src/filesystem.c,v 1.3 2000/08/12 01:06:19 paul Exp $
 */


#include <errno.h>

#ifdef HAVE_LIBGEN_H
/* using mkdirp already available to us */
#include <libgen.h>
#else
/* using our own 'mkdirp' */
#include <fcntl.h>	/* Linux */
#include <string.h>
#include <strings.h>	/* HPUX */
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h> /* Linux */
#endif

#include <config.h>


int mkdir_p(char *path, mode_t mode);


#ifdef HAVE_LIBGEN
int mkdir_p(char *path, mode_t mode)
{
	int retval;

	/* call the mkdirp function in libgen */
	retval = mkdirp(path, mode);
	if ((retval == -1) && (errno == EEXIST))
	{
		/* if directory already exists, that's okay */
		return 0;
	}

	return retval;
}
#else
int mkdir_p(char *path, mode_t mode)
{
	/* custom mkdirp, if it is not defined in libgen */
	char *firstslash, *nextslash, *testpath;
	struct stat statbuf;


	/* we need an absolute path - could do some fancier checking to make sure this is a legal pathname if we wanted to */
	if (path[0] != '/')
	{
		return -1;
	}

	/* testpath is where we will be building the full path, bit by bit, creating directories that don't exist */
	testpath = (char *)malloc(strlen(path)+1);
	*testpath = '\0';

	/* loop through all portions of the full path, descending directories and creating them as needed */
	firstslash = path;
	while ((firstslash != NULL) && ( strlen(firstslash) > 0 ))
	{
		nextslash = index(firstslash + 1, '/');
		if ( (nextslash - firstslash) > 0 )
		{
			strncat(testpath, firstslash, (nextslash-firstslash) );
		}
		else
		{
			strcat(testpath, firstslash);
		}

		/* if this part of the directory path exists, don't need to do anything */
		if (stat(testpath, &statbuf) == -1)
		{
			/* if we got an error because the pathname doesn't exist, create it */
			/* if we get any other error, return error and errno will be set to reason */
			if (errno == ENOENT)
			{
				mkdir(testpath, mode);
			}
			else
			{
				return -1;
			}
		}

		firstslash = nextslash;
	}

	free(testpath);

	return 0;
}
#endif

