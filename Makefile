CARGS = -O3

.alloc.o: alloc.c
	@$(CC) $(CARGS) -c alloc.c -o alloc.o 

.server: server.c
	@$(CC) $(CARGS) server.c -o server

.client: client.c
	@$(CC) $(CARGS) client.c -o client

.server-alloc: server.c
	@$(CC) $(CARGS) -DUSECUSTOMMALLOC server.c alloc.o -o server-alloc

.client-alloc: client.c
	@$(CC) $(CARGS) -DUSECUSTOMMALLOC client.c alloc.o -o client-alloc

all: .alloc.o .server .server-alloc .client-alloc

test-alloc: .client-alloc .server-alloc
	@echo "Using custom malloc and free"
	time ./run.sh "./server-alloc" "./client-alloc"

test-default: .server .client
	@echo "Using default malloc and free"
	time ./run.sh "./server" "./client"

#Run test for both the default and custom malloc
#using the server client hashtable with six clients
run-test: test-alloc test-default