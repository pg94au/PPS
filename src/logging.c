/*
 *  logging.c
 *
 *  Functions to provide for print and error logging.
 *
 *  (c)1997 Paul Grebenc
 *
 *  $Header: /usr/local/PPS_RCS/src/logging.c,v 1.1 1999/12/10 03:34:44 paul Exp $
 */


#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <pwd.h>
#include <sys/stat.h>
#include <sys/types.h>
#if HAVE_SYS_VFS_H && HAVE_STATFS_IN_VFS_H && HAVE_FSTATFS
#include <sys/vfs.h>
#elif HAVE_SYS_STATVFS_H && HAVE_STATVFS_IN_STATVFS_H && HAVE_FSTATVFS
#include <sys/statvfs.h>
#endif
#include <time.h>
#include <unistd.h>

#include "pps.h"


extern int errno;


int print_log(struct inf *inf, char *queue, char *printer, int pages, float charge, float new_balance, int result);
int admin_log(char *usercode, float *old_balance, float *new_balance);
int error_log(char *string);
int debug_log(char *string);
int check_log_size(int fd, int low, int high);
char *get_username(int userid);



int print_log(struct inf *inf, char *queue, char *printer, int pages, float charge, float new_balance, int result)
{
	int fd_result;
	char path[256];
	int fd;
	char string[1024];
	time_t t;
	struct tm *tm;
	char result_char;


	/*** attempt to lock print_log file ***/

	/* first open the file if we can */
	sprintf(path, "%s/logs/print_log", ROOT);
	fd = open(path, O_CREAT | O_APPEND | O_RDWR, S_IRWXU);
	/* */

	if (fd == -1)
	{
		perror("cannot open or create print_log file");
		return -1;
	}

	/* opened the file, so we write lock it */
	fd_result = wait_lock_file(fd);
	/* */

	/* cannot lock file, report */
	if (fd_result == -1)
	{
		perror("cannot write lock print_log file");
		return -1;
	}
	/* */

	/* got the lock, so check to see first if the logs have gotten to big */
	if (check_log_size(fd, TRIM_PRINT_LOG_TO, TRIM_PRINT_LOG_AT) == -1)
	{
		close(fd);

		return -1;
	}
	/* */

	/** write out the log entry **/

	/* first build the log entry */
	t = time(NULL);
	tm = localtime(&t);
	switch(result)
	{
		case PPS_JOB_PRINTED:
			result_char = ' ';
			break;
		case PPS_JOB_CANCELLED:
			result_char = 'C';
			break;
		case PPS_PRINTER_RESET:
			result_char = 'R';
			break;
		case PPS_JOB_TIMEOUT:
			result_char = 'T';
			break;
		case PPS_ERROR:
			result_char = 'E';
			break;
		default:
			result_char = '?';
			break;
	}
	sprintf(string, "%02d/%02d/%4d %02d:%02d:%02d|%-8s|%-32s|%-32s|%3d|%5.2f|%6.2f|%c|%s\n",tm->tm_mon+1, tm->tm_mday, 1900+tm->tm_year,
			tm->tm_hour, tm->tm_min, tm->tm_sec, get_username(inf->userid), queue, printer, pages, charge, new_balance, result_char, inf->filename);
	/* */

	/* write the string to the log file */	
	if (!write(fd, string, strlen(string)))
	{
		perror("cannot write to print_log");
		close(fd);
		return -1;
	}
	/* */

	/** **/

	/* close print_log, unlock it as we do */
	close(fd);
	/* */

	return 0;
}



int admin_log(char *usercode, float *old_balance, float *new_balance)
{
	int fd_result;
	char path[256];
	int fd;
	char string[1024];
	time_t t;
	struct tm *tm;
	char remote_user[9];


	/*** attempt to lock admin_log file ***/

	/* first open the file if we can */
	sprintf(path, "%s/logs/admin_log", ROOT);
	fd = open(path, O_CREAT | O_APPEND | O_RDWR, S_IRWXU);
	/* */

	if (fd == -1)
	{
		perror("cannot open or create admin_log file");
		return -1;
	}

	/* opened the file, so we write lock it */
	fd_result = wait_lock_file(fd);
	/* */

	/* cannot lock file, report */
	if (fd_result == -1)
	{
		perror("cannot write lock admin_log file");
		return -1;
	}
	/* */

	/* got the lock, so check to see first if the logs have gotten to big */
	if (check_log_size(fd, TRIM_ADMIN_LOG_TO, TRIM_ADMIN_LOG_AT) == -1)
	{
		close(fd);

		return -1;
	}
	/* */

	/** write out the log entry **/

	/* first build the log entry */
	t = time(NULL);
	tm = localtime(&t);
	if (getenv("REMOTE_USER") == NULL)
		strcpy(remote_user, "\0");
	else
		strcpy(remote_user, getenv("REMOTE_USER"));
	sprintf(string, "%02d/%02d/%4d %02d:%02d:%02d|%-8s|%6.2f|%6.2f|%s\n",tm->tm_mon+1, tm->tm_mday, 1900+tm->tm_year, tm->tm_hour,
		tm->tm_min, tm->tm_sec, usercode, *old_balance, *new_balance, remote_user);
	/* */

	/* write the string to the log file */	
	if (!write(fd, string, strlen(string)))
	{
		perror("cannot write to admin_log");
		close(fd);
		return -1;
	}
	/* */

	/** **/

	/* close admin_log, unlock it as we do */
	close(fd);
	/* */

	return 0;
}




