/*
 * PackageLicenseDeclared: Apache-2.0
 * Copyright (c) 2015 ARM Limited
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// This is a purely visual test of MINAR, it blinks two LEDs connected
// on LED1 (each 500ms) and LED2 (each 1000ms)

#include <stdio.h>

#include "mbed.h"
#include "minar/minar.h"
#include "mbed-util/FunctionPointer.h"

using mbed::util::FunctionPointer0;

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

