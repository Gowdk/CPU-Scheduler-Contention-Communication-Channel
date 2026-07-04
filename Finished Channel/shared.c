/*
    Functions shared between producer.c and consumer.c
    in the Scheduler Contention Covert Timing Channel
*/

#include "shared.h"

struct timespec nanosecondsToTimespec(uint64_t nanoseconds){
    struct timespec ts;
    ts.tv_sec = (time_t)(nanoseconds / NS_IN_S);
    ts.tv_nsec = (long)(nanoseconds % NS_IN_S);
    return ts;
}

void pinToCpu(size_t cpu){
    cpu_set_t set;  // This is an interesting data structure supplied by sched.h
                    // Represents a "set of logical CPUs"
                    // Bitmask operations inside a data structure for better abstraction?? Woah!

    // Assert cpu is valid. size_t cannot be negative so don't worry abt that.
    // CPU_SETSIZE is the max number of CPU indicies that a cpu_set_t can represent.
    if (cpu >= CPU_SETSIZE) {
        fprintf(stderr, "CPU index %zu is outside CPU_SETSIZE.\n", cpu);
        exit(EXIT_FAILURE);
    }

    // Clear the CPU set. Important because "set" may contain unspecified data. We don't like random data...
    /* 
        Conceptually (after CPU_ZERO()) set looks like:
        CPU 0: not selected
        CPU 1: not selected
        CPU 2: not selected
        etc...
    */
    CPU_ZERO(&set);
    

    // Set the desired CPU for our cpu_set_t data structure.
    /* 
        Conceptually (After CPU_SET() call) set looks like:
        CPU 0: not selected
        CPU 1: not selected
        CPU 2: selected
        etc...
    */
    CPU_SET(cpu, &set);

    // Take our cpu_set_t data structure and hand it over to the sched_setaffinity() system call to 
    //  make sure the desired CPU is used.
    if (sched_setaffinity(0, sizeof(set), &set) != 0) {
        die("sched_setaffinity");
    } else {
        printf("Process pinned to CPU %zu\n", cpu);
    }
}

void die(const char *message){
    perror(message);
    exit(EXIT_FAILURE);
}

uint64_t monotonic_ns(void){
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        die("clock_gettime");
    }
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

uint8_t shiftRegisterCRC8(uint8_t remainder, uint8_t value){
    /*
        Begin by XORing our current remainder against the value provided.
            This "feeds" the value into our remainder.
        Notice how we're keeping a running value here, as the remainder is the output and input
            into this function. This allows us to keep a running remainder for the entire
            message processed so far.
    */ 
    remainder ^= value;   
    for (unsigned int i = 0; i < BITS_IN_ONE_BYTE; i++) {
        // If the leading 1 is the highest bit, we want to shift to the left and then XOR
        //  against our polynomial's lowest terms (G(x) = x^8 + x^2 + x + 1, so take x^2 + x + 1 == 00000111).
        //  Notice how we discard overflows (x^8)!  
        // Else just shift left until we hit 1 in highest bit.
        remainder = (remainder & 0x80)  ? (uint8_t)((remainder << 1U) ^ 0x07)
                                        : (uint8_t)(remainder << 1U);
    }
    return remainder;
}

uint8_t frameCRC(const uint8_t *payload, size_t length){
    uint8_t remainder = 0U;
    // Start with the length byte. 
    remainder = shiftRegisterCRC8(remainder, (uint8_t)length);

    // Next, run through the payload message.
    for (size_t i = 0; i < length; ++i) {
        remainder = shiftRegisterCRC8(remainder, payload[i]);
    }

    return remainder;
}
