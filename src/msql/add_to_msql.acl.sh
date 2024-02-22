#!/bin/sh

# $Header: /usr/local/PPS_RCS/src/msql/add_to_msql.acl.sh,v 1.1 1999/12/10 03:25:26 paul Exp $

# add_to_msql.acl.sh
#
# Create the pps entry in MSQL's access control list file.
#
# (c)1998 Paul Grebenc

if grep pps $1/msql.acl > /dev/null
then
	echo "msql.acl file already updated"
else
	echo "" >> $1/msql.acl
	echo "database=pps" >> $1/msql.acl
	echo "read=pps,root" >> $1/msql.acl
	echo "write=pps,root" >> $1/msql.acl
	echo "access=local" >> $1/msql.acl
	echo "msql.acl file updated"
fi

