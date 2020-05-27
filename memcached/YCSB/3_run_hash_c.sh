./bin/ycsb run memcached \
-P workloads/workloadc \
-P record.conf -s \
-threads 50 \
> 3_run_hash_c.txt

mv 3_run_hash_c.txt graph/hash