int error_log(char *string)
{
	int fd_result;
	char path[256];
	int fd;
	char log[1024];
	time_t t;
	struct tm *tm;


	/*** attempt to lock error_log file ***/

	/* first open the file if we can */
	sprintf(path, "%s/logs/error_log", ROOT);
	fd = open(path, O_CREAT | O_APPEND | O_RDWR, S_IRWXU);
	/* */

	if (fd == -1)
	{
		perror("cannot open or create error_log file");
		return -1;
	}

	/* opened the file, so we write lock it */
	fd_result = wait_lock_file(fd);
	/* */

	/* cannot lock file, report */
	if (fd_result == -1)
	{
		perror("cannot write lock error_log file");
		return -1;
	}
	/* */

	/* got the lock, so check to see first if the logs have gotten to big */
	if (check_log_size(fd, TRIM_ERROR_LOG_TO, TRIM_ERROR_LOG_AT) == -1)
	{
		close(fd);

		return -1;
	}
	/* */

	/** write out the log entry **/

	/* write the string to the log file */	

	t = time(NULL);
	tm = localtime(&t);
	sprintf(log, "%02d/%02d/%4d %02d:%02d:%02d %s",tm->tm_mon+1, tm->tm_mday, 1900+tm->tm_year,
			tm->tm_hour, tm->tm_min, tm->tm_sec, string);

	if (!write(fd, log, strlen(log)))
	{
		perror("cannot write to error_log");
		fprintf(stderr, ">>> %s", log);
		close(fd);
		return -1;
	}
	if (string[strlen(log)-1] != '\n')
	{
		if (!write(fd, "\n", 1))
		{
			perror("cannot write to error_log");
			close(fd);
			return -1;
		}
	}
	/* */

	/** **/

	/* close error_log, unlock it as we do */
	close(fd);
	/* */

	return 0;
}



int debug_log(char *string)
{
	int fd_result;
	char path[256];
	int fd;
	char log[1024];
	time_t t;
	struct tm *tm;


	/*** attempt to lock debug_log file ***/

	/* first open the file if we can */
	sprintf(path, "%s/logs/debug_log", ROOT);
	fd = open(path, O_CREAT | O_APPEND | O_RDWR, S_IRWXU);
	/* */

	if (fd == -1)
	{
		perror("cannot open or create debug_log file");
		return -1;
	}

	/* opened the file, so we write lock it */
	fd_result = wait_lock_file(fd);
	/* */

	/* cannot lock file, report */
	if (fd_result == -1)
	{
		perror("cannot write lock debug_log file");
		return -1;
	}
	/* */

	/* got the lock, so check to see first if the logs have gotten to big */
	if (check_log_size(fd, TRIM_DEBUG_LOG_TO, TRIM_DEBUG_LOG_AT) == -1)
	{
		close(fd);

		return -1;
	}
	/* */

	/** write out the log entry **/

	/* write the string to the log file */	

	t = time(NULL);
	tm = localtime(&t);
	sprintf(log, "%02d/%02d/%4d %02d:%02d:%02d %s",tm->tm_mon+1, tm->tm_mday, 1900+tm->tm_year,
			tm->tm_hour, tm->tm_min, tm->tm_sec, string);

	#ifdef DEBUG_STDERR
	fprintf(stderr, "%s\n", log);
	#endif

	if (!write(fd, log, strlen(log)))
	{
		perror("cannot write to debug_log");
		fprintf(stderr, ">>> %s", log);
		close(fd);
		return -1;
	}
	if (string[strlen(log)-1] != '\n')
	{
		if (!write(fd, "\n", 1))
		{
			perror("cannot write to debug_log");
			close(fd);
			return -1;
		}
	}
	/* */

	/** **/

	/* close debug_log, unlock it as we do */
	close(fd);
	/* */

	return 0;
}



