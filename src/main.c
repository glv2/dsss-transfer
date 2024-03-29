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

#include <locale.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "dsss-transfer.h"
#include "gettext.h"

#define _(string) gettext(string)

void signal_handler(int signum)
{
  if(dsss_transfer_is_verbose())
  {
    fprintf(stderr, _("\nStopping (signal %d)\n"), signum);
  }
  else
  {
    fprintf(stderr, "\n");
  }
  dsss_transfer_stop_all();
}

void usage()
{
  printf(_("dsss-transfer version 1.2.0\n"));
  printf("\n");
  printf(_("Usage: dsss-transfer [options] [filename]\n"));
  printf("\n");
  printf(_("Options:\n"));
  printf("  -a\n");
  printf(_("    Use audio samples instead of IQ samples.\n"));
  printf(_("  -b <bit rate>  (default: 100 b/s)\n"));
  printf(_("    Bit rate of the DSSS transmission.\n"));
  printf(_("  -c <ppm>  (default: 0.0, can be negative)\n"));
  printf(_("    Correction for the radio clock.\n"));
  printf(_("  -d <filename>\n"));
  printf(_("    Dump a copy of the samples sent to or received from\n"
           "    the radio.\n"));
  printf(_("  -e <fec[,fec]>  (default: h128,none)\n"));
  printf(_("    Inner and outer forward error correction codes to use.\n"));
  printf(_("  -f <frequency>  (default: 434000000 Hz)\n"));
  printf(_("    Frequency of the DSSS transmission.\n"));
  printf(_("  -g <gain>  (default: 0)\n"));
  printf(_("    Gain of the radio transceiver, or audio gain in dB.\n"));
  printf("  -h\n");
  printf(_("    This help.\n"));
  printf(_("  -i <id>  (default: \"\")\n"));
  printf(_("    Transfer id (at most 4 bytes). When receiving, the frames\n"
           "    with a different id will be ignored.\n"));
  printf(_("  -n <factor>  (default: 64, must be between 2 and 64)\n"));
  printf(_("    Spectrum spreading factor.\n"));
  printf(_("  -o <offset>  (default: 0 Hz, can be negative)\n"));
  printf(_("    Set the central frequency of the transceiver 'offset' Hz\n"
           "    lower than the signal frequency to send or receive.\n"));
  printf(_("  -r <radio>  (default: \"\")\n"));
  printf(_("    Radio to use.\n"));
  printf(_("  -s <sample rate>  (default: 2000000 S/s)\n"));
  printf(_("    Sample rate to use.\n"));
  printf(_("  -T <timeout>  (default: 0 s)\n"));
  printf(_("    Number of seconds after which reception will be stopped if\n"
           "    no frame has been received. A timeout of 0 means no timeout.\n"));
  printf("  -t\n");
  printf(_("    Use transmit mode.\n"));
  printf("  -v\n");
  printf(_("    Print debug messages.\n"));
  printf(_("  -w <delay>  (default: 0.0 s)\n"));
  printf(_("    Wait a little before switching the radio off.\n"
           "    This can be useful if the hardware needs some time to send\n"
           "    the last samples it has buffered.\n"));
  printf("\n");
  printf(_("By default the program is in 'receive' mode.\n"
           "Use the '-t' option to use the 'transmit' mode.\n"));
  printf("\n");
  printf(_("In 'receive' mode, the samples are received from the radio,\n"
           "and the decoded data is written either to 'filename' if it\n"
           "is specified, or to standard output.\n"
           "In 'transmit' mode, the data to send is read either from\n"
           "'filename' if it is specified, or from standard input,\n"
           "and the samples are sent to the radio.\n"));
  printf("\n");
  printf(_("Instead of a real radio transceiver, the 'io' radio type uses\n"
           "standard input in 'receive' mode, and standard output in\n"
           "'transmit' mode.\n"
           "The 'file=path-to-file' radio type reads/writes the samples\n"
           "from/to 'path-to-file'.\n"
           "The IQ samples must be in 'complex float' format\n"
           "(32 bits for the real part, 32 bits for the imaginary part).\n"
           "The audio samples must be in 'signed integer' format (16 bits).\n"));
  printf("\n");
  printf(_("The gain parameter can be specified either as an integer to set a\n"
           "global gain, or as a series of keys and values to set specific\n"
           "gains (for example 'LNA=32,VGA=20').\n"
           "When using the audio mode (with the '-a' option), the gain value\n"
           "in dB is applied to the audio samples.\n"));
  printf("\n");
  printf(_("Available radios (via SoapySDR):\n"));
  dsss_transfer_print_available_radios();
  printf("\n");
  printf(_("Available forward error correction codes:\n"));
  dsss_transfer_print_available_forward_error_codes();
}

