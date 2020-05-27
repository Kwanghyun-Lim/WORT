./bin/ycsb run memcached \
-P workloads/workloada \
-P record.conf -s \
-threads 50 \
> 1_run_hash_a.txt

mv 1_run_hash_a.txt graph/hash
