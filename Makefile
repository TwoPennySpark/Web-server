all:
	gcc master.c -o master -Wall --std=c11
	gcc worker.c -o workerwebserv -Wall --std=c11
