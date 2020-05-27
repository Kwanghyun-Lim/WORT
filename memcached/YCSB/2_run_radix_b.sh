./bin/ycsb run memcached \
-P workloads/workloadb \
-P record.conf -s \
-threads 50 \
> 2_run_radix_b.txt

mv 2_run_radix_b.txt graph/radix
