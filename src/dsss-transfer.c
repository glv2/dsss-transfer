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

#include <complex.h>
#include <fcntl.h>
#include <liquid/liquid.h>
#include <math.h>
#include <signal.h>
#include <SoapySDR/Device.h>
#include <SoapySDR/Formats.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>
#include "dsssframe.h"
#include "dsss-transfer.h"
#include "gettext.h"

#define TAU (2 * M_PI)

#define MIN(x, y) ((x < y) ? x : y)
#define MAX(x, y) ((x > y) ? x : y)

#define _(string) gettext(string)

#define SOAPYSDR_CHECK(funcall) \
{ \
  int e = funcall; \
  if(e != 0) \
  { \
    fprintf(stderr, _("Error: %s\n"), SoapySDRDevice_lastError()); \
    exit(EXIT_FAILURE); \
  } \
}

typedef enum
  {
    IO,
    FILENAME,
    SOAPYSDR
  } radio_type_t;

typedef union
{
  FILE *file;
  SoapySDRDevice *soapysdr;
} radio_device_t;

typedef union
{
  SoapySDRStream *soapysdr;
} radio_stream_t;

struct dsss_transfer_s
{
  radio_type_t radio_type;
  radio_device_t radio_device;
  radio_stream_t radio_stream;
  unsigned char emit;
  FILE *file;
  unsigned long int sample_rate;
  unsigned int bit_rate;
  unsigned long int frequency;
  long int frequency_offset;
  unsigned int spreading_factor;
  crc_scheme crc;
  fec_scheme inner_fec;
  fec_scheme outer_fec;
  char id[5];
  FILE *dump;
  unsigned char stop;
  int (*data_callback)(void *, unsigned char *, unsigned int);
  void *callback_context;
  unsigned int timeout;
  time_t timeout_start;
  firhilbf audio_converter;
  float audio_gain;
};

unsigned char stop = 0;
unsigned char verbose = 0;

void dsss_transfer_set_verbose(unsigned char v)
{
  verbose = v;
}

unsigned char dsss_transfer_is_verbose()
{
  return(verbose);
}

void dump_samples(dsss_transfer_t transfer,
                  complex float *samples,
                  unsigned int samples_size)
{
  fwrite(samples, sizeof(complex float), samples_size, transfer->dump);
}

int read_data(void *context,
              unsigned char *payload,
              unsigned int payload_size)
{
  dsss_transfer_t transfer = (dsss_transfer_t) context;
  int n;

  if(feof(transfer->file))
  {
    return(-1);
  }

  n = fread(payload, 1, payload_size, transfer->file);
  if(n == 0)
  {
    usleep(1);
  }

  return(n);
}

int write_data(void *context,
               unsigned char *payload,
               unsigned int payload_size)
{
  dsss_transfer_t transfer = (dsss_transfer_t) context;

  fwrite(payload, 1, payload_size, transfer->file);
  if(transfer->file == stdout)
  {
    fflush(transfer->file);
  }

  return(payload_size);
}

void write_audio(dsss_transfer_t transfer,
                 complex float *samples,
                 unsigned int samples_size,
                 FILE *output)
{
  float gain = transfer->audio_gain;
  unsigned int n;
  float audio_samples[2];
  short int audio_samples_s16[2];

  for(n = 0; n < samples_size; n++)
  {
    firhilbf_interp_execute(transfer->audio_converter,
                            samples[n],
                            audio_samples);
    audio_samples_s16[0] = (audio_samples[0] * gain) * 32767;
    audio_samples_s16[1] = (audio_samples[1] * gain) * 32767;
    fwrite(audio_samples_s16, sizeof(short int), 2, output);
  }
}

unsigned int read_audio(dsss_transfer_t transfer,
                        complex float *samples,
                        unsigned int samples_size,
                        FILE* input)
{
  float gain = transfer->audio_gain;
  unsigned int n = 0;
  float audio_samples[2];
  short int audio_samples_s16[2];

  while((n < samples_size) &&
        (fread(audio_samples_s16, sizeof(short int), 2, input) == 2))
  {
    audio_samples[0] = (audio_samples_s16[0] * gain) / 32768.0;
    audio_samples[1] = (audio_samples_s16[1] * gain) / 32768.0;
    firhilbf_decim_execute(transfer->audio_converter,
                           audio_samples,
                           &samples[n]);
    n++;
  }
  return(n);
}

