./bin/ycsb run memcached \
-P workloads/workloadd \
-P record.conf -s \
-threads 50 \
> 5_run_hash_d.txt

mv 5_run_hash_d.txt graph/hash
