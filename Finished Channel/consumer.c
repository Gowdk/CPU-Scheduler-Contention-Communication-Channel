#include "consumer.h"

/*
    Timeline for the consumer:
        1) Set up the samples data structure to represent each "TIME SLOT" window
            -> Each window will be further divided into 8 "bins" that will be used to measure smaller windows within the
            current time slot. The median of these 8 bins will be used as the value observed from each sample.
            -> Each time slot window will correspond to a single bit within the encoded message
        2) Begin asking the question "When have we found the start of a valid frame?"
            -> Measure samples until we have hit a total of 32 samples, decode the bits (32) to find if the pattern of work count
            has a strong correlation to the expected values of the well-known preamble pattern.
                TECHNIQUE USED: Calculate the Normalized Cross-Correlation between the pattern observed
                                from the 32 samples and the expected preamble pattern.
                                        If r >= 0.72,
                                            we have found a valid frame!
                                        else
                                            append the next sample to the sample data structure, and
                                                look at the next 32 "indices" (in this case, we'd be looking
                                                at indices 1-33) for a potential valid frame start.

                                        
        3) Once a valid frame start has been found, we can continue to listen for the next:
            -> 8 bits of High calibration                       (What is a 1?)
            -> 8 bits of Low calibration                        (What is a 0?)
            -> 8 bits detailing the length (n) of the message   (How long is the message?)
            -> N bits of the message                            (What is the actual message?)
            -> 8 bits of a CRC8                                 (Was there any accidental data corruption in the message?)

        4) Decode the entirety of the (32 + N) bits of information
        5) Calculate our own CRC8, and compare against the 8 bits of the CRC8 decoded from the information above
            If a match is found, 
                we have succesfully received the message communicated by the producer process
                print out message and metrics
            Else, 
                Count this as a failed candidate
                Go back to (2), where we look for the next valid frame start (preamble)
                    
    Considerations:
        We are working with "relatively" small window sizes (<= 100ms). We are also trying to measure our 
            CPU utilization in order to notice fluctuations in said utilization to infer data.

        Thus, we need to make our program small, and efficient. These are the ways I have attempted to do so. I'm assuming there are better
            options, but for the sake of completing this project in a 2-week window, this is my "architecture":
            -> Calculate a Correlation metric between the expected/observed preamble pattern (32 bits) to find a valid frame start
            -> Use a small calibration window (8 high/8 low bits = 16 bits) to observe how 1s and 0s should be inferred from
                the current frame.
            -> Use CRC8 as it's a lightweight (8 bits) tool to detect errors in data transmission
                It also uses XOR operations, so it's directly utilizing specialized hardware for efficiency!!
        
    Things that are new to me in this project:
        - Been a while since I've had to work with double values
        - Normalized Cross-Correlation
        - CRC8 (this was actually fun to learn. I loved working with boolean logic in COMP 2280)
        - Using medians in a thoughtful way
        - Jitter with communication channels (transitions between time slot windows)
        - Covert Communicaiton Channels
            Timing Channels
            Storage Channels (Not this implementation, but a cool read)
        - Utilizing inference to validate shared frameState.between two processes that do not use IPC
            This was akin to threads trying to synchronize data, but threads can simply look at the shared values and 
                directly observe (rather than inferring) changes or frameState.
            Synchronizing through inference was a huge challenge here, especially with all the noise involved with the operating system.
                I was essentially asking "Without directly me what you're saying, tell me what you're saying..."
            Implementation-wise, this looked like having a data structure that held metadata (shoutout COMP 3430 for the idea) about 
                the message, and then utilizing the correct well-known pattern of bits to infer information by having check stops
                scattered throughout the listening phase dependent on the frameState.of the current frame.
                - The First idea that popped into my head was to record all of the bits for
                    some arbitrarily high number of bits, and then try to decode the message after all bits after this was done.
                    At the cost of space complexity AND time complexity, I believe this would allow reliability
                    and accuracy to increase to the point of "almost" always working. This is nice, but I don't like sacrificing
                    space and time when there should exist an inverse relationship between the two.
                - Another strategy would be to infer information, byte by byte (or some fixed number of bits), checking if we're matching some
                    sort of expected value. But this sounds like it would be incredibly slow:
                        - We'd have a lot of monotonic_ns() calls, as we'd be synchronizing frameState.after each fixed number of bits.
                        - We'd have a nightmare complexity-wise as tracking the accuracy of each fixed number of bits would be a growing problem. 
                - Remzi H. Arpaci-Dusseau (OSTEP) teaches that if you can come up with two solutions to a problem, you can analyze them
                    by creating trade-offs, find what works well and what doesn't, then try to take the best of both worlds to come up
                    with a better solution!
                - So here's where I believe the best of both worlds leads us. Start off by recording each time slot as we listen to a potential frame.
                    By doing this, we can break frames into multiple well-known phases that are large enough such that we don't 
                    dominate our performance with monotonic_ns() calls or other performance-reducing code.
                    This small number of checkstops inside each frame will allow us require a certains frameState.before advancing to the next phase 
                    of this given frame. 
                    
                    These approaches (Breaking up a frame into well-known phases AND having a requirement of frameState.in a small number of phases),
                    gives us performance and reliability!
                     
                    This idea of a performative + reliable covert communication channel is the main focus of where this research is heading.
                        "How fast can we send messages through covert channels while remaining reliable?"
                            Thus,
                            1) How fast is fast enough?
                            2) How long can the message be?
                            3) Can anyone notice that we're sending messages?
                            4) How reliable is reliable enough? What are the safeguards for if we fail sending a message?
*/


