Microbenchmarks for debugging the issue here (https://github.com/cosmoss-vt/mv-rlu/issues/4):

# To run the microbenchmark:

- git clone git clone git@github.com:maximilian1064/mvrlu-dchain-debug.git --recurse-submodules

- change mv-rlu ordo value to your system value

- I pin the mv-rlu gp thread to core id 46 (https://github.com/dslab-epfl/mv-rlu/blob/5a499ee79eb58120296b7d42127d44619dd4e28f/lib/mvrlu.c#L1227), change it to an appropriate core on your system.
  Note: do not unpin the gp thread since this might be related to the bug; also do not pin the gp\_thread to the same core as other thread

- cd deps/mv-rlu/lib && make libmvrlu-ordo.a CC=gcc-8 (I use gcc gcc version 8.4.0 (Ubuntu 8.4.0-3ubuntu2), make sure you use the same version if possible)

- cd tests/ && make run-test NUM\_CORES=... (by default the threads are pined to core 0 to core #cores-1)

I tried and this microbenchmark did not trigger the bug I reported in the issue. But maybe it will do after tweaking
some parameters (#cores, dchain size, access pattern).

# Some docs on the data structure and the bug scenario:

This directory contains the code for the double chain index allocator, which uses
mv-rlu. The API is defined in `include/double-chain.h`

Not showing the application code in the bug scenario since it is too complicated, but essentially it uses 8 cores, and concurrently de-/allocates indexes from the double-chain index allocator.
Each index allocated is de-allocated after a random amount of time (~3 secs). The application runs for ~ 8 seconds. The total number of de-/allocation requests is ~ 16M. The dchain size is 3250000 and utilization is close to 100%.

Line 233 of `double-chain-impl.c` is where all bug cases I have seen happened.

I would like to provide an env for reproducing the bug but it requires special hardware (NICs), maybe you can tune
the above microbenchmark according the bug scenario to trigger the bug.

