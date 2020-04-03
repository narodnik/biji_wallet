#!/bin/bash -x

#g++ -ggdb main.cpp -std=c++2a -fconcepts -o wallet $(pkg-config --cflags --libs libbitcoin-client)

CFLAGS="$(pkg-config --cflags libbitcoin-client) -Iinclude/ -std=c++2a -fconcepts"
LIBS=$(pkg-config --libs libbitcoin-client)

echo $CFLAGS

mkdir -p build/
g++ -c examples/main.cpp $CFLAGS -o build/main.o
g++ -c src/biji.cpp $CFLAGS -o build/biji.o
g++ -c src/get_history.cpp $CFLAGS -o build/get_history.o
g++ -o build/bijiwallet build/*.o $LIBS

