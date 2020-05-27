cd /usr/local/src
wget http://monkey.org/~provos/libevent-1.3e.tar.gz
tar xvzf libevent-1.3e.tar.gz
cd libevent-1.3e
./configure
make
make install
echo "/usr/local/lib/" > /etc/ld.so.conf.d/libevent.conf
ldconfig -v
