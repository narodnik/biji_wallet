#!/bin/bash
g++ main.cpp $(pkg-config --cflags --libs biji) -std=c++2a -fconcepts -o biji-example

