./bin/ycsb load memcached \
-P workloads/workloada \
-P record.conf -s \
-threads 50 \
> 0_load_hash.txt

mv 0_load_hash.txt graph/hash
