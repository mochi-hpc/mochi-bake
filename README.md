# bake-bulk

## Dependencies

* uuid (install uuid-dev package on ubuntu)
* NVML/libpmem (see instructions below)
* margo (see instructions at https://xgitlab.cels.anl.gov/sds/margo)

You can compile and install the latest git revision of NVML as follows:

* `git clone https://github.com/pmem/nvml.git`
* `cd nvml`
* `make`
* `make install prefix=/home/carns/working/install/`

## Compilation

* `./prepare.sh`
* `mkdir build`
* `cd build`
* `../configure --prefix=/home/carns/working/install`
* `make`

If any dependencies are installed in a nonstandard location, then
modify the configure step listed above to include the following argument:

* `PKG_CONFIG_PATH=/home/carns/working/install/lib/pkgconfig`


## Server daemon execution example (using tmpfs memory as backing store)

* `truncate -s 500M /dev/shm/foo.dat`
* `pmempool create obj /dev/shm/foo.dat`
* `bake-bulk-server tcp://3344 /dev/shm/foo.dat`

### Explanation

The truncate command creates an empty 500 MiB file in /dev/shm,
which will act as a ramdisk for storage in this case.  You can skip this step
if you are using a true NVRAM device file.

The pmempool command formats the storage device as a pmem target for
libpmemobj.

The bake-bulk-server command starts the server daemon.  

The first argument to bake-bulk-server is the address for Mercury to
listen on.  Alternatives, depending on which transport you are using,
include the following.  See the README.md in the margo repo for more
information:

* tcp://3344
* sm://1/1
* verbs://3344

The second argument to bake-bulk-server is the path to the libpmem-formatted
storage device.

## Benchmark execution example

* `./bb-latency-bench tcp://localhost:3344 100000 4 8`

This example runs a sequence of latency benchmarks.  Other bb- utilities
installed with bake-bulk will perform other rudimentary operations.

The first argument is the address of the server.  We are using TCP in this
example, but alternatives (depending on your transport) include the
following.  See the README.md in the margo repo for more information:

* tcp://HOSTNAME:3344
* sm:///tmp/cci/sm/carns-x1/1/1
* verbs://IPADDR:3344

The second argument is the number of benchmark iterations.

The third and fourth arguments specify the range of sizes to use for read and
write operations in the benchmark.