// Refer to shared.h for comments
const uint8_t PREAMBLE[PREAMBLE_BITS] = {
    0, 0, 1, 0, 1, 0, 1, 1,
    0, 0, 1, 1, 1, 0, 0, 0,
    1, 0, 1, 1, 0, 1, 1, 1,
    0, 1, 1, 0, 0, 1, 0, 0
};

// Refer to shared.h for comments
volatile uint64_t workSink = 0xd1b54a32d192ed03ULL;

/*
    =====================================================
    === HELPER FUNCTIONS PROVIDED BY EXTERNAL SOURCES ===
    =====================================================
*/

// Source: LLM. This came with the function below.
static int compare_double(const void *left, const void *right)
{
    const double a = *(const double *)left;
    const double b = *(const double *)right;
    return (a > b) - (a < b);
}

// Source: LLM. Asked Chat for a function that takes the median of two double values.
static double median(double *values, size_t count)
{
    qsort(values, count, sizeof(values[0]), compare_double);
    // Return correctly indexed value dependent on even/odd size.
    if ((count & 1) != 0) {
        return values[count / 2];
    }
    return (values[count / 2 - 1] + values[count / 2]) / 2.0;
}

// Reusing code for when wrong commandline arguments are provided
void usage(char *argv0){
    fprintf(stderr,
            "Usage: %s [-c cpu] [-s slotMS] [-n nice] [-b numSubSlots]\n"
            "  -c cpu               logical CPU used by both processes (default %d)\n"
            "  -s slotMS            symbol duration in milliseconds (default %.1f)\n"
            "  -n nice              consumer niceness, 0..19 (default %d)\n"
            "  -b numSubSlots       number of sub slots for each slot (default %d)\n",
            argv0, DEFAULT_CPU, DEFAULT_SLOT_MS, DEFAULT_NICE, DEFAULT_SUB_SLOTS);
}

/*
    =================================
    === FUNCTIONS BUILT BY MYSELF ===
    =================================
*/

double divideSlot(const uint64_t *samples, size_t slotStart, size_t numSubSlots){
    double interior[numSubSlots - 2];   // We'll only be interested in the inner N-2 sub slots

    // Fill up interior by copying the samples over.
    for (size_t i = 0; i <= numSubSlots - 2; i++) {
        interior[i] = (double)samples[slotStart + (i + 1)];
    }

    // Return the MEDIAN of the values inside interior.
    return median(interior, numSubSlots - 2);
}

