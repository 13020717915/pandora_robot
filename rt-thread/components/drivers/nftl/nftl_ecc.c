/*
 * File      : nftl_ecc.c
 * COPYRIGHT (C) 2012-2014, Shanghai Real-Thread Electronic Technology Co.,Ltd
 *
 * Change Logs:
 * Date           Author       Notes
 * 2014-01-21     Bernard      Add COPYRIGHT file header.
 */

#include <rtthread.h>
#include <rtdevice.h>

#define Hamming_ERROR_SINGLEBIT         1
#define Hamming_ERROR_ECC               2
#define Hamming_ERROR_MULTIPLEBITS      3

static rt_uint8_t CountBitsInByte(rt_uint8_t byte)
{
    rt_uint8_t count = 0;

    while (byte > 0)
    {
        if (byte & 1)
        {
            count++;
        }
        byte >>= 1;
    }

    return count;
}

static void Compute256(const rt_uint8_t *data, rt_uint8_t *code)
{
    rt_uint32_t i;
    rt_uint8_t columnSum = 0;
    rt_uint8_t evenLineCode = 0;
    rt_uint8_t oddLineCode = 0;
    rt_uint8_t evenColumnCode = 0;
    rt_uint8_t oddColumnCode = 0;

    // Xor all bytes together to get the column sum;
    // At the same time, calculate the even and odd line codes
    for (i = 0; i < 256; i++)
    {
        columnSum ^= data[i];

        // If the xor sum of the byte is 0, then this byte has no incidence on
        // the computed code; so check if the sum is 1.
        if ((CountBitsInByte(data[i]) & 1) == 1)
        {
            // Parity groups are formed by forcing a particular index bit to 0
            // (even) or 1 (odd).
            // Example on one byte:
            //
            // bits (dec)  7   6   5   4   3   2   1   0
            //      (bin) 111 110 101 100 011 010 001 000
            //                            '---'---'---'----------.
            //                                                   |
            // groups P4' ooooooooooooooo eeeeeeeeeeeeeee P4     |
            //        P2' ooooooo eeeeeee ooooooo eeeeeee P2     |
            //        P1' ooo eee ooo eee ooo eee ooo eee P1     |
            //                                                   |
            // We can see that:                                  |
            //  - P4  -> bit 2 of index is 0 --------------------'
            //  - P4' -> bit 2 of index is 1.
            //  - P2  -> bit 1 of index if 0.
            //  - etc...
            // We deduce that a bit position has an impact on all even Px if
            // the log2(x)nth bit of its index is 0
            //     ex: log2(4) = 2, bit2 of the index must be 0 (-> 0 1 2 3)
            // and on all odd Px' if the log2(x)nth bit of its index is 1
            //     ex: log2(2) = 1, bit1 of the index must be 1 (-> 0 1 4 5)
            //
            // As such, we calculate all the possible Px and Px' values at the
            // same time in two variables, evenLineCode and oddLineCode, such as
            //     evenLineCode bits: P128  P64  P32  P16  P8  P4  P2  P1
            //     oddLineCode  bits: P128' P64' P32' P16' P8' P4' P2' P1'
            //
            evenLineCode ^= (255 - i);
            oddLineCode ^= i;
        }
    }

    // At this point, we have the line parities, and the column sum. First, We
    // must caculate the parity group values on the column sum.
    for (i = 0; i < 8; i++)
    {
        if (columnSum & 1)
        {
            evenColumnCode ^= (7 - i);
            oddColumnCode ^= i;
        }
        columnSum >>= 1;
    }

    // Now, we must interleave the parity values, to obtain the following layout:
    // Code[0] = Line1
    // Code[1] = Line2
    // Code[2] = Column
    // Line = Px' Px P(x-1)- P(x-1) ...
    // Column = P4' P4 P2' P2 P1' P1 PadBit PadBit
    code[0] = 0;
    code[1] = 0;
    code[2] = 0;

    for (i = 0; i < 4; i++)
    {
        code[0] <<= 2;
        code[1] <<= 2;
        code[2] <<= 2;

        // Line 1
        if ((oddLineCode & 0x80) != 0)
        {
            code[0] |= 2;
        }

        if ((evenLineCode & 0x80) != 0)
        {
            code[0] |= 1;
        }

        // Line 2
        if ((oddLineCode & 0x08) != 0)
        {
            code[1] |= 2;
        }

        if ((evenLineCode & 0x08) != 0)
        {
            code[1] |= 1;
        }

        // Column
        if ((oddColumnCode & 0x04) != 0)
        {
            code[2] |= 2;
        }

        if ((evenColumnCode & 0x04) != 0)
        {
            code[2] |= 1;
        }

        oddLineCode <<= 1;
        evenLineCode <<= 1;
        oddColumnCode <<= 1;
        evenColumnCode <<= 1;
    }

    // Invert codes (linux compatibility)
    code[0] = (~(rt_uint32_t)code[0]);
    code[1] = (~(rt_uint32_t)code[1]);
    code[2] = (~(rt_uint32_t)code[2]);
}

