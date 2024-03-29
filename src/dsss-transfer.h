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
*/

#ifndef DSSS_TRANSFER_H
#define DSSS_TRANSFER_H

typedef struct dsss_transfer_s *dsss_transfer_t;

/* Set the verbosity level
 *  - v: if not 0, print some debug messages to stderr
 */
void dsss_transfer_set_verbose(unsigned char v);

/* Get the verbosity level */
unsigned char dsss_transfer_is_verbose();

/* Initialize a new transfer
 *  - radio_driver: radio to use (e.g. "io" or "driver=hackrf")
 *  - emit: 1 for transmit mode; 0 for receive mode
 *  - file: in transmit mode, read data from this file
 *          in receive mode, write data to this file
 *          if NULL, use stdin or stdout instead
 *  - sample_rate: samples per second
 *  - bit_rate: bits per second
 *  - frequency: center frequency of the transfer in Hertz
 *  - frequency_offset: set the frequency of the radio frequency_offset Hz
 *    lower than the frequency of the transfer (can be negative to set the
 *    frequency higher)
 *  - gain: gain of the radio transceiver
 *  - ppm: correction for the radio clock
 *  - spreading_factor: spectrum spreading factor
 *  - inner_fec: inner forward error correction code to use
 *  - outer_fec: outer forward error correction code to use
 *  - id: transfer id; when receiving, frames with a different id will be
 *    ignored
 *  - dump: if not NULL, write raw samples sent or received to this file
 *  - timeout: number of seconds after which reception will be stopped if no
 *    frame has been received; 0 means no timeout
 *  - audio: 0 to use IQ samples, 1 to use audio samples
 *
 * If the transfer initialization fails, the function returns NULL.
 */
dsss_transfer_t dsss_transfer_create(char *radio_driver,
                                     unsigned char emit,
                                     char *file,
                                     unsigned long int sample_rate,
                                     unsigned int bit_rate,
                                     unsigned long int frequency,
                                     long int frequency_offset,
                                     char *gain,
                                     float ppm,
                                     unsigned int spreading_factor,
                                     char *inner_fec,
                                     char *outer_fec,
                                     char *id,
                                     char *dump,
                                     unsigned int timeout,
                                     unsigned char audio);

/* Initialize a new transfer using a callback
 * The parameters are the same as dsss_transfer_create() except that the 'file'
 * string is replaced by the 'data_callback' function pointer and the
 * 'callback_context' pointer.
 * The callback function must have the following type:
 *
 *  int callback(void *context,
 *               unsigned char *payload,
 *               unsigned int payload_size)
 *
 * When emitting, the callback must try to read 'payload_size' bytes from
 * somewhere and put them into 'payload'. It must return the number of bytes
 * read, or -1 if the input stream is finished.
 * When receiving, the callback must take 'payload_size' bytes from 'payload'
 * and write them somewhere. It must return only when all the bytes have been
 * written. The returned value should be the number of bytes written, but
 * currently it is not used.
 * The user-specified 'callback_context' pointer is passed to the callback
 * as 'context'.
 */
dsss_transfer_t dsss_transfer_create_callback(char *radio_driver,
                                              unsigned char emit,
                                              int (*data_callback)(void *,
                                                                   unsigned char *,
                                                                   unsigned int),
                                              void *callback_context,
                                              unsigned long int sample_rate,
                                              unsigned int bit_rate,
                                              unsigned long int frequency,
                                              long int frequency_offset,
                                              char *gain,
                                              float ppm,
                                              unsigned int spreading_factor,
                                              char *inner_fec,
                                              char *outer_fec,
                                              char *id,
                                              char *dump,
                                              unsigned int timeout,
                                              unsigned char audio);

/* Cleanup after a finished transfer */
void dsss_transfer_free(dsss_transfer_t transfer);

/* Start a transfer and return when finished */
void dsss_transfer_start(dsss_transfer_t transfer);

/* Interrupt a transfer */
void dsss_transfer_stop(dsss_transfer_t transfer);

/* Interrupt all transfers */
void dsss_transfer_stop_all();

/* Print list of detected software defined radios */
void dsss_transfer_print_available_radios();

/* Print list of supported forward error codes */
void dsss_transfer_print_available_forward_error_codes();

#endif
