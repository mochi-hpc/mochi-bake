# Bake

Bake is a microservice (a Mochi provider) for high performance bulk
storage of raw data regions.  Bake uses modular backends to store data
on persistent memory, conventional file systems, or other storage media.

See https://www.mcs.anl.gov/research/projects/mochi/ and
https://mochi.readthedocs.io/en/latest/ for more information about Mochi.

Bake's scope is limited exclusively to data storage.  Capabilities such as
indexing, name spaces, and sharding must be provided by other microservice
components.

## Installation

The easiest way to install Bake is through spack:

`spack install bake`

This will install BAKE and its dependencies.  Please refer to the end of the
document for manual compilation instructions.

## Architecture

Like most Mochi services, BAKE relies on a client/provider architecture.
A provider, identified by its _address_ and _multiplex id_, manages one or more
_BAKE targets_, referenced externally by their _target id_.

A target can be thought of as a storage device.  This may be (for example) a
PMDK volume or a local file system.

## Setting up a BAKE target

BAKE requires the backend storage file to be created beforehand using
`bake-mkpool`. For instance:

`bake-mkpool -s 500M /dev/shm/foo.dat`

creates a 500 MB file at _/dev/shm/foo.dat_ to be used by BAKE as a target.
Bake will use the `pmem` (persistent memory) backend by default, which means
that the underlying file will memory mapped for access usign the PMDK
library.  You can also providie an explicit prefix (such as `file:` for the
conventional file backend or `pmem:` for the persistent memory backend) to
dictate a specific target type.

## Starting a daemon

BAKE ships with a default daemon program that can setup providers and attach
to storage targets. This daemon can be started as follows:

`bake-server-daemon [options] <listen_address> <bake_pool_1> <bake_pool_2> ...`

The program takes a set of options followed by an address at which to listen for
incoming RPCs, and a list of
BAKE targets already created using `bake-mkpool`.

For example:

`bake-server-daemon -f bake.addr -m providers bmi+tcp://localhost:1234 /dev/shm/foo.dat /dev/shm/bar.dat`

The following options are accepted:
* `-f` provides the name of the file in which to write the address of the daemon.
* `-m` provides the mode (_providers_ or _targets_).

The _providers_ mode indicates that, if multiple BAKE targets are used (as above),
these targets should be managed by multiple providers, accessible through 
different multiplex ids 1, 2, ... _N_ where _N_ is the number of storage targets
to manage. The _targets_ mode indicates that a single provider should be used to
manage all the storage targets.

## Integrating Bake into a larger service

Bake is not intended to be a standalone user-facing service.  See
https://mochi.readthedocs.io/en/latest/bedrock.html for guidance on how to
integrate it with other providers using Mochi's Bedrock capability.

## Client API example

Data is stored in `regions` within a `target` using explicit create,
write, and persist operations.  The caller cannot dictate the region id
that will be used to reference a region; this identifier is generated
by Bake at creation time.  The region size must be specified at creation
time as well; there is no mechanism for extending the size of an existing
region.

```c
#include <bake-client.h>

int main(int argc, char **argv)
{
    char *svr_addr_str; // string address of the BAKE server
    hg_addr_t svr_addr; // Mercury address of the BAKE server
    margo_instance_id mid; // Margo instance id
    bake_client_t bcl; // BAKE client
    bake_provider_handle_t bph; // BAKE handle to provider
    uint8_t mplex_id; // multiplex id of the provider
    uint32_t target_number; // target to use
    bake_region_id_t rid; // BAKE region id handle
	bake_target_id_t* bti; // array of target ids

	/* ... setup variables ... */

	/* Initialize Margo */
	mid = margo_init(..., MARGO_CLIENT_MODE, 0, -1);
	/* Lookup the server */
	margo_addr_lookup(mid, svr_addr_str, &svr_addr);
	/* Creates the BAKE client */
	bake_client_init(mid, &bcl);
	/* Creates the provider handle */
	bake_provider_handle_create(bcl, svr_addr, mplex_id, &bph);
	/* Asks the provider for up to target_number target ids */
	uint32_t num_targets = 0;
	bti = calloc(num_targets, sizeof(*bti));
	bake_probe(bph, target_number, bti, &num_targets);
	if(num_targets < target_number) {
		fprintf(stderr, "Error: provider has only %d storage targets\n", num_targets);
	}
	/* Create a region */
	size_t size = ...; // size of the region to create
	bake_create(bph, bti[target_number-1], size, &rid);
	/* Write data into the region at offset 0 */
	char* buf = ...;
	bake_write(bph, rid, 0, buf, size);
	/* Make all modifications persistent */
	bake_persist(bph, rid);
	/* Release provider handle */
	bake_provider_handle_release(bph);
	/* Release BAKE client */
	bake_client_finalize(bcl);
	/* Cleanup Margo resources */
	margo_addr_free(mid, svr_addr);
	margo_finalize(mid);
	return 0;
}
```

