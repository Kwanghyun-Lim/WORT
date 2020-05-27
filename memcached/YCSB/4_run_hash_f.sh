./bin/ycsb run memcached \
-P workloads/workloadf \
-P record.conf -s \
-threads 50 \
> 4_run_hash_f.txt

mv 4_run_hash_f.txt graph/hash
