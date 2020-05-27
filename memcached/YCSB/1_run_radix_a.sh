./bin/ycsb run memcached \
-P workloads/workloada \
-P record.conf -s \
-threads 50 \
> 1_run_radix_a.txt

mv 1_run_radix_a.txt graph/radix
