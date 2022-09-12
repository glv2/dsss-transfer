/*
This file is part of dsss-transfer, a program to send or receive data
by software defined radio using the DSSS modulation.

Copyright 2022 Guillaume LE VAILLANT

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.


This file includes a variation of the code from the liquid-dsp library to
create a dsssframegen object. The original code has the following license:

Copyright (c) 2007 - 2020 Joseph Gaeddert

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

#include <complex.h>
#include <liquid/liquid.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define DSSSFRAME_H_USER_DEFAULT 8
#define DSSSFRAME_H_DEC 5

enum state {
    STATE_PREAMBLE = 0, // write preamble p/n sequence
    STATE_HEADER,       // write header symbols
    STATE_PAYLOAD,      // write payload symbols
    STATE_TAIL,         // tail symbols
};

struct dsssframegen_s {
    // interpolator
    unsigned int        k;             // interp samples/symbol (fixed at 2)
    unsigned int        m;             // interp filter delay (symbols)
    float               beta;          // excess bandwidth factor
    firinterp_crcf      interp;        // interpolator object
    float complex       buf_interp[2]; // output interpolator buffer [size: k x 1]

    dsssframegenprops_s props;        // payload properties
    dsssframegenprops_s header_props; // header properties

    // preamble
    float complex *     preamble_pn; // p/n sequence
    synth_crcf          header_synth;
    synth_crcf          payload_synth;

    // header
    unsigned char *     header;          // header data
    unsigned int        header_user_len; // header user section length
    unsigned int        header_dec_len;  // header length (decoded)
    qpacketmodem        header_encoder;  // header encoder/modulator
    unsigned int        header_mod_len;  // header length
    float complex *     header_mod;

    // payload
    unsigned int        payload_dec_len; // length of decoded
    qpacketmodem        payload_encoder;
    unsigned int        payload_mod_len;
    float complex *     payload_mod;

    // counters/states
    unsigned int        symbol_counter; // output symbol number
    unsigned int        sample_counter; // output sample number
    unsigned int        bit_counter;    // output bit number
    int                 bit_high;       // current bit is 1
    float complex       sym;
    int                 frame_assembled; // frame assembled flag
    int                 frame_complete;  // frame completed flag
    enum state          state;           // write state
};

dsssframegen dsssframegen_create_set(unsigned int _n,
                                     dsssframegenprops_s * _fgprops)
{
    if ((_n < 2) || (_n > 64)) {
        fprintf(stderr, "dsssframegen_create_set(), spreading factor must be between 2 and 64");
        return NULL;
    }

    dsssframegen q = (dsssframegen)calloc(1, sizeof(struct dsssframegen_s));
    unsigned int i;

    // create pulse-shaping filter
    q->k      = 2;
    q->m      = 7;
    q->beta   = 0.25f;
    q->interp = firinterp_crcf_create_prototype(LIQUID_FIRFILT_ARKAISER, q->k, q->m, q->beta, 0);

    // generate pn sequence
    q->preamble_pn = (float complex *)malloc(64 * sizeof(float complex));
    msequence ms   = msequence_create(7, 0x0089, 1);
    for (i = 0; i < 64; i++) {
        q->preamble_pn[i] = (msequence_advance(ms) ? M_SQRT1_2 : -M_SQRT1_2);
        q->preamble_pn[i] += (msequence_advance(ms) ? M_SQRT1_2 : -M_SQRT1_2) * _Complex_I;
    }
    msequence_destroy(ms);

    float complex * pn = (float complex *)malloc(_n * sizeof(float complex));
    ms                        = msequence_create(7, 0x00cb, 0x53);
    for (i = 0; i < _n; i++) {
        pn[i] = (msequence_advance(ms) ? M_SQRT1_2 : -M_SQRT1_2);
        pn[i] += (msequence_advance(ms) ? M_SQRT1_2 : -M_SQRT1_2) * _Complex_I;
    }
    q->header_synth  = synth_crcf_create(pn, _n);
    q->payload_synth = synth_crcf_create(pn, _n);
    free(pn);
    msequence_destroy(ms);

    dsssframegen_reset(q);

    q->header          = NULL;
    q->header_user_len = DSSSFRAME_H_USER_DEFAULT;
    q->header_dec_len  = DSSSFRAME_H_DEC + q->header_user_len;
    q->header_mod      = NULL;
    q->header_encoder  = qpacketmodem_create();

    q->payload_encoder = qpacketmodem_create();
    q->payload_dec_len = 0;
    q->payload_mod_len = 0;
    q->payload_mod     = NULL;

    dsssframegen_setprops(q, _fgprops);
    dsssframegen_set_header_props(q, NULL);
    dsssframegen_set_header_len(q, q->header_user_len);

    return q;
}
