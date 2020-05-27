./bin/ycsb run memcached \
-P workloads/workloadc \
-P record.conf -s \
-threads 50 \
> 3_run_b+_c.txt

mv 3_run_b+_c.txt graph/b+
