/*
 * Copyright (c) 2007, 2009 Joseph Gaeddert
 * Copyright (c) 2007, 2009 Virginia Polytechnic Institute & State University
 *
 * This file is part of liquid.
 *
 * liquid is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * liquid is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with liquid.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "liquid.internal.h"


// Compute group delay for an FIR filter
//  _h      : filter coefficients array
//  _n      : filter length
//  _fc     : frequency at which delay is evaluated (-0.5 < _fc < 0.5)
float fir_group_delay(float * _h, unsigned int _n, float _fc)
{
    // TODO: validate input

    unsigned int i;
    float complex t0=0.0f;
    float complex t1=0.0f;
    for (i=0; i<_n; i++) {
        t0 += _h[i] * cexpf(_Complex_I*2*M_PI*_fc*i) * i;
        t1 += _h[i] * cexpf(_Complex_I*2*M_PI*_fc*i);
    }

    return crealf(t0/t1);
}

float iir_group_delay(float * _b,
                      unsigned int _nb,
                      float * _a,
                      unsigned int _na,
                      float _fc)
{
    // TODO : validate input

    // compute c = conv(b,fliplr(a))
    //         c(z) = b(z)*a(1/z)*z^(-_na)
    unsigned int nc = _na + _nb - 1;
    float c[nc];
    unsigned int i,j;
    for (i=0; i<nc; i++)
        c[i] = 0.0;

    for (i=0; i<_na; i++) {
        for (j=0; j<_nb; j++) {
            c[i+j] += conjf(_a[_na-i-1])*_b[j];
        }
    }

    float complex t0=0.0f;
    float complex t1=0.0f;
    for (i=0; i<nc; i++) {
        t0 += c[i] * cexpf(_Complex_I*2*M_PI*_fc*i) * i;
        t1 += c[i] * cexpf(_Complex_I*2*M_PI*_fc*i);
    }

    float tol = 1e-5f;
    if (cabsf(t1)<tol)
        return 0.0f;

    return crealf(t0/t1) - (_na - 1);
}
