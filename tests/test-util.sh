#
# General test script utilities
#

if [ -z "$TIMEOUT" ] ; then
    echo expected TIMEOUT variable defined to its respective command
    exit 1
fi

if [ -z "$MKTEMP" ] ; then
    echo expected MKTEMP variable defined to its respective command
    exit 1
fi

TMPDIR=/dev/shm
export TMPDIR
mkdir -p $TMPDIR
TMPBASE=$(${MKTEMP} --tmpdir -d test-XXXXXX)

if [ ! -d $TMPBASE ];
then
  echo "Temp directory doesn't exist: $TMPBASE"
  exit 3
fi

function run_to ()
{
    maxtime=${1}s
    shift
    ${TIMEOUT} --signal=9 $maxtime "$@"
}

function test_start_servers ()
{
    nservers=${1:-4}
    startwait=${2:-15}
    maxtime=${3:-120}
    backend=${4:-"pmem:"}

    # start daemons
    for i in `seq $nservers`
    do
        src/bake-mkpool -s 100M $TMPBASE/svr-$i.dat
        if [ $? -ne 0 ]; then
            exit 1
        fi

        run_to ${maxtime} src/bake-server-daemon -p -f $TMPBASE/svr-$i.addr na+sm ${backend}$TMPBASE/svr-$i.dat &
        if [ $? -ne 0 ]; then
            # TODO: this doesn't actually work; can't check return code of
            # something executing in background.  We have to rely on the
            # return codes of the actual client side tests to tell if
            # everything started properly
            exit 1
        fi
    done

    # wait for servers to start
    sleep ${startwait}

    svr1=`cat $TMPBASE/svr-1.addr`
}

function test_start_servers_multi_providers ()
{
    nservers=${1:-4}
    nproviders=${2:-1}
    startwait=${3:-15}
    maxtime=${4:-120}

    # start daemons
    for i in `seq $nservers`
    do
	for j in `seq $nproviders`
	do
    	    src/bake-mkpool -s 100M $TMPBASE/svr-$i-prvd-$j.dat
            if [ $? -ne 0 ]; then
                exit 1
            fi
        done

        run_to ${maxtime} src/bake-server-daemon -m providers -f $TMPBASE/svr-$i.addr na+sm $TMPBASE/svr-$i-prvd-*.dat &
        if [ $? -ne 0 ]; then
            # TODO: this doesn't actually work; can't check return code of
            # something executing in background.  We have to rely on the
            # return codes of the actual client side tests to tell if
            # everything started properly
            exit 1
        fi
    done

    # wait for servers to start
    sleep ${startwait}

    svr1=`cat $TMPBASE/svr-1.addr`
}

function test_start_servers_multi_targets ()
{
    nservers=${1:-4}
    ntargets=${2:-1}
    startwait=${3:-15}
    maxtime=${4:-120}

    # start daemons
    for i in `seq $nservers`
    do
	for j in `seq $ntargets`
	do
    	    src/bake-mkpool -s 100M $TMPBASE/svr-$i-tgt-$j.dat
            if [ $? -ne 0 ]; then
                exit 1
            fi
        done

        run_to ${maxtime} src/bake-server-daemon -m targets -f $TMPBASE/svr-$i.addr na+sm $TMPBASE/svr-$i-tgt-*.dat &
        if [ $? -ne 0 ]; then
            # TODO: this doesn't actually work; can't check return code of
            # something executing in background.  We have to rely on the
            # return codes of the actual client side tests to tell if
            # everything started properly
            exit 1
        fi
    done

    # wait for servers to start
    sleep ${startwait}

    svr1=`cat $TMPBASE/svr-1.addr`
}

