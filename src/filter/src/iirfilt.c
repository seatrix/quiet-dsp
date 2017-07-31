/*
 * Copyright (c) 2007 - 2015 Joseph Gaeddert
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

//
// iirfilt : Infinite impulse response filter
//
// References:
//  [Pintelon:1990] Rik Pintelon and Johan Schoukens, "Real-Time
//      Integration and Differentiation of Analog Signals by Means of
//      Digital Filtering," IEEE Transactions on Instrumentation and
//      Measurement, vol 39 no. 6, December 1990.
//

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// defined:
//  IIRFILT()       name-mangling macro
//  TO              output type
//  TC              coefficients type
//  TI              input type
//  WINDOW()        window macro
//  DOTPROD()       dotprod macro
//  PRINTVAL()      print macro

// use structured dot product? 0:no, 1:yes
#define LIQUID_IIRFILT_USE_DOTPROD   (0)

// filter structure type
enum type {
    IIRFILT_TYPE_NORM=0,
    IIRFILT_TYPE_SOS
};

struct IIRFILT(_s) {
    TC * b;             // numerator (feed-forward coefficients)
    TC * a;             // denominator (feed-back coefficients)
    TI * v;             // internal filter state (buffer)
    unsigned int n;     // filter length (order+1)

    unsigned int nb;    // numerator length
    unsigned int na;    // denominator length

    enum type type;

#if LIQUID_IIRFILT_USE_DOTPROD
    DOTPROD() dpb;      // numerator dot product
    DOTPROD() dpa;      // denominator dot product
#endif

    // second-order sections 
    IIRFILTSOS() * qsos;    // second-order sections filters
    unsigned int nsos;      // number of second-order sections
};

// create iirfilt (infinite impulse response filter) object
//  _b      :   numerator, feed-forward coefficients [size: _nb x 1]
//  _nb     :   length of numerator
//  _a      :   denominator, feed-back coefficients [size: _na x 1]
//  _na     :   length of denominator
IIRFILT() IIRFILT(_create)(TC *         _b,
                           unsigned int _nb,
                           TC *         _a,
                           unsigned int _na)
{
    // validate input
    if (_nb == 0) {
        fprintf(stderr,"error: iirfilt_%s_create(), numerator length cannot be zero\n", EXTENSION_FULL);
        exit(1);
    } else if (_na == 0) {
        fprintf(stderr,"error: iirfilt_%s_create(), denominator length cannot be zero\n", EXTENSION_FULL);
        exit(1);
    }

    // create structure and initialize
    IIRFILT() q = (IIRFILT()) malloc(sizeof(struct IIRFILT(_s)));
    q->nb = _nb;
    q->na = _na;
    q->n = (q->na > q->nb) ? q->na : q->nb;
    q->type = IIRFILT_TYPE_NORM;

    // allocate memory for numerator, denominator
    q->a = (TC *) malloc((q->na)*sizeof(TC));
    q->b = (TC *) malloc((q->nb)*sizeof(TC));

    // normalize coefficients to _a[0]
    TC a0 = _a[0];

    unsigned int i;
#if 0
    // read values in reverse order
    for (i=0; i<q->nb; i++)
        q->b[i] = _b[q->nb - i - 1];

    for (i=0; i<q->na; i++)
        q->a[i] = _a[q->na - i - 1];
#else
    for (i=0; i<q->nb; i++)
        q->b[i] = _b[i] / a0;

    for (i=0; i<q->na; i++)
        q->a[i] = _a[i] / a0;
#endif

    // create buffer and initialize
    q->v = (TI *) malloc((q->n)*sizeof(TI));

#if LIQUID_IIRFILT_USE_DOTPROD
    q->dpa = DOTPROD(_create)(q->a+1, q->na-1);
    q->dpb = DOTPROD(_create)(q->b,   q->nb);
#endif

    // reset internal state
    IIRFILT(_reset)(q);
    
    // return iirfilt object
    return q;
}

// create iirfilt (infinite impulse response filter) object based
// on second-order sections form
//  _B      :   numerator, feed-forward coefficients [size: _nsos x 3]
//  _A      :   denominator, feed-back coefficients  [size: _nsos x 3]
//  _nsos   :   number of second-order sections
//
// NOTE: The number of second-order sections can be computed from the
// filter's order, n, as such:
//   r = n % 2
//   L = (n-r)/2
//   nsos = L+r
IIRFILT() IIRFILT(_create_sos)(TC *         _B,
                               TC *         _A,
                               unsigned int _nsos)
{
    // validate input
    if (_nsos == 0) {
        fprintf(stderr,"error: iirfilt_%s_create_sos(), filter must have at least one 2nd-order section\n", EXTENSION_FULL);
        exit(1);
    }

    // create structure and initialize
    IIRFILT() q = (IIRFILT()) malloc(sizeof(struct IIRFILT(_s)));
    q->type = IIRFILT_TYPE_SOS;
    q->nsos = _nsos;
    q->qsos = (IIRFILTSOS()*) malloc( (q->nsos)*sizeof(IIRFILTSOS()) );
    q->n = _nsos * 2;

    // create coefficients array and copy over
    q->b = (TC *) malloc(3*(q->nsos)*sizeof(TC));
    q->a = (TC *) malloc(3*(q->nsos)*sizeof(TC));
    memmove(q->b, _B, 3*(q->nsos)*sizeof(TC));
    memmove(q->a, _A, 3*(q->nsos)*sizeof(TC));

    TC at[3];
    TC bt[3];
    unsigned int i,k;
    for (i=0; i<q->nsos; i++) {
        for (k=0; k<3; k++) {
            at[k] = q->a[3*i+k];
            bt[k] = q->b[3*i+k];
        }
        q->qsos[i] = IIRFILTSOS(_create)(bt,at);
        //q->qsos[i] = IIRFILT(_create)(q->b+3*i,3,q->a+3*i,3);
    }
    return q;
}

// create iirfilt (infinite impulse response filter) object based
// on prototype
//  _ftype      :   filter type (e.g. LIQUID_IIRDES_BUTTER)
//  _btype      :   band type (e.g. LIQUID_IIRDES_BANDPASS)
//  _format     :   coefficients format (e.g. LIQUID_IIRDES_SOS)
//  _order      :   filter order
//  _fc         :   low-pass prototype cut-off frequency
//  _f0         :   center frequency (band-pass, band-stop)
//  _Ap         :   pass-band ripple in dB
//  _As         :   stop-band ripple in dB
IIRFILT() IIRFILT(_create_prototype)(liquid_iirdes_filtertype _ftype,
                                     liquid_iirdes_bandtype   _btype,
                                     liquid_iirdes_format     _format,
                                     unsigned int _order,
                                     float _fc,
                                     float _f0,
                                     float _Ap,
                                     float _As)
{
    // derived values : compute filter length
    unsigned int N = _order; // effective order
    // filter order effectively doubles for band-pass, band-stop
    // filters due to doubling the number of poles and zeros as
    // a result of filter transformation
    if (_btype == LIQUID_IIRDES_BANDPASS ||
        _btype == LIQUID_IIRDES_BANDSTOP)
    {
        N *= 2;
    }
    unsigned int r = N%2;       // odd/even order
    unsigned int L = (N-r)/2;   // filter semi-length

    // allocate memory for filter coefficients
    unsigned int h_len = (_format == LIQUID_IIRDES_SOS) ? 3*(L+r) : N+1;
    float *B = (float*)malloc(h_len*sizeof(float));
    float *A = (float*)malloc(h_len*sizeof(float));

    // design filter (compute coefficients)
    liquid_iirdes(_ftype, _btype, _format, _order, _fc, _f0, _Ap, _As, B, A);

    // move coefficients to type-specific arrays (e.g. liquid_float_complex)
    TC *Bc = (TC*)malloc(h_len*sizeof(TC));
    TC *Ac = (TC*)malloc(h_len*sizeof(TC));
    unsigned int i;
    for (i=0; i<h_len; i++) {
        Bc[i] = B[i];
        Ac[i] = A[i];
    }

    // create filter object
    IIRFILT() q = NULL;
    if (_format == LIQUID_IIRDES_SOS)
        q = IIRFILT(_create_sos)(Bc, Ac, L+r);
    else
        q = IIRFILT(_create)(Bc, h_len, Ac, h_len);

    // return filter object
    free(Ac);
    free(Bc);
    free(A);
    free(B);
    return q;
}

// create simplified low-pass Butterworth IIR filter
//  _n      : filter order
//  _fc     : low-pass prototype cut-off frequency
IIRFILT() IIRFILT(_create_lowpass)(
            unsigned int _order,
            float        _fc)
{
    return IIRFILT(_create_prototype)(LIQUID_IIRDES_BUTTER,
                                      LIQUID_IIRDES_LOWPASS,
                                      LIQUID_IIRDES_SOS,
                                      _order,
                                      _fc,
                                      0.0f,      // center
                                      0.1f,      // pass-band ripple
                                      60.0f);    // stop-band attenuation
}

// create 8th-order integrating filter
IIRFILT() IIRFILT(_create_integrator)()
{
    // 
    // integrator digital zeros/poles/gain, [Pintelon:1990] Table II
    //
    // zeros, digital, integrator
    liquid_float_complex zdi[8] = {
        1.175839f * -1.0f,
        3.371020f * cexpf(_Complex_I * (T)M_PI / 180.0f * -125.1125f),
        3.371020f * cexpf(_Complex_I * (T)M_PI / 180.0f *  125.1125f),
        4.549710f * cexpf(_Complex_I * (T)M_PI / 180.0f *  -80.96404f),
        4.549710f * cexpf(_Complex_I * (T)M_PI / 180.0f *   80.96404f),
        5.223966f * cexpf(_Complex_I * (T)M_PI / 180.0f *  -40.09347f),
        5.223966f * cexpf(_Complex_I * (T)M_PI / 180.0f *   40.09347f),
        5.443743f,};
    // poles, digital, integrator
    liquid_float_complex pdi[8] = {
        0.5805235f * -1.0f,
        0.2332021f * cexpf(_Complex_I * (T)M_PI / 180.0f * -114.0968f),
        0.2332021f * cexpf(_Complex_I * (T)M_PI / 180.0f *  114.0968f),
        0.1814755f * cexpf(_Complex_I * (T)M_PI / 180.0f *  -66.33969f),
        0.1814755f * cexpf(_Complex_I * (T)M_PI / 180.0f *   66.33969f),
        0.1641457f * cexpf(_Complex_I * (T)M_PI / 180.0f *  -21.89539f),
        0.1641457f * cexpf(_Complex_I * (T)M_PI / 180.0f *   21.89539f),
        1.0f,};
    // gain, digital, integrator
    liquid_float_complex kdi = -1.89213380759321e-05f;

    // second-order sections
    // allocate 12 values for 4 second-order sections each with
    // 2 roots (order 8), e.g. (1 + r0 z^-1)(1 + r1 z^-1)
    float Bi[12];
    float Ai[12];
    iirdes_dzpk2sosf(zdi, pdi, 8, kdi, Bi, Ai);

    // copy to type-specific array
    TC B[12];
    TC A[12];
    unsigned int i;
    for (i=0; i<12; i++) {
        B[i] = (TC) (Bi[i]);
        A[i] = (TC) (Ai[i]);
    }

    // create and return filter object
    return IIRFILT(_create_sos)(B,A,4);
}

// create 8th-order differentiation filter
IIRFILT() IIRFILT(_create_differentiator)()
{
    // 
    // differentiator digital zeros/poles/gain, [Pintelon:1990] Table IV
    //
    // zeros, digital, differentiator
    liquid_float_complex zdd[8] = {
        1.702575f * -1.0f,
        5.877385f * cexpf(_Complex_I * (T)M_PI / 180.0f * -221.4063f),
        5.877385f * cexpf(_Complex_I * (T)M_PI / 180.0f *  221.4063f),
        4.197421f * cexpf(_Complex_I * (T)M_PI / 180.0f * -144.5972f),
        4.197421f * cexpf(_Complex_I * (T)M_PI / 180.0f *  144.5972f),
        5.350284f * cexpf(_Complex_I * (T)M_PI / 180.0f *  -66.88802f),
        5.350284f * cexpf(_Complex_I * (T)M_PI / 180.0f *   66.88802f),
        1.0f,};
    // poles, digital, differentiator
    liquid_float_complex pdd[8] = {
        0.8476936f * -1.0f,
        0.2990781f * cexpf(_Complex_I * (T)M_PI / 180.0f * -125.5188f),
        0.2990781f * cexpf(_Complex_I * (T)M_PI / 180.0f *  125.5188f),
        0.2232427f * cexpf(_Complex_I * (T)M_PI / 180.0f *  -81.52326f),
        0.2232427f * cexpf(_Complex_I * (T)M_PI / 180.0f *   81.52326f),
        0.1958670f * cexpf(_Complex_I * (T)M_PI / 180.0f *  -40.51510f),
        0.1958670f * cexpf(_Complex_I * (T)M_PI / 180.0f *   40.51510f),
        0.1886088f,};
    // gain, digital, differentiator
    liquid_float_complex kdd = 2.09049284907492e-05f;

    // second-order sections
    // allocate 12 values for 4 second-order sections each with
    // 2 roots (order 8), e.g. (1 + r0 z^-1)(1 + r1 z^-1)
    float Bd[12];
    float Ad[12];
    iirdes_dzpk2sosf(zdd, pdd, 8, kdd, Bd, Ad);

    // copy to type-specific array
    TC B[12];
    TC A[12];
    unsigned int i;
    for (i=0; i<12; i++) {
        B[i] = (TC) (Bd[i]);
        A[i] = (TC) (Ad[i]);
    }

    // create and return filter object
    return IIRFILT(_create_sos)(B,A,4);
}

// create DC-blocking filter
//
//          1 -          z^-1
//  H(z) = ------------------
//          1 - (1-alpha)z^-1
IIRFILT() IIRFILT(_create_dc_blocker)(float _alpha)
{
    // compute DC-blocking filter coefficients
    float bf[2] = {1.0f, -1.0f  };
    float af[2] = {1.0f, -1.0f + _alpha};

    // convert to type-specific array
    TC b[2] = {(TC)bf[0], (TC)bf[1]};
    TC a[2] = {(TC)af[0], (TC)af[1]};
    return IIRFILT(_create)(b,2,a,2);
}

// create phase-locked loop iirfilt object
//  _w      :   filter bandwidth
//  _zeta   :   damping factor (1/sqrt(2) suggested)
//  _K      :   loop gain (1000 suggested)
IIRFILT() IIRFILT(_create_pll)(float _w,
                               float _zeta,
                               float _K)
{
    // validate input
    if (_w <= 0.0f || _w >= 1.0f) {
        fprintf(stderr,"error: iirfilt_%s_create_pll(), bandwidth must be in (0,1)\n", EXTENSION_FULL);
        exit(1);
    } else if (_zeta <= 0.0f || _zeta >= 1.0f) {
        fprintf(stderr,"error: iirfilt_%s_create_pll(), damping factor must be in (0,1)\n", EXTENSION_FULL);
        exit(1);
    } else if (_K <= 0.0f) {
        fprintf(stderr,"error: iirfilt_%s_create_pll(), loop gain must be greater than zero\n", EXTENSION_FULL);
        exit(1);
    }

    // compute loop filter coefficients
    float bf[3];
    float af[3];
    iirdes_pll_active_lag(_w, _zeta, _K, bf, af);

    // copy to type-specific array
    TC b[3];
    TC a[3];
    b[0] = bf[0];   b[1] = bf[1];   b[2] = bf[2];
    a[0] = af[0];   a[1] = af[1];   a[2] = af[2];

    // create and return filter object
    return IIRFILT(_create_sos)(b,a,1);
}

// destroy iirfilt object
void IIRFILT(_destroy)(IIRFILT() _q)
{
#if LIQUID_IIRFILT_USE_DOTPROD
    DOTPROD(_destroy)(_q->dpa);
    DOTPROD(_destroy)(_q->dpb);
#endif
    free(_q->b);
    free(_q->a);
    // if filter is comprised of cascaded second-order sections,
    // delete sub-filters separately
    if (_q->type == IIRFILT_TYPE_SOS) {
        unsigned int i;
        for (i=0; i<_q->nsos; i++)
            IIRFILTSOS(_destroy)(_q->qsos[i]);
        free(_q->qsos);
    } else {
        free(_q->v);
    }

    free(_q);
}

// print iirfilt object internals
void IIRFILT(_print)(IIRFILT() _q)
{
    printf("iir filter [%s]:\n", _q->type == IIRFILT_TYPE_NORM ? "normal" : "sos");
    unsigned int i;

    if (_q->type == IIRFILT_TYPE_SOS) {
        for (i=0; i<_q->nsos; i++)
            IIRFILTSOS(_print)(_q->qsos[i]);
    } else {

        printf("  b :");
        for (i=0; i<_q->nb; i++)
            PRINTVAL_TC(_q->b[i],%12.8f);
        printf("\n");

        printf("  a :");
        for (i=0; i<_q->na; i++)
            PRINTVAL_TC(_q->a[i],%12.8f);
        printf("\n");

#if 0
        printf("  v :");
        for (i=0; i<_q->n; i++)
            PRINTVAL(_q->v[i]);
        printf("\n");
#endif
    }
}

// clear/reset iirfilt object internals
void IIRFILT(_reset)(IIRFILT() _q)
{
    unsigned int i;

    if (_q->type == IIRFILT_TYPE_SOS) {
        // clear second-order sections
        for (i=0; i<_q->nsos; i++) {
            IIRFILTSOS(_reset)(_q->qsos[i]);
        }
    } else {
        // set internal buffer to zero
        for (i=0; i<_q->n; i++)
            _q->v[i] = 0;
    }
}

// execute normal iir filter using traditional numerator/denominator
// form (not second-order sections form)
//  _q      :   iirfilt object
//  _x      :   input sample
//  _y      :   output sample
void IIRFILT(_execute_norm)(IIRFILT() _q,
                            TI _x,
                            TO *_y)
{
    unsigned int i;

    // advance buffer
    for (i=_q->n-1; i>0; i--)
        _q->v[i] = _q->v[i-1];

#if LIQUID_IIRFILT_USE_DOTPROD
    // compute new v
    TI v0;
    DOTPROD(_execute)(_q->dpa, _q->v+1, & v0);
    v0 = _x - v0;
    _q->v[0] = v0;

    // compute new y
    DOTPROD(_execute)(_q->dpb, _q->v, _y);
#else
    // compute new v
    TI v0 = _x;
    for (i=1; i<_q->na; i++)
        v0 -= _q->a[i] * _q->v[i];
    _q->v[0] = v0;

    // compute new y
    TO y0 = 0;
    for (i=0; i<_q->nb; i++)
        y0 += _q->b[i] * _q->v[i];

    // set return value
    *_y = y0;
#endif
}

// execute iir filter using second-order sections form
//  _q      :   iirfilt object
//  _x      :   input sample
//  _y      :   output sample
void IIRFILT(_execute_sos)(IIRFILT() _q,
                           TI        _x,
                           TO *      _y)
{
    TI t0 = _x;     // intermediate input
    TO t1 = 0.;     // intermediate output
    unsigned int i;
    for (i=0; i<_q->nsos; i++) {
        // run each filter separately
        IIRFILTSOS(_execute)(_q->qsos[i], t0, &t1);

        // output for filter n becomes input to filter n+1
        t0 = t1;
    }
    *_y = t1;
}

// execute iir filter, switching to type-specific function
//  _q      :   iirfilt object
//  _x      :   input sample
//  _y      :   output sample
void IIRFILT(_execute)(IIRFILT() _q,
                       TI        _x,
                       TO *      _y)
{
    if (_q->type == IIRFILT_TYPE_NORM)
        IIRFILT(_execute_norm)(_q,_x,_y);
    else
        IIRFILT(_execute_sos)(_q,_x,_y);
}

// execute the filter on a block of input samples; the
// input and output buffers may be the same
//  _q      : filter object
//  _x      : pointer to input array [size: _n x 1]
//  _n      : number of input, output samples
//  _y      : pointer to output array [size: _n x 1]
void IIRFILT(_execute_block)(IIRFILT()    _q,
                             TI *         _x,
                             unsigned int _n,
                             TO *         _y)
{
    unsigned int i;
    for (i=0; i<_n; i++)
        // compute output sample
        IIRFILT(_execute)(_q, _x[i], &_y[i]);
}


// get filter length (order + 1)
unsigned int IIRFILT(_get_length)(IIRFILT() _q)
{
    return _q->n;
}

// compute complex frequency response
//  _q      :   filter object
//  _fc     :   frequency
//  _H      :   output frequency response
void IIRFILT(_freqresponse)(IIRFILT()       _q,
                            float           _fc,
                            liquid_float_complex * _H)
{
    unsigned int i;
    liquid_float_complex H = 0.0f;

    if (_q->type == IIRFILT_TYPE_NORM) {
        // 
        liquid_float_complex Ha = 0.0f;
        liquid_float_complex Hb = 0.0f;

        for (i=0; i<_q->nb; i++)
            Hb += _q->b[i] * cexpf(_Complex_I*(T)(2*M_PI*_fc*i));

        for (i=0; i<_q->na; i++)
            Ha += _q->a[i] * cexpf(_Complex_I*(T)(2*M_PI*_fc*i));

        // TODO : check to see if we need to take conjugate
        H = Hb / Ha;
    } else {

        H = 1.0f;

        // compute 3-point DFT for each second-order section
        for (i=0; i<_q->nsos; i++) {
            liquid_float_complex Hb =  _q->b[3*i+0] * cexpf(_Complex_I*(T)(2*M_PI*_fc*0)) +
                                _q->b[3*i+1] * cexpf(_Complex_I*(T)(2*M_PI*_fc*1)) +
                                _q->b[3*i+2] * cexpf(_Complex_I*(T)(2*M_PI*_fc*2));

            liquid_float_complex Ha =  _q->a[3*i+0] * cexpf(_Complex_I*(T)(2*M_PI*_fc*0)) +
                                _q->a[3*i+1] * cexpf(_Complex_I*(T)(2*M_PI*_fc*1)) +
                                _q->a[3*i+2] * cexpf(_Complex_I*(T)(2*M_PI*_fc*2));

            // TODO : check to see if we need to take conjugate
            H *= Hb / Ha;
        }
    }

    // set return value
    *_H = H;
}

// compute group delay in samples
//  _q      :   filter object
//  _fc     :   frequency
float IIRFILT(_groupdelay)(IIRFILT() _q,
                           float     _fc)
{
    float groupdelay = 0;
    unsigned int i;

    if (_q->type == IIRFILT_TYPE_NORM) {
        // compute group delay from regular transfer function form

        // copy coefficients
        float *b = (float*)malloc(_q->nb*sizeof(float));
        float *a = (float*)malloc(_q->na*sizeof(float));
        for (i=0; i<_q->nb; i++) b[i] = crealf(_q->b[i]);
        for (i=0; i<_q->na; i++) a[i] = crealf(_q->a[i]);
        groupdelay = iir_group_delay(b, _q->nb, a, _q->na, _fc);
        free(a);
        free(b);
    } else {
        // accumulate group delay from second-order sections
        for (i=0; i<_q->nsos; i++)
            groupdelay += IIRFILTSOS(_groupdelay)(_q->qsos[i], _fc) - 2;
    }

    return groupdelay;
}

