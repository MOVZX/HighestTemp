#!/usr/bin/env bash

if [ "$1" == "install" ]; then
    echo "Installing highesttemp service..."

    sudo cp highesttemp.service /etc/systemd/system/highesttemp.service
    sudo systemctl daemon-reload
    sudo systemctl stop highesttemp fancontrol
    sudo cp highesttemp /usr/local/bin/highesttemp
    sudo systemctl start highesttemp fancontrol

    echo "highesttemp service installed and started."

    exit 0
fi

if command -v nvidia-smi &> /dev/null
then
    echo "nvidia-smi found, compiling with NVIDIA support."

    gcc -O3 -DNVIDIA -o highesttemp highesttemp.c -lm -ldl
else
    echo "nvidia-smi not found, compiling without NVIDIA support."

    gcc -O3 -o highesttemp highesttemp.c -lm
fi
