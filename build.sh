#!/usr/bin/env bash

if command -v nvidia-smi &> /dev/null
then
    echo "nvidia-smi found, compiling with NVIDIA support."

    gcc -O3 -DNVIDIA -o highesttemp highesttemp.c -lm -ldl
else
    echo "nvidia-smi not found, compiling without NVIDIA support."

    gcc -O3 -o highesttemp highesttemp.c -lm
fi
