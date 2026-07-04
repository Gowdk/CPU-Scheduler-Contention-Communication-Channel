#pragma once

#include "shared.h"

typedef struct frameState {
    bool found;

    size_t frameStartSlot;

    double busyMedian;
    double idleMedian;
    double threshold;
    double correlation;

    bool lengthKnown;
    uint8_t payloadLength;
} FrameState;

void usage(char *argv0);

/*
    These are constraints for our demonstration!
    1) Pinning to a CPU means we don't have to worry about our processes being context switched
        to a different CPU, meaning we can continually listen to for what our producer is trying to tell us.
        I would like to relax this constraint after this is functional.
    2) Setting the priority of our process makes it easier for other processes to lower our work counts.
        This is also something I'd like to relax! 
*/
void setConstraints(int cpu, int niceVal);

/*
    Take the N-2 inner sub slots.
    Removing the 2 outer sub slots reduces effects from:
       1) late wakeups
       2) transitions between symbols
       3) Scheduling delays near slot boundaries
  
    RETURNS: Median of the N - 2 interior sub slots.
*/
double divideSlot(const uint64_t *samples, size_t slotStart, size_t numSubSlots);

/*
    Problem:
        - We need this to be relatively inexpensive. 
        - We want to measure iterations, and nothing else.
        - monotonic_ns() is expensive! If used to track how far along we are from our timeWindow,
            we will measure how long it takes to run monotonic_ns() as well as CPU utilization!
            We do not care how well clock_gettime() performs!

    Solution:
        - Run monotonic_ns() after some fixed interval, reducing the clock calls.
        - Have our number of iterations be easy to track across processes, thus we want them to be:
            1) Inexpensive
            2) Entirely CPU-based.
            3) Difficult for the compiler to replace with a single constant expression, i.e:
                for(int i = 0; i < FIXED_INTERVAL; i++){}
                ^^^
                    This is a single constant expression of length FIXED_INTERVAL. The compiler can just ignore.

                ALSO:
                for(int i = 0; i < FIXED_INTERVAL; i++){iterations++;}
                ^^^
                    The compiler can instead optimize to just do "iterations += FIXED_INTERVAL"

            4) Stateful (leading into point 5...)
            5) dependent from one iteration to the next (states must be apart of some dependency chain).
                state1 --> state2 --> state3 --> ... --> state500
                state500 cannot be achieved without first knowing state499


    Learning Outcome:
        - When performing experiments on computer systems, it is important to code your implementations
            with the caution that the computer system is a gigantic, unimaginably big monster that 
            we must do our best to subdue in small, tiny chunks. These chunks must be created by paying 
            close attention to the mechanisms behind what you're trying to implement.
            
            In this case, we want to implement a function that tracks how many "things" can occur in
            some fixed timeWindow. We need these "things" to be quick + inexpensive so that we can 
            estimate our final metrics with as high of a precision as possible, knowing that this metric
            will also play a part in solving a larger problem (else our margin-of-error will grow exponentially!)

            So what impacts our implementation/solution?
                1) The CPU driving the instructions
                2) monotonic_ns() tells us when to stop
                3) The OS tries to optimize where it can
                4) The interval/timeWindow must be big enough to notice a difference between busy/sleep loops

            What constraints shall we put in place to ensure our solution has a small margin-of-error?
                1) Bound the iteration mechanism to a single CPU.
                2) Call monotonic_ns() at fixed sized intervals
                3) Make sure the iteration mechanism actually utilizes the CPU in some way (work with bits!)
                4) Use a checksum on the state of the iteration mechanism so that the CPU does not optimize it away.
                    i.e print it somewhere.
                5) Play around with different variable sizes to achieve proper output for busy/sleep loops.
*/
size_t measureSlot(uint64_t endOfSlotNS);

/*
    Calculate the Normalized Cross-Correlation between the observed bits (samples) and the expected preamble
*/ 
double preambleCorrelation(const uint64_t *samples, size_t frameStart, size_t numSubSlots);

/*
    Similar to a try_lock for a mutex lock, here we try to lock a frame state!
    Needed as we are polling for samples. This serves as our first checkstop!

    Uses preambleCorrelation() between the observed preamble and the well-known preamble
    If below CORRELATION_MIN, go back to listening for slots
    Recall that we want to keep CORRELATION_MIN high enough for reliability's sake.

    Potential Upgrade:
        Is it at all possible to continually test large quantities of samples for a valid preamble,
        and when a significant correlation is calculated, go on to skip the preamble bits and go straight
        to the calibration phase? If I'm thinking about synchronization, this sounds like more complexity. 
        Is the trade off of complexity for performance worth it? Not sure.
*/
bool tryLock(const uint64_t *samples, size_t frameStart, FrameState *frameState, size_t numSubSlots);

/*
    Decoding functions.

    Provided with the "samples", start at the frameStart of the currently locked FrameState data structure,
    index the correct slot, then decode the bit/byte at that location based on the threshold/numSubSlots values provided.
*/
uint8_t decodeBit(const uint64_t *samples, size_t frameStart, size_t slotIndex, double threshold, size_t numSubSlots);
uint8_t decodeByte(const uint64_t *samples, size_t frameStart, size_t firstSlot, double threshold, size_t numSubSlots);
