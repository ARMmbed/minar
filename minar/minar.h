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

#ifndef __MINAR_MINAR_H__
#define __MINAR_MINAR_H__

#include "compiler-polyfill/attributes.h"
#include "minar-platform/minar_platform.h"
//#include "mbed.h"
// [TODO] change this
#include "core-util/Event.h"
#include "core-util/FunctionPointer.h"

namespace minar{

/// @name Types
enum Constants{
    /// Number of callbacks to look ahead when trying to find the optimal thing
    /// to execute
    Optimise_Lookahead = 5,
    // warn if callbacks take longer than this to execute
    Warn_Duration_Milliseconds = 10,
    // warn if a callback cannot be executed this long after it should have
    // been because necessary resources are not available
    Warn_Late_Milliseconds = 100,
    // warn if the event loop is lagging (all callbacks are being executed late
    // because there is too much to do) by more than this
    Warn_Lag_Milliseconds = 500,
};

/// Basic callback type
typedef mbed::util::Event callback_t;

/// Internal time type
typedef platform::tick_t tick_t;

/// Handle onto scheduled callbacks
typedef void* callback_handle_t;

class SchedulerData;

class Scheduler{
    private:
        class CallbackAdder{
            friend class Scheduler;
            public:
                CallbackAdder& delay(tick_t delay);
                CallbackAdder& tolerance(tick_t tolerance);
                CallbackAdder& period(tick_t tolerance);

                callback_handle_t getHandle();

                ~CallbackAdder();

            private:
                CallbackAdder(Scheduler& sched, callback_t cb);

                Scheduler&   m_sched;
                callback_t   m_cb;
                tick_t       m_tolerance;
                tick_t       m_delay;
                tick_t       m_period;
                bool         m_posted;
        };
    public:
        // get the global scheduler instance
        // The scheduler will be automatically initialised the first time it is
        // referenced.
        // It is not currently possible to de-initialize the scheduler after it
        // has been initialised (it will exist for the lifetime of the
        // program).
        static Scheduler* instance();

        /// start the scheduler, never returns in normal operation. The return type is
        /// (int) so that the end of a program's main function can be:
        ///      ...
        ///      return sched.start();
        ///   }
        static int start();

        /// stop the scheduler, (even if there is still work to do), returns the number
        /// of items in the scheduling queue. This function should not normally be
        /// used, and is only provided as a convenience for writing tests.
        static int stop();

        // Function for posting callback with bound argument(s)
        // usage: postCallback([&]{ ...  // }).withDelay(...).requiring(...).releasing(...);
        static CallbackAdder postCallback(callback_t const& cb);


        // Function for posting callbacks using FunctionPointer objects without arguments
        static CallbackAdder postCallback(mbed::util::FunctionPointer& callback)
        {
            return postCallback(callback.bind());
        }

        // Functions for posting callbacks to direct function pointers
        // and objects/member pointers without arguments
        static CallbackAdder postCallback(void (*callback)(void))
        {
            return postCallback(mbed::util::FunctionPointer(callback).bind());
        }

        template<typename T>
        static CallbackAdder postCallback(T *object, void (T::*member)())
        {
            return postCallback(mbed::util::FunctionPointer(object, member).bind());
        }

        static int cancelCallback(callback_handle_t handle);

        static tick_t getTime();

    private:


        Scheduler();

        // [FPTR] this was a unique_ptr, what's the consequence of making it a simple pointer?
        SchedulerData* data;
};

/// @name Time

/// convert milliseconds into the internal "ticks" time representation
tick_t milliseconds(uint32_t miliseconds);

/// convert ticks to milliseconds
uint32_t ticks(tick_t ticks);


/// Return the scheduled execution time of the current callback. This lags
/// behind the wall clock time if the system is busy.
///
/// Note that this time is NOT monotonic. If callbacks are executed in an order
/// different to their scheduled order because of the resources they need, then
/// this time will jump backwards.
tick_t getTime();


} // namespace minar

#endif // ndef __MINAR_MINAR_H__
