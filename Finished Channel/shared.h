#pragma once

#define _GNU_SOURCE     // For CLOCK_MONOTONIC 
#include <errno.h>
#include <getopt.h>
#include <math.h>
#include <sched.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>
#include <unistd.h>
#include <inttypes.h>
#include <bits/types.h>

// Sizes
#define BITS_IN_ONE_BYTE    8U
#define TIME_SLOT_MS        100
#define NS_IN_MS            1000000
#define NS_IN_S             1000000000ULL

// Default Command Line Argument Settings
#define DEFAULT_CPU         2
#define DEFAULT_SLOT_MS     100.0
#define DEFAULT_NICE        10
#define DEFAULT_SUB_SLOTS   8

// Frame Constants. This is the "well-known" structure where
//      offsets are calculated in consumer.c
#define CALIB_BUSY_BITS     8U
#define CALIB_IDLE_BITS     8U
#define CALIB_BITS          (CALIB_BUSY_BITS + CALIB_IDLE_BITS)
#define PREAMBLE_BITS       32U
#define PREFIX_BITS         (PREAMBLE_BITS + CALIB_BITS)
#define LENGTH_BITS         8U
#define CRC_BITS            8U
#define MAX_SAMPLES         4000000

#define CPU_WORK_BATCHES    512
#define MAX_PAYLOAD         255     // 256 - 1 (for '\0')

#define CORRELATION_MIN     0.72
#define SEPARATION_MIN      0.08

