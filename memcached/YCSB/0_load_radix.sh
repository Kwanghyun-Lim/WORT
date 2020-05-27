./bin/ycsb load memcached \
-P workloads/workloada \
-P record.conf -s \
-threads 50 \
> 0_load_radix.txt

mv 0_load_radix.txt graph/radix
