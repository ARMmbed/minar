# MINAR scheduler

MINAR is an *event scheduler*. Applications instruct MINAR to execute blocks of code called *events* (either immediately, or in the future), and MINAR decides when to run those blocks based on their scheduled execution time. When there's nothing to execute, MINAR puts the MCU to sleep.

A (very) simplified view of MINAR's main loop can be found below (we'll improve it later):

```
while (true) {
    while (event_available_to_schedule())
        schedule_event();
    sleep();
}
```

## Events

In MINAR, an *event* is a pointer to a function, plus a specific binding of the function's arguments. The event is created from a [FunctionPointer](https://github.com/ARMmbed/core-util/blob/master/core-util/FunctionPointer.h) by calling its `bind` method. Although the `FunctionPointer` implementation was largely rewritten in mbed OS, it serves the same purpose as it did in mbed Classic: it keeps a pointer to a function or a method. Usage example:

```
#include "mbed.h"

void f0(void) {
}

int f1(const char *arg) {
}

class A {
public:
    void m(int a) {
    }
}

void test() {
    A a;
    FunctionPointer0<void> ptr_to_f0(f0);
    FunctionPointer1<int, const char*> ptr_to_f1(f1);
    FunctionPointer1<void, int> ptr_to_m(&a, &A::m);

    ptr_to_f0.call();
    ptr_to_f1("test"); // you can also call directly, without 'call'
    ptr_to_m.call(0); // or simply 'ptr_to_m(0);'
}
```

At the moment, there are implementations of `FunctionPointer` for functions without an argument (`FunctionPointer0`), as well as functions with one argument (`FunctionPointer1`), two arguments (`FunctionPointer2`) and three arguments (`FunctionPointer3`).

In order to create an event from a function pointer, its `bind` method needs to be called. `bind` takes a set of fixed values for the function's arguments (if the function has arguments) and creates a [FunctionPointerBind](https://github.com/ARMmbed/core-util/blob/master/core-util/FunctionPointerBind.h) instance. `FunctionPointerBind` keeps a copy of those fixed values and allows us to call the function later with those fixed arguments without having to specify them again. This is best explained by an example. Building on top of the code above:

