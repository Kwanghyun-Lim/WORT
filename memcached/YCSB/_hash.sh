echo "[[0_load_hash.sh]]" >> /home/q/new_proj/backup/src_memcached/memcached-1.4.25/trace.txt
./0_load_hash.sh
echo "[[1_run_hash_a.sh]]" >> /home/q/new_proj/backup/src_memcached/memcached-1.4.25/trace.txt
./1_run_hash_a.sh
echo "[[2_run_hash_b.sh]]" >> /home/q/new_proj/backup/src_memcached/memcached-1.4.25/trace.txt
./2_run_hash_b.sh
echo "[[3_run_hash_c.sh]]" >> /home/q/new_proj/backup/src_memcached/memcached-1.4.25/trace.txt
./3_run_hash_c.sh
echo "[[4_run_hash_f.sh]]" >> /home/q/new_proj/backup/src_memcached/memcached-1.4.25/trace.txt
./4_run_hash_f.sh
echo "[[5_run_hash_d.sh]]" >> /home/q/new_proj/backup/src_memcached/memcached-1.4.25/trace.txt
./5_run_hash_d.sh
