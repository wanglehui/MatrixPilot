// This file is part of MatrixPilot.
//
//    http://code.google.com/p/gentlenav/
//
// Copyright 2009-2011 MatrixPilot Team
// See the AUTHORS.TXT file for a list of authors of MatrixPilot.
//
// MatrixPilot is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// MatrixPilot is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with MatrixPilot.  If not, see <http://www.gnu.org/licenses/>.


#include "defines.h"
#include "navigate.h"
#include "behaviour.h"
#include "servoPrepare.h"
#include "config.h"
#include "states.h"
#include "airspeedCntrl.h"
#include "altitudeCntrl.h"
#include "helicalTurnCntrl.h"
#include "../libUDB/servoOut.h"
#include "../libDCM/rmat.h"

//  If the state machine selects pitch feedback, compute it from the pitch gyro and accelerometer.

#define ANGLE_90DEG (RMAX/(2*57.3)) // FIXME: never used
#define RTLKICK ((int32_t)(RTL_PITCH_DOWN*(RMAX/57.3)))
#define INVNPITCH ((int32_t)(INVERTED_NEUTRAL_PITCH*(RMAX/57.3)))
#define HOVERPOFFSET ((int32_t)(hover.HoverPitchOffset*(RMAX/57.3)))
#define HOVERPTOWP ((int32_t)(hover.HoverPitchTowardsWP*(RMAX/57.3)))

uint16_t pitchgain;
uint16_t pitchkd;
uint16_t hoverpitchgain;
uint16_t hoverpitchkd;
uint16_t rudderElevMixGain;
uint16_t rollElevMixGain;

int16_t pitchrate;
int16_t navElevMix;
int16_t elevInput;

static void normalPitchCntrl(void);
static void hoverPitchCntrl(void);

void init_pitchCntrl(void)
{
	pitchgain = (uint16_t)(gains.Pitchgain*RMAX);
	pitchkd = (uint16_t) (gains.PitchKD*SCALEGYRO*RMAX);
	hoverpitchgain = (uint16_t)(hover.HoverPitchGain*RMAX);
	hoverpitchkd = (uint16_t) (hover.HoverPitchKD*SCALEGYRO*RMAX);
	rudderElevMixGain = (uint16_t)(RUDDER_ELEV_MIX*RMAX);
	rollElevMixGain = (uint16_t)(ROLL_ELEV_MIX*RMAX);
}

void save_pitchCntrl(void)
{
	gains.Pitchgain      = (float)pitchgain         / (RMAX);
	gains.PitchKD        = (float)pitchkd           / (SCALEGYRO*RMAX);
	hover.HoverPitchGain = (float)hoverpitchgain    / (RMAX);
	hover.HoverPitchKD   = (float)hoverpitchkd      / (SCALEGYRO*RMAX);
	gains.RudderElevMix  = (float)rudderElevMixGain / (RMAX);
	gains.RollElevMix    = (float)rollElevMixGain   / (RMAX);
}

void pitchCntrl(void)
{
	if (canStabilizeHover() && desired_behavior._.hover)
	{
		hoverPitchCntrl();
	}
	else
	{
		normalPitchCntrl();
	}
}