// With help from AI
double preambleCorrelation(const uint64_t *samples, size_t frameStart, size_t numSubSlots){
    double observed[PREAMBLE_BITS];
    double mean = 0.0;
    double numerator = 0.0;
    double observedEnergy = 0.0;
    double expectedEnergy = 0.0;
    double correlation = 0.0;

    // Calculate the mean of PREAMBLE_BITS from the sample parameter.
    //  Will be used to compare against the actual PREAMBLE_BITS
    //  in calculating the correlation.
    // We will also record the "most likely" work count for each slot.
    for (size_t i = 0; i < PREAMBLE_BITS; i++) {
        observed[i] = divideSlot(samples, frameStart + (i * numSubSlots), numSubSlots);
        mean += observed[i];
    }
    mean /= PREAMBLE_BITS;
    
    // Calculate the Normalized cross-correlation. The preamble contains
    //      an equal number of 1s and 0s, thus the bipolar representation has a mean
    //      of 0, making the calculation effectively the Pearson correlation coefficient
    //      between the observations and the expected preamble.
    for (size_t i = 0; i < PREAMBLE_BITS; i++) {
        // Busy bits are represented by 1, which should lower B's iteration count. Map 1s to -1.
        // Idle bits are represented by 0, which should raise B's iteration count. Map 0s to +1.
        // This process is known as "converting an expected variable to a bipolar signal"
        //      Effect: Gives our expected value that will be plugged into the correlation function
        double expected = PREAMBLE[i] != 0U ? -1.0 : 1.0;

        // Subtract the mean of observed values from the observed value.
        //      Effect:     Removes the baseline CPU-performance level. Comparison
        //                  focuses on the alternating busy/idle pattern instead 
        //                  of the "absolutely measurement magnitude."
        double centered = observed[i] - mean;

        // (Xi - Xbar) * ei
        numerator += centered * expected;

        // For our denominator (Normalization)
        // (xi - xbar)^2
        observedEnergy += centered * centered;

        // (ei)^2
        expectedEnergy += expected * expected;
    }

    // Else calculate the correlation and return it.
    correlation = numerator / sqrt(observedEnergy * expectedEnergy);
    return correlation;
}

bool tryLock(const uint64_t *samples, size_t frameStart, FrameState *frameState, size_t numSubSlots){
    double correlation = preambleCorrelation(samples, frameStart, numSubSlots);
    if (correlation < CORRELATION_MIN) {
        return false;
    }

    // Instance Variables
    double busyCounts[CALIB_BUSY_BITS];
    double idleCounts[CALIB_IDLE_BITS];
    double busyMedian = 0.0;
    double idleMedian = 0.0;
    double threshold = 0.0;
    double separation = 0.0;
    size_t calibrationStart = PREAMBLE_BITS;
    size_t slotIndex = 0;

    // Fill in our busy work counts
    for (size_t i = 0; i < CALIB_BUSY_BITS; ++i) {
        slotIndex = calibrationStart + i;
        busyCounts[i] = divideSlot(samples, frameStart + slotIndex * numSubSlots, numSubSlots);
    }

    // Fill in our idle work counts 
    for (size_t i = 0; i < CALIB_IDLE_BITS; ++i) {
        slotIndex = calibrationStart + CALIB_BUSY_BITS + i;
        idleCounts[i] = divideSlot(samples, frameStart + slotIndex * numSubSlots, numSubSlots);
    }

    // Now set up our threshold for when a message should be inferred to a 1 or a 0.
    busyMedian = median(busyCounts, CALIB_BUSY_BITS);
    idleMedian = median(idleCounts, CALIB_IDLE_BITS);
    threshold = (busyMedian + idleMedian) / 2.0;

    // Assertion of a valid threshold
    if (threshold <= 0.0 || idleMedian <= busyMedian) {
        return false;
    }

    
    //  If we created confidence intervals around these medians and they are not separated enough, we
    //      wouldn't be able to conclude with a significant amount of confidence    
    //      that the idle/busy counts produced would accurately infer the
    //      proper 1 or 0 it is intended to be.
    separation = (idleMedian - busyMedian) / threshold;
    if (separation < SEPARATION_MIN) {
        return false;
    }

    // Recall that we have a shared frameState in the form of a frame. Save it in our data structure passed as a param
    frameState->found = true;
    frameState->frameStartSlot = frameStart;
    frameState->busyMedian = busyMedian;
    frameState->idleMedian = idleMedian;
    frameState->threshold = threshold;
    frameState->correlation = correlation;

    // Will be changed when we decode the length byte within the frame
    frameState->lengthKnown = false;
    frameState->payloadLength = 0U;

    // Success!
    return true;
}

