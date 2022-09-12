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

#ifndef DSSSFRAME_H
#define DSSSFRAME_H

#include <liquid/liquid.h>

// create DSSS frame generator with specific parameter
//  _n       :   spreading factor
//  _props   :   frame properties (FEC, etc.)
dsssframegen dsssframegen_create_set(unsigned int _n,
                                     dsssframegenprops_s * _props);

// create DSSS frame synchronizer
//  _n          :   spreading factor
//  _callback   :   callback function
//  _userdata   :   user data pointer passed to callback function
dsssframesync dsssframesync_create_set(unsigned int _n,
                                       framesync_callback _callback,
                                       void * _userdata);

#endif
