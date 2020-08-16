#!/bin/bash

DOCKER=d766eb465f07

docker cp $DOCKER:/home/build/vncsrv/vncsrv ./ && scp vncsrv root@x213:~/