size_t measureSlot(uint64_t endOfSlotNS){
    uint64_t x = workSink; // Fetch the most recent version of the workSink
    size_t batches = 0;

    // While we have not reached the end of the current timing slot,
    //  record cpu work.
    while(1){
       // Work in batches of CPU_WORK_BATCHES iterations of the loop
        for (unsigned int i = 0; i < CPU_WORK_BATCHES; ++i) {
            // These are meant to be unoptimizable so that the CPU
            // is guaranteed to be affected by these instructions.
            x ^= x << 13;
            x ^= x >> 7;
            x ^= x << 17;
            x += 0x9e3779b97f4a7c15ULL;
        }
        batches++;

        // At the end of the CPU_WORK_BATCH, check if we've reached our deadline
        // Usage of batches means we do not have to check the current time after each CPU-intensive instruction.
        //  If we did check after each instruction, we'd be incurring too much overhead from those checks.
        //  We would be measuring the performance of monotonic_ns() in our calculations!

        // Note that we could record MORE than what we want.
        //      We solve this by:
        //      1) Having a large enough time slot such that overestimates in batches are not statistically significant
        //      2) Getting rid of the outter most subslots when inferring what bit we are receiving. (And thus having large enough sub slots
        //          such that errors do not carry into the inner sub slots)
        if (monotonic_ns() >= endOfSlotNS) {
            break;
        }
    }

    workSink = x; // Set the global checksum
    return batches;
}

// Single bit decoding used to infer a 1 or a 0 based on if the median value of the sample provided
//      is below or above the threshold parameter.
uint8_t decodeBit(const uint64_t *samples, size_t frameStart, size_t slotIndex, double threshold, size_t numSubSlots){
    // Find the median of value of the numbSubSlots found at position:
    //       frameStart + (slotIndex * numSubSlots)
    double median = divideSlot(samples, frameStart + slotIndex * numSubSlots, numSubSlots);

    // If the median value is less than the threshold, decode the bit as a 1, else decode as a 0.
    return median < threshold   ? 1U 
                                : 0U;
}

/*
    Single byte decoding used to infer if each bit of the byte is a 1 or a 0 based on if the median value of the sample provided
        is below or above the threshold parameter. 
    Useful for interpretting words or numbers instead of single bits.
        frameStart == the start of the current valid frame.
        firstSlot == the first slot corresponding to the start of the byte being decoded.
*/
uint8_t decodeByte(const uint64_t *samples, size_t frameStart, size_t firstSlot, double threshold, size_t numSubSlots){
    uint8_t value = 0U;
    // For each bit (starting with the most significant), decode that bit, then shift it over to the left.
    //      The final result will contain the big endian version uint8_t variable that can be interpretted
    //      as needed.
    for (size_t bit = 0; bit < BITS_IN_ONE_BYTE; bit++) {
        value = (uint8_t)((value << 1U) | decodeBit(samples, frameStart, firstSlot + bit, threshold, numSubSlots));
    }

    return value;
}

void setConstraints(int cpu, int niceVal){
    pinToCpu(cpu);
    if (setpriority(PRIO_PROCESS, 0, niceVal) != 0) {
        die("setpriority");
    }
}

