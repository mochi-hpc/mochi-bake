#!/bin/bash -x

if [ -z $srcdir ]; then
    echo srcdir variable not set.
    exit 1
fi
# File backend uses directio, which does not work on tmpfs. Put targets in
# local dir instead.
export TMPDIR="."
source $srcdir/tests/test-util.sh

# start 1 server with 2 second wait, 20s timeout
test_start_servers 1 2 20 file:

# actual test case
#####################

echo "Hello world." > $TMPBASE/foo.dat
CPOUT=`run_to 10 src/bake-copy-to $TMPBASE/foo.dat $svr1 1 1`
if [ $? -ne 0 ]; then
    run_to 10 src/bake-shutdown $svr1
    wait
    exit 1
fi

RID=`echo "$CPOUT" | grep -o -P '/tmp.*$'`
run_to 10 src/bake-copy-from $svr1 1 $RID $TMPBASE/foo-out.dat 13
if [ $? -ne 0 ]; then
    run_to 10 src/bake-shutdown $svr1
    wait
    exit 1
fi

cat $TMPBASE/foo-out.dat
sleep 1

#####################

# tear down
run_to 10 src/bake-shutdown $svr1
if [ $? -ne 0 ]; then
    wait
    exit 1
fi

wait

echo cleaning up $TMPBASE
rm -rf $TMPBASE

exit 0