void nftl_ecc_compute256(const rt_uint8_t *pucData, rt_uint32_t dwSize, rt_uint8_t *puCode)
{
    while (dwSize > 0)
    {
        Compute256(pucData, puCode) ;

        pucData += 256;
        puCode += 3;
        dwSize -= 256;
    }
}

static unsigned char CountBitsInCode256(unsigned char *code)
{
    return CountBitsInByte(code[0])
           + CountBitsInByte(code[1])
           + CountBitsInByte(code[2]);
}

static unsigned char Verify256(unsigned char *data, const unsigned char *originalCode)
{
    // Calculate new code
    unsigned char computedCode[3];
    unsigned char correctionCode[3] = {0};
    Compute256(data, computedCode);

    // Xor both codes together
    correctionCode[0] = computedCode[0] ^ originalCode[0];
    correctionCode[1] = computedCode[1] ^ originalCode[1];
    correctionCode[2] = computedCode[2] ^ originalCode[2];

    // If all bytes are 0, there is no error
    if ((correctionCode[0] == 0)
            && (correctionCode[1] == 0)
            && (correctionCode[2] == 0))
    {
        return 0;
    }

    // If there is a single bit error, there are 11 bits set to 1
    if (CountBitsInCode256(correctionCode) == 11)
    {
        rt_uint8_t byte, bit;

        // Get byte and bit indexes
        byte = correctionCode[0] & 0x80;
        byte |= (correctionCode[0] << 1) & 0x40;
        byte |= (correctionCode[0] << 2) & 0x20;
        byte |= (correctionCode[0] << 3) & 0x10;

        byte |= (correctionCode[1] >> 4) & 0x08;
        byte |= (correctionCode[1] >> 3) & 0x04;
        byte |= (correctionCode[1] >> 2) & 0x02;
        byte |= (correctionCode[1] >> 1) & 0x01;

        bit = (correctionCode[2] >> 5) & 0x04;
        bit |= (correctionCode[2] >> 4) & 0x02;
        bit |= (correctionCode[2] >> 3) & 0x01;

        data[byte] ^= (1 << bit);
        return Hamming_ERROR_SINGLEBIT;
    }

    // Check if ECC has been corrupted
    if (CountBitsInCode256(correctionCode) == 1)
    {
        return Hamming_ERROR_ECC;
    }
    // Otherwise, this is a multi-bit error
    else
    {
        return Hamming_ERROR_MULTIPLEBITS;
    }
}

int nftl_ecc_verify256(rt_uint8_t *data, rt_uint32_t size, const rt_uint8_t *code)
{
    int error;
    int result = 0;

    while (size > 0)
    {
        error = Verify256(data, code);
        if (error == Hamming_ERROR_SINGLEBIT)
        {
            result = Hamming_ERROR_SINGLEBIT;
        }
        else if (error)
        {
            return error;
        }

        data += 256;
        code += 3;
        size -= 256;
    }

    if (result == Hamming_ERROR_SINGLEBIT)
        result = -RT_MTD_EECC_CORRECT;
    if (result == 0)
        result = RT_MTD_EOK;

    return result;
}