// The well-known PREAMBLE pattern. 
/*
    This was probably the most interesting part of the design process (Learning the CRC was too fun!) due
    to how much math was involved (Cyclic Autocorrelation is still a bit of a mystery to me). 
    I learnt a beautiful way of using statistics to synchronize two different processes on the same system 
    trying to communicate with noise, in an already noisy system!

    To solve this synchronization problem, we needed to find a way to distinguish an intentional signal from unintentional noise. This means
    things like idle Operating System periods, busy Operating System periods, and background noise need to be accounted for. The solution 
    found used two main tools in order to have a correlation formula spit out a reliable enough number that could be used to detect the
    start of an intentional preamble:
        1) A Balanced Preamble.
            leading to the idea that we need a small pattern.
        2) A Preamble with (queue the mysterious music) **Low-Cyclic Autocorrelation**. 

    Firstly, a balanced preamble gives us a way of removing any CONSTANT noise observed during an entire N-bit measurement period. 

    This means that, during an N-bit long measurement period from the consumer, so long as the preamble pattern has a balanced number of 1s and 0s 
    we can remove the constant noise from our preamble correlation calculations! Here's the math behind it:
        1. Map 1 --> +1 and 0 --> -1. This is known as converting ordinary bits into bipolar values. We want the summation of x_i to equal
            0 in the event that we have a balanced number of 0s and 1s. Ordinary bits would have the summation = # of 1s. Converting to 
            bipolar values ensures that a balanced summation will equal 0.
        2. Let the i'th preamble bit be x_i, and the consumer's measured work count during i be y_i.
            Thus, the correlation score at alignment k (Where we start calculating our correlation in the preamble. This matters
            more so for the misalignment problem, but is still important with our mental model behind a balanced preamble!) is:
                C(k) = summation_i(x_i * y_(i+k))

        3. Now, x_i is an element of {-1, 1}.                      These are the bipolar values obtained in (1)
                y_(i+k) = s_(i+k) + c + r_(i+k);        Where:  
                                                        - s_i is the signal produced by the producer 
                                                        - c is the "constant" noise
                                                        - r_(i+k) is the random noise

        4. So,  C(k)    = summation(x_i * y_(i+k))
                        = summation(x_i * (s_(i+k) + c + r_(i+k)))
                        = summation(x_i *s_(i+k)) + summation(x_i * c) + summation(x_i * r_(i+k))
            In plain english:
                Correlation at Alignment K = Desired Signal + Constant Noise + Random Noise
            
            Isolating for Constant Noise:
                Constant Noise  = summation(x_i * c)
                                = c * summation(x_i)
                                = c * (0)               * If we have a balanced preamble, summation(x_i) should have each bipolar value
                                                            cancel out (for each 0 (bipolar value = -1) we have an accompanying 1 (bipolar = +1))
                                = 0
            So we can completely disregard any baseline noise that remains completely constant during our N-Bit pattern!
            Thus, if we want to increase our chances of having constant noise be irrelevant, we should choose a small value of N.
                POTENTIAL RESEARCH: Analyze differing values of N to decide which one allows us to reliably remove a high value of
                                    constant noise when trying to recognize our synchronization pattern.

    
    Thus balancing 1s and 0s in our preamble removes the common "constant" offset without 
    weakening the correctly aligned signal peak, wherever it may be..
    ASIDE: Exact balance is also not an absolute law. For example, CCSDS specifies 0x1ACFFC1D as a widely used 32-bit Attached Sync Marker; 
    it contains 19 ones and 13 zeros. 
    That illustrates that a pattern may sacrifice perfect balance to improve other properties within a particular receiver and framing system.
        https://ccsds.org/Pubs/131x0b5.pdf

    Unfortunately, balancing is not enough! Consider the following preamble:
        [0, 1, 0, 1, 0, 1, 0, 1]

        This is balanced, but it's incredibly easy to reproduce! Just shift it left twice. Or Right twice. Or any even number of times. 
        It's the same pattern! Thus, if we choose the incorrectly balanced preamble pattern, our consumer may start listening to the producer
        halfway through its preamble emission, and incorrectly assume that it's the start!
        
        Thus, welcome our next tool: Cyclic Autocorrelation

    Cyclic Autocorrelation:
        Keeping in tune with the bipolar representation of 1s and 0s, let's introduce Cyclic Autocorrelation at shift k:
        R_c(k) = summation_i(x_i * x_((i+k) mod n))           where 
                                                            - n = |PREAMBLE|
                                                            - i = current observed bit
                                                            - k = current alignment
                

        Notice how at k = 0, we have each x_i = x_(i+k). As each x_i = {-1, 1}, if k = 0, we can rewrite R_c(0) as:
            R_c(0)  = summation_i(x_i * x_i)
                    = summation_i(x^2)
                    = summation_i(1)            (As each x_i = {-1, 1}, (x^2) = 1)
                    = (Upper bound of i + 1) * 1

        This means that, at the correct alignment (0, also known as the "main lobe"), we can observe three things:
            1) our inner term of the summation operation is always 1
            2) R_c(0) = (Upper bound of i) * 1 = length of the preamble message.
            3) R_c(0) represents the largest Auto Correlation we can obtain!

        Further, at every other alignment, k > 0 (we call such alignments as side lobes so long as k < n, the length of our preamble), 
            R_c(k) <= R_c(0).
            This is because if R_c(k) == R_c(0), then after k shifts we have generated the same pattern as our main lobe!

            Thus we want to define some N-bit preamble pattern that has each side lobe STRICTLY less than the ABSOLUTE value of R_c(0). 
            Will this be our only goal? This could be a point of research. And I'm not here to research Cyclic Autocorrelation,
                I just want to use it for application purposes! Thus I'll just assume I need some small enough maximum(|R_c(k)|), say
                32/2. 
            POTENTIAL RESEARCH QUESTION: What max(|R_c(k)|), where |R_c(k)| < R_c(0), is required to provide a strong enough pattern candidate
                to reliably (95%) detect a preamble pattern among the noise within operating system (with a set list of constraints)
                while utilizing Scheduler Contention as the medium to detect said preamble pattern?

            Because this is not a research question. I asked ChatGPT to provide me with a 32-bit preamble pattern that is balanced
                and also has a low cyclic autocorrelation value for each side lobe. This value was verified by the C program I coded
                in "CyclicAutocorrelationChecker.c"


    Thus, picking a sequence of 1s and 0s that would provide reliable synchronization is actually a difficult problem.
        This pattern needs to:
            1) Be easy to detect.
            2) Hard to "misalign"
            3) Be robust to timing-channel noise
        Thus, we need to answer questions such as:
            1) What do signals (work counts) NORMALLY look like? We haven't calibrated anything yet!
            2) How easy is it to reproduce this pattern?

    REQUIREMENTS:
    1) Must be balanced (1s and 0s)
    2) Must be hard to "accidentally" interpret among a busy channel or the subsequent phases within the producer frames!
        - The pattern must possess a "Low Cyclic Autocorrelation". Validate with CyclicAutocorrelationChecker.c inside /Helpers 

*/
extern const uint8_t PREAMBLE[PREAMBLE_BITS];

