//
// Raspberry Pi controlled car
//
// Written by Larry Bank
// Project started - 8/1/2017
// Copyright (c) 2017 BitBank Software, Inc.
// bitbank@pobox.com
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <SDL2/SDL.h>
#include <pigpio.h>
int fd;
volatile int iJoyBits;
static SDL_Joystick *joy0;
// axis index
#define X_AXIS 0
#define Y_AXIS 1
static int iJoyAxis[8];
volatile int iLSpeed, iRSpeed; // left and right side motor speeds
volatile int iLCount, iRCount; // opto switch counters
static int iPWML0, iPWML1, iPWMR0, iPWMR1;
static int iOptoL, iOptoR;
// Header pins used to run the motor controller
#define LEFTGPIO_PLUS 11
#define LEFTGPIO_MINUS 13
#define RIGHTGPIO_PLUS 15
#define RIGHTGPIO_MINUS 16
#define LEFT_OPTO 18
#define RIGHT_OPTO 22

// Table to translate header pin numbers into PIGPIO GPIO numbers
static unsigned char ucPIGPins[] = {0xff,0xff,0xff,2,0xff,3,0xff,4,14,0xff,15,
                       17,18,27,0xff,22,23,0xff,24,10,0xff,9,25,11,8,0xff,7,0,1,
                       5,0xff,6,12,13,0xff,19,16,26,20,0xff,21};
//
// Callback (interrupt) function to count pulses from the opto switches
//
void CounterCallback(int iGPIO, int iLevel, uint32_t u32Tick)
{
	if (iGPIO == iOptoL) // which sensor triggered?
		iLCount++;
	else
		iRCount++;
} /* CounterCallback() */
 
//
// Return the current time in milliseconds
//
int MilliTime()
{
int iTime;
struct timespec res;

    clock_gettime(CLOCK_MONOTONIC, &res);
    iTime = 1000*res.tv_sec + res.tv_nsec/1000000;

    return iTime;
} /* MilliTime() */

//
// Wheels are 65mm in diameter (204.2mm circumference)
// Each wheel encoder has 20 slots = 10.21mm per tick
// Convert the number of ticks into meters per second
//
float WheelSpeed(int iTicks, int iDeltaTime)
{
float fSpeed;

	if (iDeltaTime < 0) return 0.0f; // first time through

	fSpeed = (float)iTicks * 10.21f; // distance traveled
	fSpeed = fSpeed / (float)iDeltaTime; // millimeters / milliseconds
	return fSpeed;
} /* WheelSpeed() */

//
// Convert joystick axis position into L/R speed values (+/- 0-255)
//
// The gamepad axis can go from neutral (0) to max up (-32767) 
// to max down (+32767)
// We convert this value into 256 forward and 256 reverse speeds
//
void JoyToSpeed(void)
{
int i;
#define SPEED_SCALE 128

   iLSpeed = iRSpeed = (iJoyAxis[Y_AXIS]  / SPEED_SCALE); // +/- 0-255 forward motion
// Now adjust the L/R speed to allow turning the car
   if (iJoyAxis[X_AXIS] != 0)
   {
      i = iJoyAxis[X_AXIS] / SPEED_SCALE;
      if (i < 0) // user wants to go left, so slow L wheels
      {
         iLSpeed += i;
         if (iLSpeed < -255) iLSpeed = -255;
      }
      else // must want to turn right, slow R wheels
      {
         iRSpeed -= i;
         if (iRSpeed < -255) iRSpeed = -255;
      }
   }  
} /* JoyToSpeed() */

//
// Set the GPIO PWM pins to generate the desired speed and direction
// There are 2 digital inputs for each output of the L298N motor controller
// 00 = off
// 10 = forward
// 01 = reverse
//
// These bits are pulse-width modulated (PWM) to adjust the speed since
// the motor controller doesn't control the current flowing through it.
// The PIGPIO library translates the value 0-255 into the duty cycle of the PWM.
// When the motor is about to start moving, but doesn't have enough total energy
// you can hear the PWM signal 'ringing' through the motor's magnetic coils.
//
void SetSpeed(void)
{

	if (iLSpeed == 0)
	{
		gpioPWM(iPWML0, 0); // turn off both outputs
		gpioPWM(iPWML1, 0);
	}
	else if (iLSpeed < 0) // backwards
	{
		gpioPWM(iPWML0, 0 - iLSpeed); // 0-255
		gpioPWM(iPWML1, 0);
	}
	else
	{
		gpioPWM(iPWML0, 0);
		gpioPWM(iPWML1, iLSpeed);
	}
	if (iRSpeed == 0)
	{
		gpioPWM(iPWMR0, 0);
		gpioPWM(iPWMR1, 0); // both off
	}
	else if (iRSpeed < 0) // backwards
	{
		gpioPWM(iPWMR0, 0 - iRSpeed);
		gpioPWM(iPWMR1, 0);
	}
	else
	{
		gpioPWM(iPWMR0, 0);
		gpioPWM(iPWMR1, iRSpeed);
	}
} /* SetSpeed() */