int check_log_size(int fd, int low, int high)
{
	struct stat stats;
	#if HAVE_SYS_VFS_H && HAVE_STATFS_IN_VFS_H && HAVE_FSTATFS
	struct statfs fs_stats;
	#elif HAVE_SYS_STATVFS_H && HAVE_STATVFS_IN_STATVFS_H && HAVE_FSTATVFS
	struct statvfs fs_stats;
	#endif
	FILE *tmp_fp;
	char c;
	int retval;
	time_t t;
	struct tm *tm;
	char string[256];


	/* before we check log size, make sure there isn't too little disk space left */
	#if HAVE_SYS_VFS_H && HAVE_STATFS_IN_VFS_H && HAVE_FSTATFS
	if (fstatfs(fd, &fs_stats) == -1)
	#elif HAVE_SYS_STATVFS_H && HAVE_STATVFS_IN_STATVFS_H && HAVE_FSTATVFS
	if (fstatvfs(fd, &fs_stats) == -1)
	#else
	return 0;	/* no fstatfs or fstatvfs, so no disk space check */
	#endif
	{
		#ifdef DEBUG_STDERR
		fprintf(stderr, "##### error checking filesystem stats while writing a log file\n");
		#endif
		return -1;	/* error checking filesystem stats */
	}

	if ((fs_stats.f_bavail * fs_stats.f_bsize) < (STOP_LOGGING_AT * 1024))
	{
		#ifdef DEBUG_STDERR
		fprintf(stderr, "##### we appear to be at the point where we should no longer log to this disk\n");
		#endif
		return -1;
	}
	/* */

	/* first check to make sure our watermarks are sane */
	if ((high == 0) || (high < low))
	{
		/* we can't use these values, so we won't do any trimming */
		return 0;
		/* */
	}
	/* */

	/* grab the stats for this file */
	if (fstat(fd, &stats) == -1)
	{
		#ifdef DEBUG_STDERR
		fprintf(stderr, "##### problem reading stats on a log file\n");
		#endif
		/* problem reading stats from this file... return error */
		return -1;
		/* */
	}
	/* */

	/* check if this file is too large and needs to be trimmed */
	if (stats.st_size < high * 1024)
	{
		/* we don't need to worry about trimming the log files */
		return 0;
		/* */
	}
	/* */

	/** move file pointer to a point where we have only what we want to keep ahead of us **/
	/* find the general area we want */
	if (lseek(fd, stats.st_size - (low * 1024), SEEK_SET) == -1)
	{
		/* we should have been able to seek to this point, so we should never be here */
		return -1;
		/* */
	}
	/* */

	/* now look for the beginning of the next line so we can start from there */
	for (;;)
	{
		if (read(fd, &c, 1) == -1)
			return -1;

		if (c == '\n')
			break;
	}
	/* */
	/** **/

	/* create a temporary file that we can copy the bit of the log file we want to keep to */
	if ((tmp_fp = tmpfile()) == NULL)
	{
		/* could not create a temporary file, so we can't trim the logs */
		return -1;
		/* */
	}
	/* */

	/* copy the remaining portion of the log file to the temp file */
	for (;;)
	{
		retval = read(fd, &c, 1);

		if (retval == 0)	/* EOF */
			break;

		if (retval == -1)	/* error */
		{
			fclose(tmp_fp);
			return -1;
		}

		if (fwrite(&c, 1, 1, tmp_fp) != 1)	/* error */
		{
			fclose(tmp_fp);
			return -1;
		}
	}
	/* */

	/* truncate the original log file to zero */
	if (ftruncate(fd, 0) == -1)
	{
		fclose(tmp_fp);
		return -1;
	}
	/* */

	/* copy the temporary file contents back to the original log file */
	if (lseek(fd, 0, SEEK_SET) == -1)
	{
		/* we should have been able to seek to this point, so we should never be here */
		return -1;
		/* */
	}
	if (fseek(tmp_fp, 0, SEEK_SET) == -1)
	{
		/* we should have been able to seek to this point, so we should never be here */
		return -1;
		/* */
	}

	while (!feof(tmp_fp))
	{
		if (fread(&c, 1, 1, tmp_fp) == 1)
		{
			if (write(fd, &c, 1) == -1)
			{
				fclose(tmp_fp);
				return -1;
			}
		}
		else
		{
			if (ferror(tmp_fp))
			{
				fclose(tmp_fp);
				return -1;
			}
		}
	}
	/* */

	/* remove temporary file */
	fclose(tmp_fp);
	/* */

	/* write an entry in the logs to note that we trimmed the logs at this time */
	t = time(NULL);
	tm = localtime(&t);
	sprintf(string, "%02d/%02d/%4d %02d:%02d:%02d ##### LOG FILE TRIMMED FROM %d BYTES TO %d BYTES #####",
		stats.st_size, low, tm->tm_mon+1, tm->tm_mday, 1900+tm->tm_year, tm->tm_hour, tm->tm_min, tm->tm_sec);
	#ifdef DEBUG_STDERR
	fprintf(stderr, "%s\n", string);
	#endif
	if (!write(fd, string, strlen(string)))
	{
		#ifdef DEBUG_STDERR
		perror("cannot write to log file");
		fprintf(stderr, ">>> %s", string);
		#endif
		return -1;
	}
	/* */

	return 0;
}



char *get_username(int userid)
{
	struct passwd *pwd;
	static char username[9];


	/* try to get the password information for this userid */
	pwd = getpwuid(userid);
	if (pwd == NULL)
	{
		return (char *)NULL;
	}
	else
	{
		strcpy(username, pwd->pw_name);

		return username;
	}
	/* */
}

