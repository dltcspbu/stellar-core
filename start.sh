#!/bin/sh
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/app/message-broker-build
ldconfig
./src/stellar-core test [overlay]