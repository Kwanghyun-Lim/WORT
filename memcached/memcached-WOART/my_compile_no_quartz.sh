cpu_max_performance.sh
cp sekwon_Makefile_no_quartz Makefile
#touch configure.ac aclocal.m4 configure Makefile.am Makefile.in
make
make install
./start_memcached.sh