void send_to_radio(dsss_transfer_t transfer,
                   complex float *samples,
                   unsigned int samples_size,
                   int last)
{
  unsigned int n;
  unsigned int size;
  int flags = 0;
  size_t mask = 0;
  long long int timestamp = 0;
  int r;
  const void *buffers[1];

  if(transfer->dump)
  {
    dump_samples(transfer, samples, samples_size);
  }

  switch(transfer->radio_type)
  {
  case IO:
    if(transfer->audio_converter)
    {
      write_audio(transfer, samples, samples_size, stdout);
    }
    else
    {
      fwrite(samples, sizeof(complex float), samples_size, stdout);
    }
    break;

  case FILENAME:
    if(transfer->audio_converter)
    {
      write_audio(transfer, samples, samples_size, transfer->radio_device.file);
    }
    else
    {
      fwrite(samples,
             sizeof(complex float),
             samples_size,
             transfer->radio_device.file);
    }
    break;

  case SOAPYSDR:
    n = 0;
    while((n < samples_size) && (!stop) && (!transfer->stop))
    {
      buffers[0] = &samples[n];
      size = samples_size - n;
      r = SoapySDRDevice_writeStream(transfer->radio_device.soapysdr,
                                     transfer->radio_stream.soapysdr,
                                     buffers,
                                     size,
                                     &flags,
                                     0,
                                     10000);
      if(r > 0)
      {
        n += r;
      }
    }
    if(last)
    {
      /* Complete the remaining buffer to ensure that SoapySDR
       * will process it */
      flags = SOAPY_SDR_END_BURST;
      size = SoapySDRDevice_getStreamMTU(transfer->radio_device.soapysdr,
                                         transfer->radio_stream.soapysdr);
      bzero(samples, samples_size * sizeof(complex float));
      buffers[0] = samples;
      while((size > 0) && (!stop) && (!transfer->stop))
      {
        n = (samples_size < size) ? samples_size : size;
        r = SoapySDRDevice_writeStream(transfer->radio_device.soapysdr,
                                       transfer->radio_stream.soapysdr,
                                       buffers,
                                       n,
                                       &flags,
                                       0,
                                       10000);
        if(r > 0)
        {
          size -= r;
        }
      }
      do
      {
        r = SoapySDRDevice_readStreamStatus(transfer->radio_device.soapysdr,
                                            transfer->radio_stream.soapysdr,
                                            &mask,
                                            &flags,
                                            &timestamp,
                                            10000);
      }
      while((r != SOAPY_SDR_UNDERFLOW) && (!stop) && (!transfer->stop));
    }
    break;
  }
}

unsigned int receive_from_radio(dsss_transfer_t transfer,
                                complex float *samples,
                                unsigned int samples_size)
{
  unsigned int n = 0;
  int flags;
  long long int timestamp;
  int r;
  void *buffers[1];

  switch(transfer->radio_type)
  {
  case IO:
    if(transfer->audio_converter)
    {
      n = read_audio(transfer, samples, samples_size, stdin);
    }
    else
    {
      n = fread(samples, sizeof(complex float), samples_size, stdin);
    }
    break;

  case FILENAME:
    if(transfer->audio_converter)
    {
      n = read_audio(transfer,
                     samples,
                     samples_size,
                     transfer->radio_device.file);
    }
    else
    {
      n = fread(samples,
                sizeof(complex float),
                samples_size,
                transfer->radio_device.file);
    }
    break;

  case SOAPYSDR:
    buffers[0] = samples;
    r = SoapySDRDevice_readStream(transfer->radio_device.soapysdr,
                                  transfer->radio_stream.soapysdr,
                                  buffers,
                                  samples_size,
                                  &flags,
                                  &timestamp,
                                  10000);
    if(r >= 0)
    {
      n = r;
    }
    break;
  }
  return(n);
}

void set_counter(unsigned char *header, unsigned int counter)
{
  header[4] = (counter >> 24) & 255;
  header[5] = (counter >> 16) & 255;
  header[6] = (counter >> 8) & 255;
  header[7] = counter & 255;
}

unsigned int get_counter(unsigned char *header)
{
  return((header[4] << 24) | (header[5] << 16) | (header[6] << 8) | header[7]);
}

