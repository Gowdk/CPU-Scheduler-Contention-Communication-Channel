#include "producer.h"

/*
 * Controlled same-host scheduling-channel demonstration.
 *
 * Bit 1: remain CPU-runnable for the entire slot.
 * Bit 0: sleep until the slot's absolute end time.
 *
 * Both producer and consumer must use the same 
 *          1. CPU number
 *          2. slot length
 *          3. Preamble pattern
 *          4. CRC Polynomial
*/
#define STARTUP_DELAY_NS        500000000ULL
#define INTERFRAME_IDLE_SLOTS   6U

const uint8_t PREAMBLE[] = {
    0, 0, 1, 0, 1, 0, 1, 1,
    0, 0, 1, 1, 1, 0, 0, 0,
    1, 0, 1, 1, 0, 1, 1, 1,
    0, 1, 1, 0, 0, 1, 0, 0
};

volatile uint64_t workSink = 0x9e3779b97f4a7c15ULL;

// Sleep the calling process until the deadline parameter bas been reached.
void sleepUntil(uint64_t deadlineNS){
    struct timespec deadline = nanosecondsToTimespec(deadlineNS);
    int rc;

    do {
        rc = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &deadline, NULL);
    } while (rc == EINTR);

    if (rc != 0) {
        errno = rc;
        die("clock_nanosleep");
    }
}

// Take the workSink global variable and perform CPU-intensive instructions on it
//  until the end of the current slot.
void busyUntil(uint64_t deadlineNS){
    uint64_t x = workSink;

    for (;;) {
        // Work in batches of CPU_WORK_BATCHES iterations of the loop
        for (unsigned int i = 0; i < CPU_WORK_BATCHES; ++i) {
            // These are meant to be unoptimizable so that the CPU
            // is guaranteed to be affected by these instructions.
            x ^= x << 13;
            x ^= x >> 7;
            x ^= x << 17;
            x += 0x9e3779b97f4a7c15ULL;
        }

        // At the end of the CPU_WORK_BATCH, check if we've reached our deadline
        // Usage of batches means we do not have to check the current time after each CPU-intensive instruction.
        //  If we did check after each instruction, we'd be incurring too much overhead from those checks.
        //  We would be measuring the performance of monotonic_ns() in our calculations!
        if (monotonic_ns() >= deadlineNS) {
            break;
        }
    }

    workSink = x;  // Change the global variable to reflect some sort of checksum.
}

// Round up the current "value" to the nearest quantum. 
// Used to align our start time with a global starting epoch
uint64_t alignTime(uint64_t value, uint64_t quantum){
    const uint64_t remainder = value % quantum;
    return remainder == 0 ? value : value + (quantum - remainder);
}

// Continually send the bit specified in the parameters until the end of the current slot
void signalBit(uint8_t bit, uint64_t epochNS, uint64_t slotNS, size_t *slotIndex){
    uint64_t endNS = epochNS + (*slotIndex + 1) * slotNS;

    if (bit != 0U) {
        // We can either deal with a 1
        busyUntil(endNS);
    } else {
        // Or a 0
        // Sleep this process until the end of the current slot
        sleepUntil(endNS);
    }

    // Increment our running slot index for the next bit in the frame
    (*slotIndex)++;
}

// Signal a byte by signaling its 8 bits (highest to lowest)
void signalByteBIG(uint8_t byte, uint64_t epochNS, uint64_t slotNS, size_t *slotIndex){
    // Send out each bit in our byte
    for (int bit = 7; bit >= 0; bit--) {
        signalBit(((byte >> bit) & 1U), epochNS, slotNS, slotIndex);
    }
}

