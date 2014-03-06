/* FastAtan2.h, copyright (c) 2014 Robert Xiao

This module contains a very fast table-based implementation of atan2 for 16-bit
operands. The computation itself contains no floating-point operations (assuming
that your compiler properly folds the floating-point constants into integers).
On my ARM machine (Exynos 5420), this routine is over 16 times faster than
libm's atan2f.
*/

#include <math.h>

/* 14 bits is all we need to achieve an accuracy of +/- 1. */
#define ATAN_BITS 14
#define ATAN_SIZE (1<<ATAN_BITS)
#define ATAN_SCALE -5215.2 // _rescalingFactor

class FastAtan2 {
    uint32_t inv[32769];
    int32_t atan_low[ATAN_SIZE+1];
    int32_t atan_high[ATAN_SIZE+1];

public:
    FastAtan2() {
        for(int i=0; i<=32768; i++) {
            inv[i] = round(32768.0 * 65536.0 / i);
        }

        for(int i=0; i<=ATAN_SIZE; i++) {
            atan_low[i] = atan2f(i, ATAN_SIZE) * ATAN_SCALE;
            atan_high[i] = atan2f(ATAN_SIZE, i) * ATAN_SCALE;
        }
    }

    inline int16_t atan2_16(int16_t y, int16_t x) {
        uint32_t yy, xx;
        int16_t add;
        int16_t ret;
        if(y < 0) {
            if(x < 0) {
                /* quadrant 3 */
                yy = -y;
                xx = -x;
                add = (int16_t)(-M_PI * ATAN_SCALE + 0.5f);
            } else {
                /* quadrant 4 */
                yy = x;
                xx = -y;
                add = (int16_t)(-M_PI/2 * ATAN_SCALE + 0.5f);
            }
        } else {
            if(x < 0) {
                /* quadrant 2 */
                yy = -x;
                xx = y;
                add = (int16_t)(M_PI/2 * ATAN_SCALE + 0.5f);
            } else {
                /* quadrant 1 */
                yy = y;
                xx = x;
                add = 0;
            }
        }

        /* xx, yy are positive and <= 32768 */
        if(yy == xx) {
            ret = (int16_t)(M_PI/4 * ATAN_SCALE + 0.5f);
        } else if(yy > xx) {
            ret = atan_high[(xx * inv[yy]) >> (31 - ATAN_BITS)];
        } else {
            ret = atan_low[(yy * inv[xx]) >> (31 - ATAN_BITS)];
        }

        return ret + add;
    }
};
