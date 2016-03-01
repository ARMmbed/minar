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

#include <stdio.h>
#include "minar/minar.h"
#include "mbed-drivers/mbed.h"
#include "greentea-client/test_env.h"
#include "unity/unity.h"
#include "core-util/FunctionPointer.h"

using mbed::util::FunctionPointer0;
using mbed::util::FunctionPointer1;

// Minimum and maximum allowed values of "cnt", computed in accordance with the
// various periods of tolerances of events in main(). TODO: actually check these
// values by running on different platforms
#define MIN_ALLOWED_CNT         46
#define MAX_ALLOWED_CNT         50

#define EXPECTED_CALLBACK_COUNT 1

static int cnt;

class LED {
public:
    LED(const char* name, PinName led): _name(name), _led(led) {}

    void toggle(void) {
        _led = !_led;
    }

    void callback_no_increment(void) {
        printf("%s callback tick... \r\n", _name);
        toggle();
    }

    void callback_and_increment(void) {
        printf("%s callback tick and increment... %d\r\n", _name, cnt++);
        toggle();
    }

private:
    const char *_name;
    DigitalOut _led;
};

static void cb_msg_and_increment(const char *msg) {
    printf("%s...%d\r\n", msg, cnt++);
}

static void stop_scheduler() {
    printf("Stopping scheduler...\r\n");
    minar::Scheduler::stop();
}

void app_start(int, char*[]) {
    GREENTEA_SETUP(35, "default");
    
    LED led1("led1", LED1);
    LED led2("led2", LED2);

    led1.toggle();

    // The next callback will run once
    minar::Scheduler::postCallback(FunctionPointer0<void>(&led1, &LED::callback_no_increment).bind())
        .delay(minar::milliseconds(500))
        .tolerance(minar::milliseconds(100));

    // The next callback will be the only periodic one
    minar::Scheduler::postCallback(FunctionPointer0<void>(&led2, &LED::callback_and_increment).bind())
        .period(minar::milliseconds(650))
        .tolerance(minar::milliseconds(100));

    FunctionPointer1<void, const char*> fp(cb_msg_and_increment);
    // Schedule this one to run after a while
    minar::Scheduler::postCallback(fp.bind("postCallbackWithDelay..."))
        .delay(minar::milliseconds(5000))
        .tolerance(minar::milliseconds(200));

    // Schedule this one to run immediately
    minar::Scheduler::postCallback(fp.bind("postImmediate"))
        .tolerance(minar::milliseconds(200));

    // Stop the scheduler after enough time has passed
    minar::Scheduler::postCallback(stop_scheduler)
        .delay(minar::milliseconds(30000))
        .tolerance(minar::milliseconds(3000));

    int cb_cnt = minar::Scheduler::start(); // this will return after stop_scheduler above is executed

    // After returning, there should be only one event in the queue (the periodic one)

    bool cnt_ok = (cnt >= MIN_ALLOWED_CNT) && (cnt <= MAX_ALLOWED_CNT);
    printf("Final counter value: %d\r\n", cnt);
    TEST_ASSERT_TRUE_MESSAGE(cnt_ok, "Counter value is out of range");
    TEST_ASSERT_EQUAL_MESSAGE(EXPECTED_CALLBACK_COUNT, cb_cnt, "Wrong call back count!");

    GREENTEA_TESTSUITE_RESULT(cnt_ok && (cb_cnt == EXPECTED_CALLBACK_COUNT));
}