static void normalPitchCntrl(void)
{
	union longww pitchAccum;
	int16_t rtlkick;
//	int16_t aspd_adj;
//	fractional aspd_err, aspd_diff;
	fractional rmat6;
	fractional rmat7;
	fractional rmat8;

#ifdef TestGains
	state_flags._.GPS_steering = 0; // turn navigation off
	state_flags._.pitch_feedback = 1; // turn stabilization on
#endif
	if (!canStabilizeInverted() || current_orientation != F_INVERTED)
	{
		rmat6 = rmat[6];
		rmat7 = rmat[7];
		rmat8 = rmat[8];
	}
	else
	{
		rmat6 = -rmat[6];
		rmat7 = -rmat[7];
		rmat8 = -rmat[8];
		pitchAltitudeAdjust = -pitchAltitudeAdjust - INVNPITCH;
	}
	navElevMix = 0;
	if (state_flags._.pitch_feedback)
	{
		if (RUDDER_OUTPUT_CHANNEL != CHANNEL_UNUSED && RUDDER_INPUT_CHANNEL != CHANNEL_UNUSED) {
			pitchAccum.WW = __builtin_mulsu(rmat6, rudderElevMixGain) << 1;
			pitchAccum.WW = __builtin_mulss(pitchAccum._.W1,
			    REVERSE_IF_NEEDED(RUDDER_CHANNEL_REVERSED, udb_pwTrim[RUDDER_INPUT_CHANNEL] - udb_pwOut[RUDDER_OUTPUT_CHANNEL])) << 3;
			navElevMix += pitchAccum._.W1;
		}
		pitchAccum.WW = __builtin_mulsu(rmat6, rollElevMixGain) << 1;
		pitchAccum.WW = __builtin_mulss(pitchAccum._.W1, rmat[6]) >> 3;
		navElevMix += pitchAccum._.W1;
	}
	pitchAccum.WW = (__builtin_mulss(rmat8, omegagyro[0])
	               - __builtin_mulss(rmat6, omegagyro[2])) << 1;
	pitchrate = pitchAccum._.W1;
	if (!udb_flags._.radio_on && state_flags._.GPS_steering)
	{
		rtlkick = RTLKICK;
	}
	else
	{
		rtlkick = 0;
	}
#if (GLIDE_AIRSPEED_CONTROL == 1)
	fractional aspd_pitch_adj = gliding_airspeed_pitch_adjust();
#endif
	if (PITCH_STABILIZATION && state_flags._.pitch_feedback)
	{
#if (GLIDE_AIRSPEED_CONTROL == 1)
		pitchAccum.WW = __builtin_mulsu(rmat7 - rtlkick + aspd_pitch_adj + pitchAltitudeAdjust, pitchgain) 
		              + __builtin_mulus(pitchkd, pitchrate);
#else
		pitchAccum.WW = __builtin_mulsu(rmat7 - rtlkick + pitchAltitudeAdjust, pitchgain) 
		              + __builtin_mulus(pitchkd, pitchrate);
#endif
	}
	else
	{
		pitchAccum.WW = 0;
	}
	pitch_control = (int32_t)pitchAccum._.W1 + navElevMix;
}

static void hoverPitchCntrl(void)
{
	union longww pitchAccum;
	int16_t elevInput;
	int16_t manualPitchOffset;
	int32_t pitchToWP;

	if (state_flags._.pitch_feedback)
	{
		pitchAccum.WW = (__builtin_mulss(-rmat[7], omegagyro[0])
		               - __builtin_mulss( rmat[6], omegagyro[1])) << 1;
		pitchrate = pitchAccum._.W1;
		elevInput = (udb_flags._.radio_on == 1) ?
		    REVERSE_IF_NEEDED(ELEVATOR_CHANNEL_REVERSED, udb_pwIn[ELEVATOR_INPUT_CHANNEL] - udb_pwTrim[ELEVATOR_INPUT_CHANNEL]) : 0;
		manualPitchOffset = elevInput * (int16_t)(RMAX/600);
		if (state_flags._.GPS_steering)
		{
			pitchToWP = (tofinish_line > hover.HoverNavMaxPitchRadius) ?
			    HOVERPTOWP : (HOVERPTOWP / hover.HoverNavMaxPitchRadius* tofinish_line);
		}
		else
		{
			pitchToWP = 0;
		}
		pitchAccum.WW = __builtin_mulsu(rmat[8] + HOVERPOFFSET - pitchToWP + manualPitchOffset, hoverpitchgain)
		              + __builtin_mulus(hoverpitchkd, pitchrate);
	}
	else
	{
		pitchAccum.WW = 0;
	}
	pitch_control = (int32_t)pitchAccum._.W1;
}
