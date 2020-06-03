#!/bin/bash -x

if [ -z $srcdir ]; then
    echo srcdir variable not set.
    exit 1
fi
# File backend uses directio, which does not work on tmpfs. Put targets in
# local dir instead.
export TMPDIR="."
source $srcdir/tests/test-util.sh

# start 1 server with 2 providers, 4 second wait, 20s timeout
test_start_servers_multi_providers 1 2 4 20 file:

# actual test case
#####################

# Writing foo1.dat to provider 1
echo "Hello world first provider." > $TMPBASE/foo1.dat
CPOUT=`run_to 10 src/bake-copy-to $TMPBASE/foo1.dat $svr1 1 1`
if [ $? -ne 0 ]; then
    run_to 10 src/bake-shutdown $svr1
    wait
    exit 1
fi
RID1=`echo "$CPOUT" | grep -o -P '/tmp.*$'`

# Writing foo2.dat to provider 2
echo "Hello world second provider." > $TMPBASE/foo2.dat
CPOUT=`run_to 10 src/bake-copy-to $TMPBASE/foo2.dat $svr1 2 1`
if [ $? -ne 0 ]; then
    run_to 10 src/bake-shutdown $svr1
    wait
    exit 1
fi
RID2=`echo "$CPOUT" | grep -o -P '/tmp.*$'`

# Reading from provider 1
run_to 10 src/bake-copy-from $svr1 1 $RID1 $TMPBASE/foo1-out.dat 28
if [ $? -ne 0 ]; then
    run_to 10 src/bake-shutdown $svr1
    wait
    exit 1
fi

cat $TMPBASE/foo1-out.dat
sleep 1

# Reading from provider 2 
run_to 10 src/bake-copy-from $svr1 2 $RID2 $TMPBASE/foo2-out.dat 29
if [ $? -ne 0 ]; then
    run_to 10 src/bake-shutdown $svr1
    wait
    exit 1
fi

cat $TMPBASE/foo2-out.dat
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
