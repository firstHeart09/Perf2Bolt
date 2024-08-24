#/bin/bash
./build_bolt.sh
perf record -e cycles:u -j any,u -o ../perf.data ../reorder
./build_perf2bolt.sh &> branch.log