void signalFrame(const uint8_t *payload, size_t length, uint64_t epochNS, uint64_t slotNS, size_t *slotIndex){
    // Calculate the remainder from our polynomial
    uint8_t remainder = frameCRC(payload, length);

    // First send the preamble pattern
    for (unsigned int i = 0; i < PREAMBLE_BITS; ++i) {
        signalBit(PREAMBLE[i], epochNS, slotNS, slotIndex);
    }

    // Next send the calibration 1's
    for (unsigned int i = 0; i < CALIB_BUSY_BITS; ++i) {
        signalBit(1U, epochNS, slotNS, slotIndex);
    }

    // Then the calibration 0's
    for (unsigned int i = 0; i < CALIB_IDLE_BITS; ++i) {
        signalBit(0U, epochNS, slotNS, slotIndex);
    }

    // Now send the payload length as the first message. Used in the receivers CRC calculation
    signalByteBIG((uint8_t)length, epochNS, slotNS, slotIndex);

    // Next send the payload, character by character. Also used in the receivers CRC calculation
    for (size_t i = 0; i < length; ++i) {
        signalByteBIG(payload[i], epochNS, slotNS, slotIndex);
    }

    // Finally, send the crc calculated earlier.
    signalByteBIG(remainder, epochNS, slotNS, slotIndex);
}

// In the event incorrect command line arguments are provided
void usage(char *argv0){
    fprintf(stderr,
            "Usage: %s [-c cpu] [-s slotMS] [-r repeats] message\n"
            "  -c cpu           logical CPU used by both processes (default %d)\n"
            "  -s slotMS        symbol duration in milliseconds (default %.1f)\n"
            "  -r repeats       number of repeated frames (default %d)\n",
            argv0, DEFAULT_CPU, DEFAULT_SLOT_MS, DEFAULT_REPEATS);
}

int main(int argc, char **argv){
    // Command line arguments
    int cpu = DEFAULT_CPU;
    double slotMS = DEFAULT_SLOT_MS;
    unsigned long repeats = DEFAULT_REPEATS;
    int option;

    // Parse our command line arguments.
    while ((option = getopt(argc, argv, "c:s:r:b:h")) != -1) {
        switch (option) {
        case 'c':
            cpu = atoi(optarg);
            break;
        case 's':
            slotMS = strtod(optarg, NULL);
            break;
        case 'r':
            repeats = strtoul(optarg, NULL, 10);
            break;
        default:
            usage(argv[0]);
            return option == 'h' ? EXIT_SUCCESS : EXIT_FAILURE;
        }
    }

    // Command line argument error checking.
    if (optind >= argc || cpu < 0 || slotMS <= 0.0 || repeats == 0UL) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    // Set up our message from the command line.
    char *message = argv[optind];
    size_t length = strlen(message);
    if (length > MAX_PAYLOAD) {
        fprintf(stderr, "Message is too long; maximum is %u bytes.\n",
                MAX_PAYLOAD);
        return EXIT_FAILURE;
    }

    // Set up our slot timings.
    uint64_t slotNS = (uint64_t)(slotMS * NS_IN_MS);

    // Pin the process to the CPU provided in the command line arguments.
    pinToCpu(cpu);
    printf("slot %.3f ms; sending %zu bytes "
            "in %lu frame(s).\n", (double)slotNS / 1000000.0, length, repeats);

    // slot index tracker
    size_t slotIndex = 0;

    // Set up our global starting epoche time.
    // We also want to incur some sort of start up delay so that we don't
    //      have to worry about start up jitter/noise.
    uint64_t epochNS = alignTime(monotonic_ns() + STARTUP_DELAY_NS, slotNS);
    sleepUntil(epochNS);

    // Begin sending out frames
    for (unsigned long repetition = 0; repetition < repeats; repetition++) {
        // Emit the frame
        signalFrame((uint8_t *)message, length, epochNS, slotNS, &slotIndex);

        // Send out idle slots after each frame as a buffer between frames.
        for (unsigned int i = 0; i < INTERFRAME_IDLE_SLOTS; ++i) {
            signalBit(0U, epochNS, slotNS, &slotIndex);
        }
    }

    return EXIT_SUCCESS;
}
