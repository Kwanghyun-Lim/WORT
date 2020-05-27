./bin/ycsb load memcached \
-P workloads/workloada \
-P record.conf -s \
-threads 50 \
> 0_load_b+.txt

mv 0_load_b+.txt graph/b+
