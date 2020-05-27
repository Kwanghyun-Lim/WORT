./bin/ycsb run memcached \
-P workloads/workloadd \
-P record.conf -s \
-threads 50 \
> 5_run_radix_d.txt

mv 5_run_radix_d.txt graph/radix
