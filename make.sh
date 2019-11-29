g++ -ggdb main.cpp -std=c++2a -fconcepts -o wallet $(pkg-config --cflags --libs libbitcoin-client)