//
// This is the callback function that SDL calls when there is a change
// to the gamepad buttons or axes.
// It saves the changes to global variables for buttons and axes positions
//
static int SG_SDLEventFilter(void *userdata, SDL_Event *event)
{

        if (event->type == SDL_JOYBUTTONDOWN || event->type == SDL_JOYBUTTONUP)
        {
         uint32_t ulMask = 0;

         SDL_JoyButtonEvent *jbe = (SDL_JoyButtonEvent *)event;
         if (jbe->button >= 0 && jbe->button < 8) // we only use 8 buttons
         {
            ulMask = 1 << (jbe->button);
            if (jbe->state == SDL_PRESSED)
               iJoyBits |= ulMask;
            else
               iJoyBits &= ~ulMask;
         }
        }
        else if (event->type == SDL_JOYAXISMOTION)
        {
            SDL_JoyAxisEvent *jae = (SDL_JoyAxisEvent *)event;
            iJoyAxis[jae->axis] = jae->value;
        }
	return 1;
} /* SG_SDLEventFilter() */

//
// Initialize everything needed to get our car running
//
int Setup(void)
{
int i;
	if (gpioInitialise() < 0)
        {
                printf("pigpio failed to initialize\n");
                return 0;
        }
	// Set up the joystick input
	SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
	if (SDL_Init(SDL_INIT_JOYSTICK))
	{
		printf("Could not initialize SDL\n");
		return 0;
	}
	iJoyBits = 0;
	SDL_SetEventFilter(SG_SDLEventFilter, NULL);
	printf("number of joysticks = %d\n", SDL_NumJoysticks()); 
	joy0 = SDL_JoystickOpen(0);	
	if (joy0 != NULL)
	{
		printf("joystick opened\n");	
		SDL_JoystickEventState(SDL_ENABLE);
	}
	else
	{
		printf("joystick failed to open\n");
		return 0;
	}
// Set up the PIGPIO pin numbers
// PWM motor outputs
   iPWML0 = ucPIGPins[LEFTGPIO_PLUS];
   iPWML1 = ucPIGPins[LEFTGPIO_MINUS];
   iPWMR0 = ucPIGPins[RIGHTGPIO_PLUS];
   iPWMR1 = ucPIGPins[RIGHTGPIO_MINUS];

// Opto interrupter switches
   iOptoL = ucPIGPins[LEFT_OPTO];
   iOptoR = ucPIGPins[RIGHT_OPTO];
   iLCount = iRCount = 0; // tick counters

// Add interrupt callback for the 2 opto sensors
   i = gpioSetISRFunc(iOptoL, RISING_EDGE, 0, CounterCallback);
   if (i != 0) printf("Error registering left opto callback\n");
   i = gpioSetISRFunc(iOptoR, RISING_EDGE, 0, CounterCallback); 
   if (i != 0) printf("Error registering right opto callback\n");

   return 1; // success
} /* Setup() */

//
// Clean up before exiting
// PIGPIO needs to be properly terminated or it won't allow
// re-initialization.
//
void Terminate(void)
{
// remove the interrupt callback from the 2 opto sensor inputs
   gpioSetISRFunc(iOptoL, RISING_EDGE, 0, NULL);
   gpioSetISRFunc(iOptoR, RISING_EDGE, 0, NULL);

   gpioTerminate();
} /* Terminate() */

//
// Program entry point
//
int main(int argc, char **argv)
{
SDL_Event event;
int iTime, i, iDeltaTime;
float fLSpeed, fRSpeed;
int iTick;

	if (Setup() == 0) goto quit;
	iTime = iTick = 0;
	while (iJoyBits == 0) // quit when a button is pressed (for now)
	{
		while (SDL_PollEvent(&event)) {}; // process any queued events
		JoyToSpeed(); // convert position to speed
		SetSpeed(); // update the motor control pins
		i = MilliTime();
		iDeltaTime = i - iTime; // new delta
		iTime = i;
		fLSpeed = WheelSpeed(iLCount, iDeltaTime);
		fRSpeed = WheelSpeed(iRCount, iDeltaTime);
		iLCount = iRCount = 0;
		if ((iTick & 7) == 0) // update 4x per second
			printf("L speed %01.3f M/s, R speed %01.3f\n", fLSpeed, fRSpeed);
		usleep(33000); // update total control loop at around 30x per second
		iTick++;
	}
quit:
	Terminate();

return 0;
} /* main() */