Note that a `bake_region_id_t` object is persistent.  It can be written
(into a file or a socket) and stored or sent to another program. These
region ids are what uniquely reference a region within a given target.

The rest of the client-side API can be found in `bake-client.h`.

## Provider API

The bake-server-daemon source is a good example of how to create providers and
attach storage targets to them. The provider-side API is located in
_bake-server.h_, and consists of mainly two functions:

```c
int bake_provider_register(margo_instance_id                     mid,
                           uint16_t                              provider_id,
                           const struct bake_provider_init_info* args,
                           bake_provider_t*                      provider);
```

This creates a provider at the given provider id using the specified margo
instance.  The `args` parameter can be used to modify default settings,
including passing in a fully specified json configuration block.  See
`bake-server.h` for details.

```c
int bake_provider_attach_target(bake_provider_t   provider,
                                const char*       target_name,
                                bake_target_id_t* target_id);
```

This makes the provider manage the given storage target.

Other functions are available to create and detach targets from a provider.

## Generic Bake benchmark

By using `--enable-benchmark` when compiling Bake (or `+benchmark` when using Spack),
you will build a `bake-benchmark` program that can be used as a configurable benchmark.
This benchmark requires an MPI compiler, hence you may need to configure Bake with
`CC=mpicc` and `CXX=mpicxx`.

The benchmark is an MPI program that can be run on 2 or more ranks. Rank 0 will act
as a server, while non-zero ranks act as clients. The server will not create
a Bake target. The Bake target needs to be created (with `bake-makepool`) beforehand.

The program takes as parameter the path to a JSON file containing the sequence
of benchmarks to execute. An example of such a file is located in `src/benchmark.json`.
Each entry in the `benchmarks` array corresponds to a benchmark. The `type` field indicates
the type of benchmark to execute. The `repetitions` field indicates how many times the
benchmark should be repeated.

The following table describes each type of benchmark and their parameters.

| type                 | parameter         | default | description                                                       |
|----------------------|-------------------|---------|-------------------------------------------------------------------|
|                      |                   |         |                                                                   |
| create               | num-entries       | 1       | Number of regions to create                                       |
|                      | region-sizes      | -       | Size of the regions, or range (e.g. [12, 24])                     |
|                      | erase-on-teardown | true    | Whether to erase the created regions after the benchmark executed |
|                      |                   |         |                                                                   |
| write                | num-entries       | 1       | Number of regions to write                                        |
|                      | region-sizes      | -       | Size of the regions, or range (e.g. [12, 24])                     |
|                      | reuse-buffer      | false   | Whether to reuse the input buffer for each write                  |
|                      | reuse-region      | false   | Whether to write to the same region                               |
|                      | preregister-bulk  | false   | Whether to preregister the input buffer for RDMA                  |
|                      | erase-on-teardown | true    | Whether to erase the created regions after the benchmark executed |
|                      |                   |         |                                                                   |
| persist              | num-entries       | 1       | Number of region to persist                                       |
|                      | region-sizes      | -       | Size of the regions, or range (e.g. [12, 24])                     |
|                      | erase-on-teardown | true    | Whether to erase the created regions after the benchmark executed |
|                      |                   |         |                                                                   |
| read                 | num-entries       | 1       | Number of region to read                                          |
|                      | region-sizes      | -       | Size of the regions, or range (e.g. [12, 24])                     |
|                      | reuse-buffer      | false   | Whether to reuse the same buffer for each read                    |
|                      | reuse-region      | false   | Whether to access the same region for each read                   |
|                      | preregister-bulk  | false   | Whether to preregister the client's buffer for RDMA               |
|                      | erase-on-teardown | true    | Whether to remove the regions after the benchmark                 |
|                      |                   |         |                                                                   |
| create-write-persist | num-entries       | 1       | Number of regions to create/write/persist                         |
|                      | region-sizes      | -       | Size of the regions, or range (e.g. [12, 24])                     |
|                      | reuse-buffer      | false   | Whether to reuse the same buffer on clients for each operation    |
|                      | preregister-bulk  | false   | Whether to preregister the client's buffer for RDMA               |
|                      | erase-on-teardown | true    | Whether to remove the regions after the benchmark                 |

## Manual installation

BAKE depends on the following libraries:

* uuid (install uuid-dev package on ubuntu)
* PMDK (see instructions below)
* json-c
* mochi-abt-io
* mochi-margo

Bake will automatically identify these dependencies at configure time using
pkg-config. To compile BAKE:

* `./prepare.sh`
* `mkdir build`
* `cd build`
* `../configure --prefix=/home/carns/working/install`
* `make`

If any dependencies are installed in a nonstandard location, then
modify the configure step listed above to include the following argument:

* `PKG_CONFIG_PATH=/home/carns/working/install/lib/pkgconfig`
