/*
    Takes a preamble pattern as input (sequence of 1s and 0s),
    calculates the maximum auto correlation for all side lobes (misalignments),
    prints out the greatest value of side lobes.
*/

#define MAX_PREAMBLE_LEN 32

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int autocorrelationAtK(int preamble[], int k, size_t preambleLen){
    int summation = 0;

    // Auto correlation uses a summation function
    for(size_t i = 0; i < preambleLen; i++){
        int x_ik    = preamble[(i + k) % preambleLen];
        int x_i     = preamble[i];

        // summation(x_i * (x_(i+k) % preambleLength))
        summation += (x_i * x_ik);
    }

    return summation;
}

int maxAutocorrelation(int preamble[], size_t preambleLen){
    int maxR = 0; 
    int currR = (MAX_PREAMBLE_LEN * -1); // The minimum autocorrelation is -32 in this case. (all misalignments cause a product of -1)
    // For each k > 0, swap the autocorrelation calculated at the side lobe R_c(K) if
    //      it is greater than the current maximum auto correlation
    for(int k = 1; k < preambleLen; k++){
        currR = autocorrelationAtK(preamble, k, preambleLen);

        if(abs(currR) >= abs(maxR)){
            maxR = abs(currR);
        }
    }
    return maxR;
}

int main (int argc, char **argv){
    if(argc < 2 || argc >= 3){
        printf("Usage: ./cyclicAuto [sequence of 1s and 0s (',' delimited)]\n");
        exit(1);
    }

    // Translate the ',' delimited preamble into array form:
    char *preambleChar = argv[1];
    int preambleLen = 0;
    int preamble[MAX_PREAMBLE_LEN];
    char *token = strtok(preambleChar, ",");

    // Use strtok() to run through the command line argument
    while(token != NULL){
        if(preambleLen >= MAX_PREAMBLE_LEN){
            printf("Reached the max preamble length. Try another pattern!\n");
            return 1;
        }
        int currBit = atoi(token);

        // This is where we convert our bits to their bipolar values:
        // x_i = {0 --> -1}
        //       {1 --> +1}
        if(currBit == 1){
            preamble[preambleLen++] = currBit;
        } else if(currBit == 0){
            preamble[preambleLen++] = -1;
        } else {
            printf("the sequence provided includes a non-0 or non-1 value!\n");
            exit(1);
        }
        
        token = strtok(NULL, ",");  // Go to the nxt token
    }

    // For testing purposes
    for(int i = 0; i < preambleLen; i++){
        printf("    Current Index: [%d] = %d\n", i, preamble[i]);
    }
    
    int maximumAutoCorrelation = maxAutocorrelation(preamble, preambleLen);
    printf("Maximum side lobe auto correlation: %d\n", maximumAutoCorrelation);

    return 0;
}