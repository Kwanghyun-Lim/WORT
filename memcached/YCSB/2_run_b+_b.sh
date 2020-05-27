./bin/ycsb run memcached \
-P workloads/workloadb \
-P record.conf -s \
-threads 50 \
> 2_run_b+_b.txt

mv 2_run_b+_b.txt graph/b+