void send_dummy_samples(dsss_transfer_t transfer,
                        msresamp_crcf resampler,
                        nco_crcf oscillator,
                        complex float *samples,
                        unsigned int delay,
                        int last)
{
  unsigned int i;
  unsigned int n;
  complex float zero_sample = 0;

  for(i = 0; i < delay; i++)
  {
    msresamp_crcf_execute(resampler, &zero_sample, 1, samples, &n);
    if(transfer->frequency_offset != 0)
    {
      nco_crcf_mix_block_up(oscillator, samples, samples, n);
    }
    if(i + 1 < delay)
    {
      send_to_radio(transfer, samples, n, 0);
    }
    else
    {
      send_to_radio(transfer, samples, n, last);
    }
  }
}

void send_frames(dsss_transfer_t transfer)
{
  unsigned int samples_per_symbol = 2;
  float samples_per_bit = transfer->spreading_factor * samples_per_symbol;
  dsssframegenprops_s frame_properties;
  dsssframegen frame_generator;
  float resampling_ratio = (float) transfer->sample_rate / (transfer->bit_rate *
                                                            samples_per_bit);
  msresamp_crcf resampler = msresamp_crcf_create(resampling_ratio, 60);
  unsigned int delay = ceilf(msresamp_crcf_get_delay(resampler));
  unsigned int header_size = 8;
  unsigned char header[header_size];
  /* Try to make frames of approximately 100 ms, but containing at least
   * 16 bytes and at most 8000 bytes of payload */
  unsigned int byte_rate = transfer->bit_rate / 8;
  unsigned int payload_size = MIN(MAX(byte_rate * 0.1, 16), 8000);
  int r;
  unsigned int n;
  unsigned int i;
  /* Process data by blocks of 50 ms */
  unsigned int frame_samples_size = ceilf((transfer->bit_rate *
                                           samples_per_bit) / 20.0);
  unsigned int samples_size = ceilf((frame_samples_size + delay) * resampling_ratio);
  int frame_complete;
  float center_frequency = (float) transfer->frequency_offset / transfer->sample_rate;
  nco_crcf oscillator = nco_crcf_create(LIQUID_NCO);
  float maximum_amplitude = 1;
  unsigned int counter = 0;
  unsigned char *payload = malloc(payload_size);
  complex float *frame_samples = malloc(frame_samples_size * sizeof(complex float));
  complex float *samples = malloc(samples_size * sizeof(complex float));

  if((payload == NULL) || (frame_samples == NULL) || (samples == NULL))
  {
    fprintf(stderr, _("Error: Memory allocation failed\n"));
    exit(EXIT_FAILURE);
  }

  nco_crcf_set_phase(oscillator, 0);
  nco_crcf_set_frequency(oscillator, TAU * center_frequency);

  frame_properties.check = transfer->crc;
  frame_properties.fec0 = transfer->inner_fec;
  frame_properties.fec1 = transfer->outer_fec;
  frame_generator = dsssframegen_create_set(transfer->spreading_factor,
                                            &frame_properties);
  dsssframegen_set_header_props(frame_generator, &frame_properties);
  dsssframegen_set_header_len(frame_generator, header_size);
  memcpy(header, transfer->id, 4);
  set_counter(header, counter);

  while((!stop) && (!transfer->stop))
  {
    r = transfer->data_callback(transfer->callback_context, payload, payload_size);
    if(r < 0)
    {
      break;
    }
    n = r;
    if(n > 0)
    {
      dsssframegen_assemble(frame_generator, header, payload, n);
      frame_complete = 0;
      while(!frame_complete)
      {
        frame_complete = dsssframegen_write_samples(frame_generator,
                                                    frame_samples,
                                                    frame_samples_size);
        n = frame_samples_size;
        if(frame_complete)
        {
          /* Don't send the padding 0 bytes */
          while((n > 0) && (frame_samples[n - 1] == 0))
          {
            n--;
          }
        }
        /* Reduce the amplitude of samples because the frame generator and
         * the resampler may produce samples with an amplitude greater than
         * 1.0 depending on the number of carriers and resampling ratio */
        maximum_amplitude = 1;
        for(i = 0; i < n; i++)
        {
          if(cabsf(frame_samples[i]) > maximum_amplitude)
          {
            maximum_amplitude = cabsf(frame_samples[i]);
          }
        }
        liquid_vectorcf_mulscalar(frame_samples,
                                  n,
                                  0.75 / maximum_amplitude,
                                  frame_samples);
        msresamp_crcf_execute(resampler, frame_samples, n, samples, &n);
        if(transfer->frequency_offset != 0)
        {
          nco_crcf_mix_block_up(oscillator, samples, samples, n);
        }
        send_to_radio(transfer, samples, n, 0);
      }
      counter++;
      set_counter(header, counter);
    }
    else
    {
      /* Underrun when reading from stdin. Send some dummy samples to get the
       * remaining output samples for the end of current frame (because of
       * resampler and filter delays) and send them */
      send_dummy_samples(transfer,
                         resampler,
                         oscillator,
                         samples,
                         delay,
                         0);
    }
  }

  /* Send some dummy samples to get the remaining output samples (because of
   * resampler and filter delays) */
  send_dummy_samples(transfer,
                     resampler,
                     oscillator,
                     samples,
                     delay,
                     1);

  free(samples);
  free(frame_samples);
  free(payload);
  nco_crcf_destroy(oscillator);
  msresamp_crcf_destroy(resampler);
  dsssframegen_destroy(frame_generator);
}

