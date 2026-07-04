/*
    I'm trying to learn how a CRC operates. This is my
    unoptimized version of the algorithm behind a basic CRC
    using polynomial G(x) = x^3 + x + 1.

*/

#include <stdint.h>
#include <stdio.h>

// Take some binary string as input, return its length by calculating how many right shifts we can do.
// I could probably optimize this by using a log() function.
//  At the cost of reliability (floating-point precision), using log() would increase performance,
//  especially for larger binary numbers!
size_t binaryLength(uint32_t n) {
    size_t length = 0;

    // Just count the number of bit shifts
    while (n > 0) {
        n >>= 1; // Right shift by 1 bit
        length++;
    }

    return length;
}

// Peform one iteration of a long division in GF(2). 
// If the length of the dividend > length of the divsionr,
//      Align the leading 1s within the dividend and divisor (left shifts)
//      XOR the bits
// Return -1 if we cannot perform the XOR operation
int xorShiftHIGH(uint32_t dividend, uint32_t divisor, size_t lenDividend, size_t lenDivisor){
    int XORresult = -1;

    // Only perfrom the XOR operation if we can fit our dividend into our divisor.
    if(lenDividend >= lenDivisor){
        // Align the dividend's leading 1 with the divisors leading 1.
        divisor <<= (lenDividend - lenDivisor);

        // Obtain the XOR result
        XORresult = dividend ^= divisor;
    }
    
    return XORresult; 
}

// Perform bit long division (XORSHIFTS), returning the remainder obtained.
uint32_t bitLongDivision(uint32_t dividend, uint32_t divisor){
    if(divisor == 0){
        printf("Cannot divide by 0");
        return 0;
    }

    size_t dividendLen = binaryLength(dividend);
    size_t divisorLen = binaryLength(divisor);
    uint32_t currRemainder = dividend;

    while((dividend = xorShiftHIGH(dividend, divisor, dividendLen, divisorLen)) != -1){
        // Find the next length of the current result.
        currRemainder = dividend;
        dividendLen = binaryLength(dividend);
        printf("XOR'd value: %u\n", dividend);
    }
    return currRemainder;

}

int main(int argc, char **argv){
    // CRC Algorithm:
    /*
        1) Convert the given word as well as the chosen G(x) polynomial
            into base 2.
            Save the length of the G(x) polynomial. Let's call it X.
            Append (X - 1) zeroes onto our original word
        2) Take the highest X bits of our given word and perform the XOR operation on them.
        3) Save the result, and continue performing (2) on each of these results until we can no longer fit our polynomial
            into said result.
            Problem: We need to find where our leading 1 is at each iteration of (2).
            Solution:   Just take the binary length of our current result, then subtract the length of the polynomial.
                        This gives us the position of the leading 1, but also provides us with a value to know whether
                        we can perform an XOR, or if we are done with our long division!
        4) Once we are finished with (3), we will finally have our remainder. Add this to our initial (appended) word,
            so that we can check that the polynomial divides into it evenly.
    */
    uint32_t value = 0b1;     // Random word
    uint32_t poly = 0b1011;       // G(x) = x^3 + x + 1
    uint32_t currWord = (value << (binaryLength(poly)-1));  // Append lengthPoly - 1 0's to our message
    uint32_t valueAppended = currWord;
    uint32_t remainder = 0;
    printf("Initial Value of Word %d\n", currWord);
    // Only perform the long division if our word has value larger than poly. 
    remainder = (binaryLength(currWord) >= binaryLength(poly)) ? bitLongDivision(currWord, poly) 
                                : value;

    printf("Remainder of our initial long division: %u\n", remainder);

    // Now add/subtract this remainder back to our original word.
    // These two operations are identical to XOR in GF(2), which is the field we are operating within.
    valueAppended ^= remainder;
    printf("valueAppended after XOR'ing our remainder: %u\n", valueAppended);

    // Final check to see if our initial remainder XOR'd against our initial message (with appended 0s)
    //  now evenly divides our poly
    remainder = bitLongDivision(valueAppended, poly);
    printf("Final remainder should be 0:\n");
    printf("    Final Remainder: %u\n", remainder);

    return 0;
}