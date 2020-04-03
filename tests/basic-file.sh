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
