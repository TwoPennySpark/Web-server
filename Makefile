all:
	gcc master.c -o master -g -Wall -Werror --std=c99
	gcc worker.c -o workerwebserv -g -Wall -Werror --std=c99