// An arbitrarily large uint64_t that will have a bunch of
//  XOR + shift operations occur so that a checksum can
//  accompany the CPU-intensive workload measured
// Used in measureSlot().
extern volatile uint64_t work_sink;


// Pin the current process to the CPU specified as a parameter.
//  THIS IS A CONSTRAINT THAT I AM PLACING ON MY EXPERIMENT.
//  I will eventually try to create a solution that relaxes this restraint. 
void pinToCpu(size_t cpu);

// A death function. What a concept. 
void die(const char *message);

// Provide the current time in nano seconds.
// I did not make this. This was stripped from my COMP3430 class.
uint64_t monotonic_ns(void);

// Returns a timespec data structure (time.h) with the
//  nanoseconds requested.
struct timespec nanosecondsToTimespec(uint64_t nanoseconds);

/*
    ===========================================
    === Cylic Redunancy Check (CRC-8 CCITT) ===
    ===========================================
*/
/*
    Learning about CRC was awesome! With my goal of this channel being reliability, it was important to me
    to have some sort of final check of the data. A basic checksum could be used here as this is
    a basic demonstration of a Covert Timing Channel, but I wanted to learn a better form of relability
    for signals.

    I asked ChatGPT to give me an idea (with explicit mention of not providing me how it works, I wanted
    to learn that myself). It told me to check out CRC-8 (Polynomial 0x07). I'll admit after reading what chat spit out,
    I wish it didn't tell me to use the CRC-8 generator polynomial! Finding the correct polynomial seems like a tough
    task. So instead, here's what reasoning I could conjure about CRC-8:

    CRC interprets a sequence of bits as a polynomial whose coefficients are either
    1s or 0s. CRC also uses GF(2) when considering its arithmetic. This idea of a field was
    new to me, but boolean logic was not, so I took it as an extension of boolean logic and
    it became a new way of viewing 1s and 0s!

    Choosing the correct polynomial is important as it determines what sort of errors
    we can detect. Namely, if a corrupted frame can have its error pattern evenly divided by
    the generator polynomial then it will be accepted. Thus we have to choose
    a polynomial such that it does not divide into the error pattern evenly.

    I then went and implemented my own basic version of CRC with the polynomial G(x) = x^3 + x + 1.
    It was incredibly slow and inefficient.
        - I had to compare lengths of binary values quite often, which I calculated by counting how many
            times I could right bitshift. I could've used the log() function. I could have. Cool.
        - I was holding the entire payload message twice in memory. 
        - I was also running through the entire long division process, having to align the leading 1s of the polynomial and the current dividend
            at each iteration of this division.

    An improvement on this process is to instead utilize a "Shift Register-CRC", which uses a "register"
    that holds |polynomial| - 1 bits. For each bit:
        Remember the register’s highest bit.
        Shift the register left.
        Shift the next message bit into the lowest register bit.
        If the old highest bit was 1, XOR the polynomial's lower bits into the register.

        This improves the CRC process by now having:
            Time: O(|message|):             We simply run through the entirety of a message once!
                - We no longer need to have a bunch of length calculations.
                - We no longer need to align any leading 1s
            Space: O(|polynomial| - 1):     Our register only needs one less bit than the polynomial has!
                - This seems like a drastic increase if our message is huge. 

        Not bad. 
*/
// Here's our "shift-register CRC operation"
uint8_t shiftRegisterCRC8(uint8_t remainder, uint8_t value);

/*
    The "protected region" that we would like to calculate our CRC over are the
        bytes sent by the producer corresponding to the length and payload phases
        of a given frame.

    Having the length byte apart of our crc produces greater reliability than 
        simply having our protected region be the payload!

    Output: The remainder produced after long division (XOR's) of the
        polynomial G(x) = x^8 + x^2 + x + 1 into the dividend:
        "length byte, payload bytes"
*/
uint8_t frameCRC(const uint8_t *payload, size_t length);