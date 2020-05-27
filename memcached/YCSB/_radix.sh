./0_load_radix.sh
memstat --servers localhost 
./1_run_radix_a.sh
memstat --servers localhost 
./2_run_radix_b.sh
memstat --servers localhost
./3_run_radix_c.sh
memstat --servers localhost
./4_run_radix_f.sh
memstat --servers localhost
./5_run_radix_d.sh
memstat --servers localhost
