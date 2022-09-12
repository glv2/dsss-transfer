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
create a dsssframesync object. The original code has the following license:

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

enum state {
    DSSSFRAMESYNC_STATE_DETECTFRAME = 0,
    DSSSFRAMESYNC_STATE_RXPREAMBLE,
    DSSSFRAMESYNC_STATE_RXHEADER,
    DSSSFRAMESYNC_STATE_RXPAYLOAD,
};

struct dsssframesync_s {
    framesync_callback  callback;
    void *              userdata;
    framesyncstats_s    framesyncstats;
    framedatastats_s    framedatastats;

    unsigned int        k;
    unsigned int        m;
    float               beta;
    qdetector_cccf      detector;
    float               tau_hat;
    float               dphi_hat;
    float               phi_hat;
    float               gamma_hat;
    nco_crcf            mixer;
    nco_crcf            pll;

    firpfb_crcf         mf;
    unsigned int        npfb;
    int                 mf_counter;
    unsigned int        pfb_index;

    float complex *     preamble_pn;
    float complex *     preamble_rx;
    synth_crcf          header_synth;
    synth_crcf          payload_synth;

    int                 header_soft;
    flexframegenprops_s header_props;
    float complex *     header_spread;
    unsigned int        header_spread_len;
    qpacketmodem        header_decoder;
    unsigned int        header_user_len;
    unsigned int        header_dec_len;
    unsigned char *     header_dec;
    int                 header_valid;

    int                 payload_soft;
    float complex *     payload_spread;
    unsigned int        payload_spread_len;
    qpacketmodem        payload_decoder;
    unsigned int        payload_dec_len;
    unsigned char *     payload_dec;
    int                 payload_valid;

    unsigned int        preamble_counter;
    unsigned int        symbol_counter;
    enum state          state;
};

dsssframesync dsssframesync_create_set(unsigned int _n,
                                       framesync_callback _callback,
                                       void * _userdata)
{
    if ((_n < 2) || (_n > 64)) {
        fprintf(stderr, "dsssframesync_create_set(), spreading factor must be between 2 and 64");
        return NULL;
    }

    dsssframesync q = (dsssframesync)calloc(1, sizeof(struct dsssframesync_s));
    q->callback     = _callback;
    q->userdata     = _userdata;

    q->k    = 2;
    q->m    = 7;
    q->beta = 0.3f;

    unsigned int i;
    q->preamble_pn = (float complex *)calloc(64, sizeof(float complex));
    q->preamble_rx = (float complex *)calloc(64, sizeof(float complex));
    msequence ms   = msequence_create(7, 0x0089, 1);
    for (i = 0; i < 64; i++) {
        q->preamble_pn[i] = (msequence_advance(ms) ? M_SQRT1_2 : -M_SQRT1_2);
        q->preamble_pn[i] += (msequence_advance(ms) ? M_SQRT1_2 : -M_SQRT1_2) * _Complex_I;
    }
    msequence_destroy(ms);

    float complex * pn = (float complex *)calloc(_n, sizeof(float complex));
    ms                        = msequence_create(7, 0x00cb, 0x53);
    for (i = 0; i < _n; i++) {
        pn[i] = (msequence_advance(ms) ? M_SQRT1_2 : -M_SQRT1_2);
        pn[i] += (msequence_advance(ms) ? M_SQRT1_2 : -M_SQRT1_2) * _Complex_I;
    }
    q->header_synth  = synth_crcf_create(pn, _n);
    q->payload_synth = synth_crcf_create(pn, _n);
    synth_crcf_pll_set_bandwidth(q->header_synth, 1e-4f);
    synth_crcf_pll_set_bandwidth(q->payload_synth, 1e-4f);
    free(pn);
    msequence_destroy(ms);

    q->detector = qdetector_cccf_create_linear(
        q->preamble_pn, 64, LIQUID_FIRFILT_ARKAISER, q->k, q->m, q->beta);
    qdetector_cccf_set_threshold(q->detector, 0.5f);

    q->npfb = 32;
    q->mf   = firpfb_crcf_create_rnyquist(LIQUID_FIRFILT_ARKAISER, q->npfb, q->k, q->m, q->beta);

    q->mixer = nco_crcf_create(LIQUID_NCO);
    q->pll   = nco_crcf_create(LIQUID_NCO);
    nco_crcf_pll_set_bandwidth(q->pll, 1e-4f); // very low bandwidth

    q->header_decoder  = qpacketmodem_create();
    q->header_user_len = DSSSFRAME_H_USER_DEFAULT;
    dsssframesync_set_header_props(q, NULL);

    q->payload_decoder    = qpacketmodem_create();
    q->payload_spread_len = _n;
    q->payload_spread
        = (float complex *)malloc(q->payload_spread_len * sizeof(float complex));

    dsssframesync_reset_framedatastats(q);
    dsssframesync_reset(q);

    return q;
}
