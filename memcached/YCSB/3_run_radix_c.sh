./bin/ycsb run memcached \
-P workloads/workloadc \
-P record.conf -s \
-threads 50 \
> 3_run_radix_c.txt

mv 3_run_radix_c.txt graph/radix