int frame_received(unsigned char *header,
                   int header_valid,
                   unsigned char *payload,
                   unsigned int payload_size,
                   int payload_valid,
                   framesyncstats_s stats,
                   void *user_data)
{
  dsss_transfer_t transfer = (dsss_transfer_t) user_data;
  char id[5];
  unsigned int counter;

  transfer->timeout_start = time(NULL);
  memcpy(id, header, 4);
  id[4] = '\0';
  counter = get_counter(header);

  if(!header_valid || !payload_valid)
  {
    if(verbose)
    {
      if(!header_valid)
      {
        fprintf(stderr, _("Frame %u for '%s': corrupted header\n"), counter, id);
      }
      if(!payload_valid)
      {
        fprintf(stderr, _("Frame %u for '%s': corrupted payload\n"), counter, id);
      }
      fflush(stderr);
    }
  }
  else if(memcmp(id, transfer->id, 4) != 0)
  {
    if(verbose)
    {
      fprintf(stderr, _("Frame %u for '%s': ignored\n"), counter, id);
      fflush(stderr);
    }
  }
  else
  {
    transfer->data_callback(transfer->callback_context, payload, payload_size);
  }
  return(0);
}

void receive_frames(dsss_transfer_t transfer)
{
  unsigned int samples_per_symbol = 2;
  float samples_per_bit = transfer->spreading_factor * samples_per_symbol;
  dsssframegenprops_s frame_properties;
  dsssframesync frame_synchronizer;
  float resampling_ratio = (transfer->bit_rate *
                            samples_per_bit) / (float) transfer->sample_rate;
  msresamp_crcf resampler = msresamp_crcf_create(resampling_ratio, 60);
  unsigned int delay = ceilf(msresamp_crcf_get_delay(resampler));
  unsigned int header_size = 8;
  unsigned int n;
  /* Process data by blocks of 50 ms */
  unsigned int frame_samples_size = ceilf((transfer->bit_rate *
                                           samples_per_bit) / 20.0);
  unsigned int samples_size = floorf(frame_samples_size / resampling_ratio);
  nco_crcf oscillator = nco_crcf_create(LIQUID_NCO);
  complex float *frame_samples = malloc((frame_samples_size + delay) *
                                        sizeof(complex float));
  complex float *samples = malloc((samples_size + delay) *
                                  sizeof(complex float));

  if((frame_samples == NULL) || (samples == NULL))
  {
    fprintf(stderr, _("Error: Memory allocation failed\n"));
    exit(EXIT_FAILURE);
  }

  nco_crcf_set_phase(oscillator, 0);
  nco_crcf_set_frequency(oscillator, TAU * ((float) transfer->frequency_offset /
                                            transfer->sample_rate));

  frame_synchronizer = dsssframesync_create_set(transfer->spreading_factor,
                                                frame_received,
                                                transfer);
  frame_properties.check = transfer->crc;
  frame_properties.fec0 = transfer->inner_fec;
  frame_properties.fec1 = transfer->outer_fec;
  dsssframesync_set_header_props(frame_synchronizer, &frame_properties);
  dsssframesync_set_header_len(frame_synchronizer, header_size);

  while((!stop) && (!transfer->stop))
  {
    n = receive_from_radio(transfer, samples, samples_size);
    if((n == 0) &&
       ((transfer->radio_type == IO) || (transfer->radio_type == FILENAME)))
    {
      break;
    }
    if((transfer->timeout > 0) &&
       (time(NULL) > transfer->timeout_start + transfer->timeout))
    {
      if(verbose)
      {
        fprintf(stderr, _("Timeout: %d s without frames\n"), transfer->timeout);
      }
      break;
    }
    if(transfer->dump)
    {
      dump_samples(transfer, samples, n);
    }
    if(transfer->frequency_offset != 0)
    {
      nco_crcf_mix_block_down(oscillator, samples, samples, n);
    }
    msresamp_crcf_execute(resampler, samples, n, frame_samples, &n);
    dsssframesync_execute(frame_synchronizer, frame_samples, n);
  }

  for(n = 0; n < delay; n++)
  {
    samples[n] = 0;
  }
  msresamp_crcf_execute(resampler, samples, delay, frame_samples, &n);
  dsssframesync_execute(frame_synchronizer, frame_samples, n);
  while(dsssframesync_is_frame_open(frame_synchronizer))
  {
    dsssframesync_execute(frame_synchronizer, samples, 1);
  }

  free(samples);
  free(frame_samples);
  nco_crcf_destroy(oscillator);
  msresamp_crcf_destroy(resampler);
  dsssframesync_destroy(frame_synchronizer);
}

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
                                              unsigned char audio)
{
  int direction;
  SoapySDRKwargs kwargs;
  unsigned int n;
  char *gain_name;
  int gain_value;
  dsss_transfer_t transfer = malloc(sizeof(struct dsss_transfer_s));

  if(transfer == NULL)
  {
    fprintf(stderr, _("Error: Memory allocation failed\n"));
    return(NULL);
  }
  bzero(transfer, sizeof(struct dsss_transfer_s));

  if(strcasecmp(radio_driver, "io") == 0)
  {
    transfer->radio_type = IO;
  }
  else if(strncasecmp(radio_driver, "file=", 5) == 0)
  {
    transfer->radio_type = FILENAME;
  }
  else
  {
    transfer->radio_type = SOAPYSDR;
  }

  transfer->stop = 0;
  transfer->emit = emit;
  transfer->file = NULL;
  transfer->data_callback = data_callback;
  transfer->callback_context = callback_context;

  if(sample_rate != 0)
  {
    transfer->sample_rate = sample_rate * ((1000000.0 - ppm) / 1000000.0);
  }
  else
  {
    fprintf(stderr, _("Error: Invalid sample rate\n"));
    free(transfer);
    return(NULL);
  }

  if(frequency != 0)
  {
    transfer->frequency = frequency * ((1000000.0 - ppm) / 1000000.0);
  }
  else
  {
    fprintf(stderr, _("Error: Invalid frequency\n"));
    free(transfer);
    return(NULL);
  }

  transfer->frequency_offset = frequency_offset;

  if(audio)
  {
    if((transfer->radio_type == IO) || (transfer->radio_type == FILENAME))
    {
      transfer->audio_converter = firhilbf_create(25, 60);
      /* The rate of audio samples is twice the rate of IQ samples */
      transfer->sample_rate = transfer->sample_rate / 2;
      /* -(sample_rate / 2) Hz IQ <=> 0 Hz audio
       * (sample_rate / 2) Hz IQ <=> (sample_rate * 2) Hz audio */
      transfer->frequency_offset = transfer->frequency - (transfer->sample_rate / 2);
      transfer->frequency = 0;
      gain_value = strtol(gain, NULL, 10);
      transfer->audio_gain = powf(10, gain_value / 20.0);
    }
    else
    {
      fprintf(stderr, _("Error: This radio type only supports IQ samples\n"));
      free(transfer);
      return(NULL);
    }
  }
  else
  {
    transfer->audio_converter = NULL;
    transfer->audio_gain = 0;
  }

  if(bit_rate != 0)
  {
    transfer->bit_rate = bit_rate;
  }
  else
  {
    fprintf(stderr, _("Error: Invalid bit rate\n"));
    free(transfer);
    return(NULL);
  }

  if(spreading_factor >= 2)
  {
    transfer->spreading_factor = spreading_factor;
  }
  else
  {
    fprintf(stderr, _("Error: Invalid spreading factor\n"));
    free(transfer);
    return(NULL);
  }

  transfer->crc = LIQUID_CRC_32;

  transfer->inner_fec = liquid_getopt_str2fec(inner_fec);
  if(transfer->inner_fec == LIQUID_FEC_UNKNOWN)
  {
    fprintf(stderr, _("Error: Invalid inner FEC\n"));
    free(transfer);
    return(NULL);
  }

  transfer->outer_fec = liquid_getopt_str2fec(outer_fec);
  if(transfer->outer_fec == LIQUID_FEC_UNKNOWN)
  {
    fprintf(stderr, _("Error: Invalid outer FEC\n"));
    free(transfer);
    return(NULL);
  }

  if(strlen(id) <= 4)
  {
    strcpy(transfer->id, id);
  }
  else
  {
    fprintf(stderr, _("Error: Id must be at most 4 bytes long\n"));
    free(transfer);
    return(NULL);
  }

  if(dump)
  {
    transfer->dump = fopen(dump, "wb");
    if(transfer->dump == NULL)
    {
      fprintf(stderr, _("Error: Failed to open '%s'\n"), dump);
      free(transfer);
      return(NULL);
    }
  }
  else
  {
    transfer->dump = NULL;
  }

  transfer->timeout = timeout;

  switch(transfer->radio_type)
  {
  case IO:
    break;

  case FILENAME:
    if(emit)
    {
      transfer->radio_device.file = fopen(radio_driver + 5, "wb");
    }
    else
    {
      transfer->radio_device.file = fopen(radio_driver + 5, "rb");
    }
    if(transfer->radio_device.file == NULL)
    {
      fprintf(stderr, _("Error: Failed to open '%s'\n"), radio_driver + 5);
      free(transfer);
      return(NULL);
    }
    break;

  case SOAPYSDR:
    transfer->radio_device.soapysdr = SoapySDRDevice_makeStrArgs(radio_driver);
    if(transfer->radio_device.soapysdr == NULL)
    {
      fprintf(stderr, _("Error: %s\n"), SoapySDRDevice_lastError());
      free(transfer);
      return(NULL);
    }
    direction = emit ? SOAPY_SDR_TX : SOAPY_SDR_RX;
    SOAPYSDR_CHECK(SoapySDRDevice_setSampleRate(transfer->radio_device.soapysdr,
                                                direction,
                                                0,
                                                transfer->sample_rate));
    SOAPYSDR_CHECK(SoapySDRDevice_setFrequency(transfer->radio_device.soapysdr,
                                               direction,
                                               0,
                                               transfer->frequency - transfer->frequency_offset,
                                               NULL));
    if(strchr(gain, '='))
    {
      kwargs = SoapySDRKwargs_fromString(gain);
      for(n = 0; n < kwargs.size; n++)
      {
        gain_name = kwargs.keys[n];
        gain_value = strtoul(kwargs.vals[n], NULL, 10);
        SOAPYSDR_CHECK(SoapySDRDevice_setGainElement(transfer->radio_device.soapysdr,
                                                     direction,
                                                     0,
                                                     gain_name,
                                                     gain_value));
      }
      SoapySDRKwargs_clear(&kwargs);
    }
    else
    {
      gain_value = strtoul(gain, NULL, 10);
      SOAPYSDR_CHECK(SoapySDRDevice_setGain(transfer->radio_device.soapysdr,
                                            direction,
                                            0,
                                            gain_value));
    }
    transfer->radio_stream.soapysdr = SoapySDRDevice_setupStream(transfer->radio_device.soapysdr,
                                                                 direction,
                                                                 SOAPY_SDR_CF32,
                                                                 NULL,
                                                                 0,
                                                                 NULL);
    if(transfer->radio_stream.soapysdr == NULL)
    {
      fprintf(stderr, _("Error: %s\n"), SoapySDRDevice_lastError());
      SoapySDRDevice_unmake(transfer->radio_device.soapysdr);
      free(transfer);
      return(NULL);
    }
    break;

  default:
    fprintf(stderr, _("Error: Unknown radio type\n"));
    free(transfer);
    return(NULL);
    break;
  }

  return(transfer);
}

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
                                     unsigned char audio)
{
  int flags;
  dsss_transfer_t transfer;

  transfer = dsss_transfer_create_callback(radio_driver,
                                           emit,
                                           emit ? read_data : write_data,
                                           NULL,
                                           sample_rate,
                                           bit_rate,
                                           frequency,
                                           frequency_offset,
                                           gain,
                                           ppm,
                                           spreading_factor,
                                           inner_fec,
                                           outer_fec,
                                           id,
                                           dump,
                                           timeout,
                                           audio);
  if(transfer == NULL)
  {
    return(NULL);
  }

  transfer->callback_context = transfer;
  if(file)
  {
    if(emit)
    {
      transfer->file = fopen(file, "rb");
    }
    else
    {
      transfer->file = fopen(file, "wb");
    }
    if(transfer->file == NULL)
    {
      fprintf(stderr, _("Error: Failed to open '%s'\n"), file);
      free(transfer);
      return(NULL);
    }
  }
  else
  {
    if(emit)
    {
      transfer->file = stdin;
      flags = fcntl(STDIN_FILENO, F_GETFL);
      fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
    }
    else
    {
      transfer->file = stdout;
    }
  }

  return(transfer);
}

