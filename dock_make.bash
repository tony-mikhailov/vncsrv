#!/bin/bash

DOCKER=d766eb465f07

docker exec -it $DOCKER /bin/bash -c "/bin/su - build -c 'cd /home/build/vncsrv/ && rm -f vncsrv  && make'"

