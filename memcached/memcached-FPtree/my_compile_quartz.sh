cpu_max_performance.sh
cp sekwon_Makefile Makefile
touch configure.ac aclocal.m4 configure Makefile.am Makefile.in
make
make install
./start_memcached_quartz.sh