void dsss_transfer_free(dsss_transfer_t transfer)
{
  if(transfer)
  {
    if(transfer->file)
    {
      fclose(transfer->file);
    }
    if(transfer->dump)
    {
      fclose(transfer->dump);
    }
    if(transfer->audio_converter)
    {
      firhilbf_destroy(transfer->audio_converter);
    }
    switch(transfer->radio_type)
    {
    case IO:
      break;

    case FILENAME:
      fclose(transfer->radio_device.file);
      break;

    case SOAPYSDR:
      SoapySDRDevice_deactivateStream(transfer->radio_device.soapysdr,
                                      transfer->radio_stream.soapysdr,
                                      0,
                                      0);
      SoapySDRDevice_closeStream(transfer->radio_device.soapysdr,
                                 transfer->radio_stream.soapysdr);
      SoapySDRDevice_unmake(transfer->radio_device.soapysdr);
      break;

    default:
      break;
    }
    free(transfer);
  }
}

void dsss_transfer_start(dsss_transfer_t transfer)
{
  stop = 0;
  transfer->stop = 0;

  switch(transfer->radio_type)
  {
  case IO:
    if(verbose)
    {
      fprintf(stderr, _("Info: Using IO pseudo-radio\n"));
    }
    break;

  case FILENAME:
    if(verbose)
    {
      fprintf(stderr, _("Info: Using FILENAME pseudo-radio\n"));
    }
    break;

  case SOAPYSDR:
    SoapySDRDevice_activateStream(transfer->radio_device.soapysdr,
                                  transfer->radio_stream.soapysdr,
                                  0,
                                  0,
                                  0);
    break;

  default:
    return;
  }

  transfer->timeout_start = time(NULL);
  if(transfer->emit)
  {
    send_frames(transfer);
  }
  else
  {
    receive_frames(transfer);
  }
}

void dsss_transfer_stop(dsss_transfer_t transfer)
{
  transfer->stop = 1;
}

void dsss_transfer_stop_all()
{
  stop = 1;
}

void dsss_transfer_print_available_radios()
{
  size_t size;
  unsigned int i;
  unsigned int j;
  char *driver;
  char *serial;
  SoapySDRKwargs *devices = SoapySDRDevice_enumerate(NULL, &size);

  if(size == 0)
  {
    printf(_("  No radio detected\n"));
  }
  else
  {
    for(i = 0; i < size; i++)
    {
      driver = NULL;
      serial = NULL;
      for(j = 0; j < devices[i].size; j++)
      {
        if(strcasecmp(devices[i].keys[j], "driver") == 0)
        {
          driver = devices[i].vals[j];
        }
        else if(strcasecmp(devices[i].keys[j], "serial") == 0)
        {
          serial = devices[i].vals[j];
          if(strlen(serial) > 8)
          {
            serial = &serial[strlen(serial) - 8];
          }
        }
      }
      printf("  - driver=%s,serial=%s\n", driver, serial);
    }
  }
  SoapySDRKwargsList_clear(devices, size);
}

void dsss_transfer_print_available_forward_error_codes()
{
  liquid_print_fec_schemes();
}