```
// FunctionPointerBind is templated on the return type of the bound function
FunctionPointerBind<void> bind_of_f0(ptr_to_f0.bind()); // f0 doesn't take any arguments
FunctionPointerBind<int> bind_of_f1(ptr_to_f1.bind("test")); // bind the argument "test" to this FunctionPointerBind
FunctionPointerBind<void> bind1_of_m(ptr_to_m.bind(0)); // bind 0 to this FunctionPointerBind
FunctionPointerBind<void> bind2_of_m(ptr_to_m.bind(10)); // bind 10 to this FunctionPointerBind

bind_of_f0.call(); // equivalent to ptr_to_f0.call()
bind_of_f1();      // equivalent to ptr_to_f1.call("test")
bind1_of_m();      // equivalent to ptr_to_m.call(0)
bind2_of_m();      // equivalent to ptr_to_m.call(10)
```
(Many more examples involving `FunctionPointer` and `Event` can be found [here](https://github.com/ARMmbed/core-util/blob/master/test/EventHandler/main.cpp))

The size of storage for the argument's values in `FunctionPointerBind` is fixed, which means that all `FunctionPointerBind` instances have the same size in memory. If the combined size of the arguments of `bind` is larger than the size of storage in `FunctionPointerBind`, you'll get a compiler error.

A MINAR [Event](https://github.com/ARMmbed/core-util/blob/master/core-util/Event.h) is simply a `FunctionPointerBind` for functions that don't return any arguments:

```
typedef FunctionPointerBind<void> Event;
```

In conclusion, using the mechanisms shown above, you can schedule any kind of function with various argument(s) by instantiating the proper `FunctionPointer` class with that function and then calling `bind` on the `FunctionPointer` instance. This will work so long as the function doesn't return anything and the total storage space required for its arguments is less than the fixed storage size in `FunctionPointerBind`.

## Using events

To actually schedule an event, you call the `postCallback` function in MINAR. Building on the code above:

```
minar::Scheduler::postCallback(bind_of_f0);
// note that f1 above can't be an event, since it returns something
```

`postCallback` adds `bind_of_f0` into the MINAR event queue, scheduling it to be executed as soon as possible. When calling `postCallback` you can specify a few more attributes for the event:

- `period`: the event will run periodically, with the specified interval.
- `delay`: the event will be executed after the specified delay.
- `tolerance`: the tolerance for the event's execution time.

Periods, delays and tolerances are expressed in _ticks_. Ticks are an internal MINAR type and the actual duration of a tick depends on the platform on which MINAR is running, so using ticks directly is not recommended. You can convert from ticks to milliseconds by calling `minar::milliseconds`.

A tolerance is necessary for the efficient scheduling of callbacks. By providing a tolerance, it permits minar to group callbacks together if they have overlapping execution schedules and tolerances. This permits minar to schedule several callbacks in a single wakeup even if there is time between their desired execution times. It's important to provide minar with realistic tolerances, since large tolerances will improve power efficiency. For example, network code can accept significant delays without reduction in performance. The default value of `tolerance` is 50 milliseconds.

Period, delay and tolerance can be specified in any order. Some examples:

```
void f() {
}
Event e(FunctionPointer0<void>(f).bind());

// Schedule to execute as soon as possible
minar::Scheduler::postCallback(e);
// Schedule to execute after 100ms
minar::Scheduler::postCallback(e).delay(minar::milliseconds(100));
// Schedule to execute each 500ms with an initial delay of 10ms
minar::Scheduler::postCallback(e).period(minar::milliseconds(500)).delay(minar::milliseconds(10))
// Schedule to execute each 100ms, with a tolerance of 2ms
minar::Scheduler::postCallback(e).tolerance(minar::milliseconds(2)).period(minar::milliseconds(100));
```

With this in mind, we can now construct a better (but still simplified) pseudo-code representation of MINAR's event loop:

```
while(true) {
	// Look at the next event in the queue:
	// The next event to execute (sorted by execution time) is always
	// located at the top of the queue
	next = peekNext();
	now_plus_tolerance = now + next->tolerance; // consider scheduling tolerance

	if(timeIsInPeriod(last_dispatch, next->call_before, now_plus_tolerance)) {
		// The next event in the queue is due, execute it now
		next->call(); // actual event execution

		if(!next->period) {
			pop(next); // we're done with this event
		} else {
			reschedule(next); // periodic event, re-schedule it
		}
	} else {
		// Nothing to do for now, so go to sleep until the next event
		// in the queue in due
		sleepFromUntil(now, peekNext()->call_before);
	}
}
```

The above pseudo-code sequence should be enough to explain a very important feature of MINAR: **MINAR is not a pre-emptive scheduler**. The user events execute uninterrupted (`next->call()` above); control goes back to MINAR's event loop when the event exits. No pre-emption means that you won't have to worry about complex, hard to understand synchronization issues between different parts of your application, like you'd have to do in a traditional RTOS. However, it also means that **MINAR is not a real time scheduler**. If your callbacks take a lot of time to execute, some other callbacks in the system might start later than expected. Consider a simple example:

```
void f(void) {
}

void function_that_takes_100ms(void) {
}

Event e(FunctionPointer0<void>(f).bind());
Event long_e(FunctionPointer0<void>(function_that_takes_100ms).bind());

minar::Scheduler::postCallback(long_e).delay(minar::milliseconds(5));
minar::Scheduler::postCallback(e).delay(minar::milliseconds(10));

// MINAR will execute 'long_e' first, because it has the shortest delay.
// 'long_e' will execute for 100ms
// 'e' would then execute after 105ms instead of 10ms as originally intended
```

To avoid this kind of situation, remember to **keep the code for your events as short as possible**. This will give other events a chance to execute in time. 

This also means that **you can't use infinite loops in your application code any more**. In mbed Classic (and traditional embedded programming in general) the following pattern is quite common:

```
// WARNING: don't do this in mbed OS.
int main() {
    while(1) {
        // Your application logic goes here
    }
}
```

In mbed OS, the only infinite loop in the system exists in the MINAR scheduler (see the pseudo-code above). Since events must return on their own to give control back to the scheduler, an infinite loop in an event will prevent the scheduler from running, which in turn prevents other events from being executed.

## Event details

An important thing to keep in mind is that **events are passed to MINAR by value**. When MINAR receives an event, it will make a copy of the event in an internal storage area, so even if the original event object gets out of scope, MINAR will still be able to call the corresponding function with its correct arguments later. This means that you don't have to worry if the event object goes out of scope after you call `postCallback` (so it's safe to use temporary objects):

```
void f(int a) {
}

minar::Scheduler::postCallback(FunctionPointer0<void>(f).bind(10));
```

Be careful though: MINAR only keeps a copy of the `Event` instance itself and nothing else outside that. If the event is bound to an object that goes out of scope before the event is scheduled, your program will likely not behave as expected (and might even crash):

```
class A {
public:
	A(int i): _i(i) {
	}

    int f() {
	    printf("i = %d", _i);
    }

private:
    int _i;
};

void test() {
    A a(10);
    // The intention is to call a.f() after 100ms
    minar::Scheduler::postCallback(FunctionPointer0<void>(&a, &A::f).bind()).
        delay(minar::milliseconds(100));
    // 'test' will exit immediately after `postCallback` above finishes
    // and 'a' will go out of scope. 100ms later, MINAR will try to call
    // 'A::f' on an instance that does not exist anymore ('a'), which leads
    // to undefined behaviour.
}
```

## Impact

MINAR is the event scheduler of mbed OS, so it's important to understand how to use it properly. The first thing you're likely to notice is that mbed OS applications don't have a `main` function anymore, they use `app_start` instead:

```
// mbed  OS application
void app_start(int argc, char *argv[]) {
    // Application code starts here
}
```

The difference between `main` and `app_start` is that `app_start` is an event scheduled with MINAR (using `postCallback`). `main` is now common for all applications:

```
extern "C" int main(void) {
    minar::Scheduler::postCallback(
        FunctionPointer2<void, int, char**>(&app_start).bind(0, NULL)
    );
    // The 'start' function below doesn't actually return, since it runs
    // the MINAR's infinite event scheduler loop
    return minar::Scheduler::start();
}
```

MINAR encourages an asynchronous programming style in which functions that are expected to take a lot of time return immediately and post an event when they're done. This is again different from mbed Classic, where lots of functions (for example functions related to I/O operations) are blocking. Some of these blocking functions are still present in mbed OS, but their use is discouraged. Support for non-blocking operations in mbed OS is already in place for some modules and will be enhanced in the future.

"Reasonable" blocking behaviour is still fine. You don't need to use asynchronous calls for everything; if you need to wait "about" a microsecond for something to happen (using, for example, an empty **for** loop), that's fine in most cases. The definition of "reasonable" depends on the requirements of your particular application.

## Runtime Warnings

Warnings are printed to the serial port in the following situations:

1. if callbacks take longer than `Warn_Duration_Milliseconds` (10ms) to execute.
1. if the event loop is lagging (all callbacks are being executed late because there is too much to do) by more than `Warn_Lag_Milliseconds` (500ms).

Warnings can be disabled by using [`yotta config`](http://yottadocs.mbed.com/reference/config.html). Just add the following to `config.json`.

```
{
    "MINAR_NO_RUNTIME_WARNINGS" : true
}
```

# Recap

- MINAR is an event scheduler, always enabled in mbed OS.
- You can schedule events with MINAR. Events are regular C/C++ functions.
- MINAR is not a pre-emptive scheduler. Control gets back to MINAR when the currently scheduled event finishes execution.
- Events shouldn't take too much time to execute.
- Event functions should never block in an infinite loop.
- Your applications start with `app_start` now, not with `main`.
- You are encouraged to use the new non-blocking APIs in mbed OS as much as possible.

