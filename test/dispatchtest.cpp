// Copyright (C) 2014 ARM Limited. All rights reserved.

// This is a purely visual test of MINAR, it blinks two LEDs connected
// on LED1 (each 500ms) and LED2 (each 1000ms)

#include <stdio.h>

#include "mbed.h"
#include "minar/minar.h"

static DigitalOut led1(LED1);
static DigitalOut led2(LED2);

static void toggleLED1()
{
    led1 = !led1;
}

static void toggleLED2()
{
    led2 = !led2;
}

void app_start(int, char*[])
{
    printf("Test starting\r\n");

    minar::Scheduler::postCallback(FunctionPointer0<void>(toggleLED1).bind())
        .period(minar::milliseconds(500))
        .tolerance(minar::milliseconds(10));

    minar::Scheduler::postCallback(FunctionPointer0<void>(toggleLED2).bind())
        .period(minar::milliseconds(250))
        .tolerance(minar::milliseconds(10));
}

