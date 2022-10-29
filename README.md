# Assignments
## Assignment I
### Malloc and Free
The memory mapping is organized as linked list. 
Each allocation has its own entry in the list, which contains
information about its size and whether the entry is still in use.
Thread safty is achived by only allowing one thread to 
manipulate the list.
#### Malloc
When `malloc` is called, it traverses the list and looks for
free entries of sufficent size. 
In case the memory block of the entry is sufficenlty sized
the block is either split at the size boundary to avoid
waste of memory inbetween entries or used directly for the new allocation.
Otherwise additional memory is allocated for the required size and list entry.
    
#### Free
The `free` function flags entries as free and merges free
neighbours. If the last entry is free, the allocation size is 
decreased by calling `sbrk`.

## Assignment II
The application consists of a client/server communication over a shared memory
buffer using offsets in the buffer to handle concurrent requests. 
```bash
 make run-test
```
Can be used to run test scenarios on the hash table using the previously implemented allocator or the system default.</br>
The test runs multiple client instances accessing the server's hash table and
examining its responses.

## Restrictions
### Allocation
* In some specific error cases the behavior might slightly differ from more common malloc implementations. 
### Hash table
* The maximal data size a single key can store is set at compile time.
* The number of concurrent connections, the server can handle is set at compile time.



