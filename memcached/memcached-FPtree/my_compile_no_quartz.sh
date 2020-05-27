cpu_max_performance.sh
touch configure.ac aclocal.m4 configure Makefile.am Makefile.in
make
make install
./start_memcached.sh