int main(int argc, char **argv){
    // Command Line Arguments
    int cpu = DEFAULT_CPU;
    uint64_t slotMS = DEFAULT_SLOT_MS;
    size_t niceVal = DEFAULT_NICE;
    size_t numSubSlots = DEFAULT_SUB_SLOTS; 
    int option;

    // Read the parameters provided by command line
    while ((option = getopt(argc, argv, "c:s:n:b:h")) != -1) {
        switch (option) {
        case 'c':
            cpu = atoi(optarg);
            break;
        case 's':
            slotMS = strtod(optarg, NULL);
            break;
        case 'n':
            niceVal = atoi(optarg);
            break;
        case 'b':
            numSubSlots = atoi(optarg);
            break;
        default:
            usage(argv[0]);
            return option == 'h' ? EXIT_SUCCESS : EXIT_FAILURE;
        }
    }

    // Error check the command line arguments
    if (cpu < 0 || slotMS < 100 || niceVal < 0 || niceVal > 19) {
        // slotMS lower bound was calculated based on the 
        // Lowest/Average/Highest P("Successful transmission of 11 byte message") data through 60 trials in each:
        //      60ms:   Lowest = 36%        Average = 49.4%     Highest = 60.0%
        //      75ms:   Lowest = 70%        Average = 73.3%     Highest = 76.7%
        //      100ms:  Lowest = 76.7%      Average = 84.0%     Highest = 96.7%
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    // Necessary constraints to see some sort of accuracy from our demonstration.
    setConstraints(cpu, niceVal);

    /*
        Setting up the Sampling Data Structures
    */

    // Setting up our slot time (nano seconds). Recall that the consumer
    // uses a bin size (number of subsamples per slot) to divy up listening phases.

    // Begin by converting miliseconds to nano seconds. This is needed for our time mechanism.
    // Error checking was done ahead of time. slotMS < 100.0ms is not accepted as a command line parameter. Refer to above
    uint64_t slotNS = (uint64_t)(slotMS * NS_IN_MS);
    slotNS -= slotNS % numSubSlots;       // Round down to the nearest multiple of numSubSlots
    uint64_t sampleNS = slotNS / numSubSlots;

    uint64_t *samples = malloc(sizeof(uint64_t) * MAX_SAMPLES);
    if(samples == NULL){
        die("Couldn't allocated memory for samples");
    }

    // Setup our data structure housing our frame's frameState.
    // We're using a stack allocated variable here! 
    FrameState frameState = {0};

    // Experimental Variables
    size_t sampleCount = 0;
    size_t failedFrames = 0;
    uint64_t experimentStartTime = monotonic_ns();  // Our initial epoche
    uint64_t experimentEndTime = 0;
    uint64_t frameFoundTimeNS = 0;

    // Set up our next deadline for measuring
    uint64_t nextDeadline = experimentStartTime + sampleNS; 

    // Set up all well-known offsets into our frame
    size_t prefixSampleSize = PREFIX_BITS * numSubSlots;
    size_t dataStartSlot = PREFIX_BITS; // Where the "length byte" begins.
    size_t payloadStartSlot = PREFIX_BITS + LENGTH_BITS;

    // Poll for samples from the producer!
    /*
        Poll for samples from the producer, making use of currSlotOffset that acts as a read cursor
        throughout a given frame. 

        Makes heavy use of the Frame Constants defined in shared.h
    */
    while(1){
        // If we have reached our max sample count without a valid frame,
        //  we should probably get out of here.
        if (sampleCount >= MAX_SAMPLES) {
            fprintf(stderr, "Sample buffer exhausted without a valid frame.\n");
            free(samples);
            return EXIT_FAILURE;
        }

        // Record how many iterations of work we can do in this current slot (before the deadline)
        samples[sampleCount++] = measureSlot(nextDeadline);
        nextDeadline += sampleNS;   

        /*
            ========================
            === FIRST CHECKSTOP: ===
            ===   THE PREAMBLE   ===
            ========================
        */

        // If we haven't gotten to a valid frame yet, frameState should not be locked
        if (!frameState.found) {
            // Only attempt to find a valid frame if we have gotten past the samples included in all PREFIX_BITS.
            // PREFIX_BITS are those in the preamble as well as 8 high + 8 low calibration bits.
            if (sampleCount >= prefixSampleSize) {
                if(tryLock(samples, sampleCount - prefixSampleSize, &frameState, numSubSlots)){
                    // If our tryLock is successfull, we have found a valid frame
                    frameFoundTimeNS = monotonic_ns();    // Let's start tracking start time for goodput
                }
            }
            continue;
        }

        /*
            =========================
            === SECOND CHECKSTOP: ===
            ===  THE LENGTH BYTE  ===
            =========================
        */
        size_t lengthEndSlot = frameState.frameStartSlot + (payloadStartSlot) * numSubSlots;
        
        // If we have a valid frame, and we have measured until the end of the length phase of the frame,
        //      our samples data structure now has all of the bits for the length phase!
        if (!frameState.lengthKnown && sampleCount >= lengthEndSlot) {
            if((frameState.payloadLength = decodeByte(samples, frameState.frameStartSlot, dataStartSlot, frameState.threshold, numSubSlots)) > 0)
                frameState.lengthKnown = true;
        }

        // Else if we do not know the length yet, so we need to continue polling for more samples.
        if (!frameState.lengthKnown) {
            continue;
        }

        /*
            =========================
            === FOURTH CHECKSTOP: ===
            ===    THE PAYLOAD    ===
            =========================
        */

        // Calculate the total bits in the data phase of our frame
        //      and then use that to calculate the slot when our frame ends.
        size_t totalDataSlots = LENGTH_BITS + (frameState.payloadLength * BITS_IN_ONE_BYTE) + CRC_BITS;
        size_t frameEndSample = frameState.frameStartSlot + ((dataStartSlot + totalDataSlots) * numSubSlots);

        // If we haven't polled and collected all of our frame, continue to the next iteration
        if (sampleCount < frameEndSample) {
            continue;
        }

        // Now that we have the full frame, we just need to decode and validate our CRC!
        uint8_t payload[MAX_PAYLOAD + 1];  // +1 for null terminator
        size_t currSlotOffset = dataStartSlot + 8U; // start of data + length byte

        // Decode the payload message, byte by byte.
        for (size_t i = 0; i < frameState.payloadLength; ++i) {
            payload[i] = decodeByte(samples, frameState.frameStartSlot, currSlotOffset, frameState.threshold, numSubSlots);
            currSlotOffset += BITS_IN_ONE_BYTE;
        }

        // Tag on our null terminator.
        payload[frameState.payloadLength] = '\0';

        /*
            ================================
            === SIXTH (FINAL) CHECKSTOP: ===
            ===  THE CRC CHECKSUM BYTE   ===
            ================================
        */

        // Decode the crc inside the last byte of the frame (recall that currSlotOffset was previously incremented
        // in the for loop above)
        uint8_t receivedCRC = decodeByte(samples, frameState.frameStartSlot, currSlotOffset, frameState.threshold, numSubSlots);

        // Reconstruct the CRC from the received payload so that we can compare
        uint8_t calculatedCRC = frameCRC(payload, frameState.payloadLength);

        // Finally, compare
        if (receivedCRC == calculatedCRC) {
            // End the experiment, we're done!
            experimentEndTime = monotonic_ns();

            // Print out the payload!!
            printf("Received %u byte(s): ", frameState.payloadLength);
            if (frameState.payloadLength > 0U) {
                printf("%s", payload);
            }

            // Metrics
            printf("\ncorrelation=%.3f busy=%.1f idle=%.1f "
                "threshold=%.1f failed_candidates=%zu\n",
                frameState.correlation, frameState.busyMedian, frameState.idleMedian,
                frameState.threshold, failedFrames);
            
            printf("E2E Latency (T_CRCaccepted - T_beginListening): %.6f seconds.\n", (double)(experimentEndTime - experimentStartTime) / 1e9);
            printf("Bit Goodput Rate (How many payload bits per second after a valid frame?): %.6f bits/second.\n", (frameState.payloadLength * BITS_IN_ONE_BYTE) / ((double)(experimentEndTime - frameFoundTimeNS) / 1e9));

            // Clean up
            free(samples);
            return EXIT_SUCCESS;
        }

        // If the CRC fails, we need to increment our metric and reset our lock so we can find a new frame.
        failedFrames++;    // For metrics sake 
        memset(&frameState, 0, sizeof(frameState)); // Reset our frame and move on to the next potential candidate.
    }


    return 0;
}