void get_fec_schemes(char *str, char *inner_fec, char *outer_fec)
{
  unsigned int size = strlen(str);
  char spec[size + 1];
  char *separation;

  strcpy(spec, str);
  if((separation = strchr(spec, ',')) != NULL)
  {
    *separation = '\0';
  }

  if(strlen(spec) < 32)
  {
    strcpy(inner_fec, spec);
  }
  else
  {
    strcpy(inner_fec, "unknown");
  }

  if(separation != NULL)
  {
    if(strlen(separation + 1) < 32)
    {
      strcpy(outer_fec, separation + 1);
    }
    else
    {
      strcpy(outer_fec, "unknown");
    }
  }
  else
  {
    strcpy(outer_fec, "none");
  }
}

int main(int argc, char **argv)
{
  dsss_transfer_t transfer;
  char *radio_driver = "";
  unsigned int emit = 0;
  unsigned long int sample_rate = 2000000;
  unsigned int bit_rate = 100;
  unsigned long int frequency = 434000000;
  long int frequency_offset = 0;
  char *gain = "0";
  float ppm = 0;
  unsigned int spreading_factor = 64;
  char inner_fec[32];
  char outer_fec[32];
  char *id = "";
  char *file = NULL;
  char *dump = NULL;
  float final_delay = 0;
  unsigned int final_delay_sec = 0;
  unsigned int final_delay_usec = 0;
  unsigned int timeout = 0;
  unsigned char audio = 0;
  int opt;

  strcpy(inner_fec, "h128");
  strcpy(outer_fec, "none");

  setlocale(LC_ALL, "");
  setlocale(LC_NUMERIC, "C");
  bindtextdomain(PACKAGE, LOCALEDIR);
  textdomain(PACKAGE);

  while((opt = getopt(argc, argv, "ab:c:d:e:f:g:hi:n:o:r:s:T:tvw:")) != -1)
  {
    switch(opt)
    {
    case 'a':
      audio = 1;
      break;

    case 'b':
      bit_rate = strtoul(optarg, NULL, 10);
      break;

    case 'c':
      ppm = strtof(optarg, NULL);
      break;

    case 'd':
      dump = optarg;
      break;

    case 'e':
      get_fec_schemes(optarg, inner_fec, outer_fec);
      break;

    case 'f':
      frequency = strtoul(optarg, NULL, 10);
      break;

    case 'g':
      gain = optarg;
      break;

    case 'h':
      usage();
      return(EXIT_SUCCESS);

    case 'i':
      id = optarg;
      break;

    case 'n':
      spreading_factor = strtoul(optarg, NULL, 10);
      break;

    case 'o':
      frequency_offset = strtol(optarg, NULL, 10);
      break;

    case 'r':
      radio_driver = optarg;
      break;

    case 's':
      sample_rate = strtoul(optarg, NULL, 10);
      break;

    case 't':
      emit = 1;
      break;

    case 'T':
      timeout = strtoul(optarg, NULL, 10);
      break;

    case 'v':
      dsss_transfer_set_verbose(1);
      break;

    case 'w':
      final_delay = strtof(optarg, NULL);
      break;

    default:
      fprintf(stderr, _("Error: Unknown parameter: '-%c %s'\n"), opt, optarg);
      return(EXIT_FAILURE);
    }
  }
  if(optind < argc)
  {
    file = argv[optind];
  }
  else
  {
    file = NULL;
  }

  signal(SIGINT, &signal_handler);
  signal(SIGTERM, &signal_handler);
  signal(SIGABRT, &signal_handler);

  transfer = dsss_transfer_create(radio_driver,
                                  emit,
                                  file,
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
    fprintf(stderr, _("Error: Failed to initialize transfer\n"));
    return(EXIT_FAILURE);
  }
  dsss_transfer_start(transfer);
  if(final_delay > 0)
  {
    /* Give enough time to the hardware to send the last samples */
    final_delay_sec = floorf(final_delay);
    final_delay_usec = (final_delay - final_delay_sec) * 1000000;
    if(final_delay_sec > 0)
    {
      sleep(final_delay_sec);
    }
    if(final_delay_usec > 0)
    {
      usleep(final_delay_usec);
    }
  }
  dsss_transfer_free(transfer);

  if(dsss_transfer_is_verbose())
  {
    fprintf(stderr, "\n");
  }

  return(EXIT_SUCCESS);
}
