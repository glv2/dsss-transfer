#!/bin/sh

# This file is part of dsss-transfer, a program to send or receive data
# by software defined radio using the DSSS modulation.
#
# Copyright 2022 Guillaume LE VAILLANT
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

set -e

DSSS_TRANSFER=../src/dsss-transfer
MESSAGE=$(mktemp -t message.XXXXXX)
DECODED=$(mktemp -t decoded.XXXXXX)
SAMPLES=$(mktemp -t samples.XXXXXX)

echo "This is a test transmission using dsss-transfer." > ${MESSAGE}

check_ok_io()
{
    NAME=$1
    OPTIONS1=$2
    OPTIONS2=$3

    echo "Test: ${NAME}"
    ${DSSS_TRANSFER} -t -r io ${OPTIONS1} ${MESSAGE} > ${SAMPLES}
    ${DSSS_TRANSFER} -r io ${OPTIONS2} ${DECODED} < ${SAMPLES}
    diff -q ${MESSAGE} ${DECODED} > /dev/null
}

check_ok_file()
{
    NAME=$1
    OPTIONS1=$2
    OPTIONS2=$3

    echo "Test: ${NAME}"
    ${DSSS_TRANSFER} -t -r file=${SAMPLES} ${OPTIONS1} ${MESSAGE}
    ${DSSS_TRANSFER} -r file=${SAMPLES} ${OPTIONS2} ${DECODED}
    diff -q ${MESSAGE} ${DECODED} > /dev/null
}

check_nok_io()
{
    NAME=$1
    OPTIONS1=$2
    OPTIONS2=$3

    echo "Test: ${NAME}"
    ${DSSS_TRANSFER} -t -r io ${OPTIONS1} ${MESSAGE} > ${SAMPLES}
    ${DSSS_TRANSFER} -r io ${OPTIONS2} ${DECODED} < ${SAMPLES}
    ! diff -q ${MESSAGE} ${DECODED} > /dev/null
}

check_nok_file()
{
    NAME=$1
    OPTIONS1=$2
    OPTIONS2=$3

    echo "Test: ${NAME}"
    ${DSSS_TRANSFER} -t -r file=${SAMPLES} ${OPTIONS1} ${MESSAGE}
    ${DSSS_TRANSFER} -r file=${SAMPLES} ${OPTIONS2} ${DECODED}
    ! diff -q ${MESSAGE} ${DECODED} > /dev/null
}

check_ok_io "Default parameters" "" ""
check_ok_io "Bit rate 1200" "-b 1200" "-b 1200"
check_ok_file "Bit rate 9600" "-b 9600" "-b 9600"
check_ok_io "Bit rate 50" "-b 50" "-b 50"
check_nok_io "Wrong bit rate 100 150" "-b 100" "-b 150"
check_ok_io "Frequency offset 200000" "-o 200000" "-o 200000"
check_ok_file "Frequency offset -123456" "-o -123456" "-o -123456"
check_nok_io "Wrong frequency offset 200000 250000" "-o 200000" "-o 250000"
check_ok_io "Sample rate 4000000" "-s 4000000" "-s 4000000"
check_ok_file "Sample rate 10000000" "-s 10000000" "-s 10000000"
check_nok_io "Wrong sample rate 1000000 2000000" "-s 1000000" "-s 2000000"
check_ok_io "Spreading factor 2" "-n 2" "-n 2"
check_ok_file "Spreading factor 10" "-n 10" "-n 10"
check_nok_io "Wrong spreading factor 30 29" "-n 30" "-n 29"
check_ok_io "FEC Hamming(7/4)" "-e h74" "-e h74"
check_ok_file "FEC Golay(24/12) and repeat(3)" "-e g2412,rep3" "-e g2412,rep3"
check_ok_io "Id a1B2" "-i a1B2" "-i a1B2"
check_nok_file "Wrong id ABCD ABC" "-i ABCD" "-i ABC"
check_ok_file "Audio frequency 1500" \
              "-a -s 48000 -f 1500 -b 30" \
              "-a -s 48000 -f 1500 -b 30"
check_nok_io "Wrong audio frequency 1500 2500" \
             "-a -s 48000 -f 1500 -b 30" \
             "-a -s 48000 -f 2500 -b 30"
check_ok_io "Audio gain -20" \
            "-a -s 48000 -f 1500 -b 30 -g -20" \
            "-a -s 48000 -f 1500 -b 30"

dd if=/dev/random of=${MESSAGE} bs=1000 count=200 status=none
check_ok_file "Bit rate 8000000, sample rate 100000000, spreading 8" \
              "-s 100000000 -n 8 -b 8000000" \
              "-s 100000000 -n 8 -b 8000000"

rm -f ${MESSAGE} ${DECODED} ${SAMPLES}
echo "All tests passed."
