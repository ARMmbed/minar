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

#include "minar/minar.h"

#include <stdlib.h>
#include <limits.h>

#include "minar-platform/minar_platform.h"

#include "core-util/ExtendablePoolAllocator.h"
#include "core-util/CriticalSectionLock.h"
#include "core-util/BinaryHeap.h"
#include "core-util/core-util.h"

//#define __MINAR_TRACE_MEMORY__
//#define __MINAR_TRACE_DISPATCH__
//#ifdef NDEBUG
#define __MINAR_NO_RUNTIME_WARNINGS__
//#endif

#ifdef __MINAR_TRACE_MEMORY__
extern "C" {
    #include <stdio.h>
}
#define ytTraceMem(...) printf(__VA_ARGS__)
#else
#define ytTraceMem(...) do{}while(0)
#endif

#ifdef __MINAR_TRACE_DISPATCH__
extern "C" {
    #include <stdio.h>
}
#define ytTraceDispatch(...) printf(__VA_ARGS__)
#else
#define ytTraceDispatch(...) do{}while(0)
#endif

#ifndef __MINAR_NO_RUNTIME_WARNINGS__
#include <stdio.h>
#define ytWarning(...) printf(__VA_ARGS__)
#else
#define ytWarning(...) do{}while(0)
#endif

/**
 * Parameters to control the initial size and growth increments for the pool of
 * CallbackNodes. The default values are expected to come from root level target
 * descriptions; but may be overridden by platform or application specific
 * configurations.
 *
 * @note: The values below take effect only if config definitions in the target
 * hierarchy don't include defaults. Refer to the output of 'yotta config' for
 * available defaults.
 *
 * TODO: these default values need some serious profiling.
 */
#ifndef YOTTA_CFG_MINAR_INITIAL_EVENT_POOL_SIZE
#define YOTTA_CFG_MINAR_INITIAL_EVENT_POOL_SIZE      50
#endif
#ifndef YOTTA_CFG_MINAR_ADDITIONAL_EVENT_POOLS_SIZE
#define YOTTA_CFG_MINAR_ADDITIONAL_EVENT_POOLS_SIZE 100
#endif

using mbed::util::ExtendablePoolAllocator;
using mbed::util::CriticalSectionLock;
using mbed::util::BinaryHeap;
using mbed::util::MinCompare;

/// - Private Types


namespace minar{
/// Callbacks are stored as a sorted tree of these, currently just ordered by
/// 'call_before', which enables a very simple form of coalescing. To do much
/// better we need to estimate or learn how long each call will take, and use
/// something like a proper interval tree.
struct CallbackNode {
    CallbackNode()
      : cb(), call_before(0), tolerance(0),
        interval(0){
    }
    CallbackNode(
        minar::callback_t cb,
        minar::tick_t call_before,
        minar::tick_t tolerance,
        minar::tick_t interval
    ) : cb(cb), call_before(call_before), tolerance(tolerance),
        interval(interval){
    }
    static void* operator new(std::size_t size){
        ytTraceMem("CallbackNode alloc %u\n", size);
        (void)size;
        void *p = get_allocator()->alloc();
        if (NULL == p) {
            CORE_UTIL_RUNTIME_ERROR("Unable to allocate CallbackNode");
        }
        return p;
    }

    static void operator delete(void *p){
        ytTraceMem("CallbackNode free %u\n", sizeof(CallbackNode));
        get_allocator()->free(p);
    }

    /// The callback pointer
    minar::callback_t cb;

    /// The scheduler will try quite hard to call the function at (or up to
    /// 'tolerance' before) 'call_before'. In the event that there is more to
    /// do than time to do it then it may still be called later.
    minar::tick_t     call_before;
    minar::tick_t     tolerance;

    /// For more-efficient repeating callbacks, store the interval here and
    /// re-schedule as soon as execution is completed, without another free &
    /// alloc.
    ///
    /// 0 means do not repeat
    minar::tick_t     interval;

    static ExtendablePoolAllocator *get_allocator() {
        static ExtendablePoolAllocator *allocator = NULL;

        if (NULL == allocator) {
            UAllocTraits_t traits;
            traits.flags = UALLOC_TRAITS_NEVER_FREE; // allocate in the never-free heap
            allocator = new ExtendablePoolAllocator;
            if (allocator == NULL) {
                CORE_UTIL_RUNTIME_ERROR("Unable to create allocator for CallbackNode");
            }
            if (!allocator->init(YOTTA_CFG_MINAR_INITIAL_EVENT_POOL_SIZE, YOTTA_CFG_MINAR_ADDITIONAL_EVENT_POOLS_SIZE, sizeof(CallbackNode), traits)) {
                CORE_UTIL_RUNTIME_ERROR("Unable to initialize allocator for CallbackNode");
            }
        }
        return allocator;
    }
};

struct YTScopeTimer{
    YTScopeTimer(minar::tick_t threshold, const char* msg, const void* ptr)
        : start(minar::platform::getTime()), thr(threshold), msg(msg), ptr(ptr){
    }
    ~YTScopeTimer(){
        const minar::tick_t dur = (minar::platform::Time_Mask & (minar::platform::getTime() - start));
        if(dur > thr)
            ytWarning("WARNING: %s %p took %lums\n", msg, ptr, dur / minar::milliseconds(1));
    }

    minar::tick_t start;
    minar::tick_t const thr;
    const char* const msg;
    const void* const ptr;
};


class SchedulerData{
    public:
        typedef CallbackNode* heap_node_t; // the binary heap (below) holds pointers to CallbackNode instances
        struct CallbackNodeCompare{
            CallbackNodeCompare(SchedulerData const& sched)
                : sched(sched){
            }
            // This function defines how the binary heap is ordered
            bool operator ()(const heap_node_t &a, const heap_node_t &b) const;

            SchedulerData const& sched;
        };
        typedef BinaryHeap<CallbackNode*, CallbackNodeCompare> dispatch_tree_t;

        SchedulerData();

        minar::callback_handle_t postGeneric(
               // [FPTR] cb below used to be a move ref, is there a better alternative to copy?
               minar::callback_t cb,
               minar::tick_t at,
               minar::tick_t interval,
               minar::tick_t double_sided_tolerance
        );

        int cancel(callback_handle_t callback);

        int start();

        // The dispatch queue is sorted by the latest possible evaluation time
        // of each callback (i.e. callbacks later in the queue may be possible
        // to evaluate sooner than those earlier)
        dispatch_tree_t dispatch_tree;

        minar::tick_t last_dispatch;
        minar::tick_t current_dispatch;
        bool stop_dispatch;
};

/// - Private Function Declarations
static minar::tick_t wrapTime(minar::tick_t time);
static minar::tick_t smallestTimeIncrement(minar::tick_t from, minar::tick_t to_a, minar::tick_t or_b);
static void* addressForFunction(minar::callback_t fn);
static bool timeIsInPeriod(minar::tick_t start, minar::tick_t time, minar::tick_t end);

/// - Pointer to instance
static minar::Scheduler* staticScheduler = NULL;

} // namespace minar


/// - Implementation of minar class

minar::Scheduler::CallbackAdder& minar::Scheduler::CallbackAdder::delay(
    minar::tick_t delay
){
    m_delay = delay;
    return *this;
}

minar::Scheduler::CallbackAdder& minar::Scheduler::CallbackAdder::tolerance(
    minar::tick_t tolerance
){
    m_tolerance = tolerance;
    return *this;
}

minar::Scheduler::CallbackAdder& minar::Scheduler::CallbackAdder::period(
    minar::tick_t period
){
    m_period = period;
    return *this;
}

minar::callback_handle_t minar::Scheduler::CallbackAdder::getHandle(){
    if(m_cb && !m_posted){
        minar::callback_handle_t temp = m_sched.data->postGeneric(
            // [FPTR] std::move was used below, is there a better way to do this?
            m_cb,
            minar::platform::getTime() + m_delay,
            m_period,
            m_tolerance
        );
        m_posted = true;
        return temp;
    }
    return NULL;
}

minar::Scheduler::CallbackAdder::~CallbackAdder(){
    getHandle();
}

minar::Scheduler::CallbackAdder::CallbackAdder(Scheduler& sched, callback_t cb)
    : m_sched(sched),
      m_cb(cb),
      m_tolerance(minar::milliseconds(50)),
      m_delay(minar::milliseconds(0)),
      m_period(minar::milliseconds(0)),
      m_posted(false){
}

minar::Scheduler* minar::Scheduler::instance(){
    if(!staticScheduler){
        staticScheduler = new minar::Scheduler();

        minar::platform::init();

        CORE_UTIL_ASSERT(staticScheduler->data->dispatch_tree.get_num_elements() == 0 && "State not clean: cannot init.");

        staticScheduler->data->last_dispatch = minar::platform::getTime();
        staticScheduler->data->current_dispatch = staticScheduler->data->last_dispatch;
    }
    return staticScheduler;
}

minar::Scheduler::Scheduler()
    // !!! FIXME: make_unique is C++14
    //: data(std::make_unique<minar::SchedulerData>()){
    : data(new minar::SchedulerData()){
}

int minar::Scheduler::start(){
    instance();
    return staticScheduler->data->start();
}

int minar::Scheduler::stop(){
    instance();
    staticScheduler->data->stop_dispatch = true;
    return staticScheduler->data->dispatch_tree.get_num_elements();
}

minar::Scheduler::CallbackAdder minar::Scheduler::postCallback(
    minar::callback_t const& cb
){
    instance();
    return CallbackAdder(*staticScheduler, cb);
}

int minar::Scheduler::cancelCallback(minar::callback_handle_t handle){
    instance();
    return staticScheduler->data->cancel(handle);
}

minar::tick_t minar::Scheduler::getTime() {
    instance();
    return staticScheduler->data->current_dispatch;
}

/// - SchedulerData Implementation

minar::SchedulerData::SchedulerData()
  : dispatch_tree(CallbackNodeCompare(*this)),
    last_dispatch(0),
    current_dispatch(0),
    stop_dispatch(false){
    UAllocTraits_t traits;

    traits.flags = UALLOC_TRAITS_NEVER_FREE;
    if (!dispatch_tree.init(YOTTA_CFG_MINAR_INITIAL_EVENT_POOL_SIZE, YOTTA_CFG_MINAR_ADDITIONAL_EVENT_POOLS_SIZE, traits)) {
        CORE_UTIL_RUNTIME_ERROR("Unable to initialize binary heap for SchedulerData");
    }
}

bool minar::SchedulerData::CallbackNodeCompare::operator ()(const heap_node_t &a, const heap_node_t &b) const {
    // FIXME!!!! double-check that this works for the case where multiple
    // callbacks have the same dispatch time, and we pop one, set Dispatch equal
    // to that time, then re-sort
    if((a->call_before - sched.last_dispatch) < (b->call_before - sched.last_dispatch))
        return true;
    else
        return false;
    return true;
}

int minar::SchedulerData::start(){
    const static minar::tick_t Warn_Duration_Ticks = minar::milliseconds(minar::Warn_Duration_Milliseconds);
    const static minar::tick_t Warn_Lag_Ticks      = minar::milliseconds(minar::Warn_Lag_Milliseconds);

    stop_dispatch = false;
    CallbackNode* next = NULL;
    minar::tick_t now = 0;
    minar::tick_t now_plus_tolerance = 0;
    bool something_to_do = false;

    while(!stop_dispatch){
        now = minar::platform::getTime();

        // look at the next callback, checking to see if we can execute it
        // because of the sort order, we will naturally execute the
        // must-execute-first callbacks first

        something_to_do = false;
        {
            CriticalSectionLock lock;
            CallbackNode *best = NULL;

            if(dispatch_tree.get_num_elements() > 0) {
                CallbackNode *root = dispatch_tree.get_root();
                now_plus_tolerance = wrapTime(now + root->tolerance);
                if (timeIsInPeriod(last_dispatch, root->call_before, now_plus_tolerance)) {
                    best = root;
                }
            }
            if (best != NULL) {
                next = best;
                dispatch_tree.remove_root();
                something_to_do = true;

                // the last dispatch time must not be updated past the time of
                // the next thing in must-execute-by order, otherwise we will
                // break the sorting of our tree, and skip the execution of
                // things.  If we haven't yet reached that time we shouldn't
                // update last_dispatch to be in the future though, (because if
                // we do that it might also go backwards)
                //
                // We have to perform this update with interrupts disabled
                // because we use last_dispatch for sorting dispatch_tree
                if(dispatch_tree.get_num_elements() > 0)
                    last_dispatch = smallestTimeIncrement(last_dispatch, now, (dispatch_tree.get_root())->call_before);
                else
                    last_dispatch = smallestTimeIncrement(last_dispatch, now, next->call_before);

                const minar::tick_t lag = wrapTime(now - last_dispatch);
                if(lag > Warn_Lag_Ticks)
                    ytWarning("WARNING: event loop lag %lums\n", lag / minar::milliseconds(1));
            }
            else
            {
                // nothing we can do right now... so go to sleep
                ytTraceDispatch("-_-\n");

                // The platform_sleepFromUntil function must work
                // even with interrupts disabled (which is the case if the
                // WFE/WFI instructions are used)
                //
                // note that here we sleep for as *long* as possible (until the
                // latest possible evaluation time of the next callback (the
                // latest time is what the queue is sorted by)), to enable
                // simple coalescing

                // update last_dispatch to be the last time we checked, rather than
                // the last actual dispatch (which may be vanishing far into
                // the past if we have nothing to do)

                // if there's stuff to do  then sleep until the next
                // must-execute-by time. If an interrupt changes this we will
                // wake up and unconditionally re-evaluate.

                // Find the next must-execute-by time that is not in the past
                // and sleep until then.
                // If none are found sleep unconditionally
                if (dispatch_tree.get_num_elements() > 0) {
                    CallbackNode *root = dispatch_tree.get_root();
                    last_dispatch = smallestTimeIncrement(last_dispatch, now, root->call_before);
                    minar::platform::sleepFromUntil(now, root->call_before);
                } else {
                    last_dispatch = now;
                    minar::platform::sleep();
                }

                // before taking re-enabling interrupts (and taking any
                // interrupt handlers), make sure the time used for the basis
                // of any callbacks scheduled from the interrupt handlers is
                // up-to-date.
                current_dispatch = minar::platform::getTime();
            }
            // after we wake from sleep (caused by an interrupt),
            // interrupts are re-enabled, we take any interrupt handlers,
            // then return here
        }

        // this is skipped when we return from sleep
        // because something_to_do will be false
        if(something_to_do){
            // therefore "next" is valid
            ytTraceDispatch("[picked first, ahead / %d]\r\n", dispatch_tree.get_num_elements());

            // current_dispatch is provided through the ytGetTime API call so
            // that functions can schedule future execution based on the
            // intended execution time of the callback, rather than the time it
            // actually executed.
            //
            // note that current_dispatch is always in the future (or equal)
            // compared to last_dispatch
            current_dispatch = wrapTime(next->call_before - next->tolerance/2);

            if(next->interval){
                // recycle the callback for next time: do that here so that the
                // callback can cancel itself
                next->call_before = wrapTime(next->call_before + next->interval);
                dispatch_tree.insert(next);
            }

            // dispatch!
            if(next->cb){
                ytTraceDispatch("[dispatch: now=%lx func=%p]\r\n", now, addressForFunction(next->cb));
                YTScopeTimer t(Warn_Duration_Ticks, "callback", addressForFunction(next->cb));
                next->cb();
            }

            if(!next->interval){
                // release any reference-counted callback as early as possible
                delete next;
                next = NULL;
            }
        }
    } // loop while(!stop_dispatch)

    return dispatch_tree.get_num_elements();
}

minar::callback_handle_t minar::SchedulerData::postGeneric(
       // [FPTR] cb below used to be a move ref, is there a better alternative to copy?
       minar::callback_t cb,
           minar::tick_t at,
           minar::tick_t interval,
           minar::tick_t double_sided_tolerance
){
    CORE_UTIL_ASSERT(double_sided_tolerance < (minar::platform::Time_Mask/2) + 1);//, "Callback tolerance greater than time wrap-around.");

    ytTraceDispatch("[post %lx %lx %p]\n", minar::platform::getTime(), at, addressForFunction(cb));

    CallbackNode* n = new CallbackNode(
        cb,
        wrapTime(at + interval),
        2 * double_sided_tolerance,
        interval
    );
    dispatch_tree.insert(n);
    return n;
}

int minar::SchedulerData::cancel(minar::callback_handle_t handle) {
    CallbackNode *node = (CallbackNode*)handle;
    if (dispatch_tree.remove(node)) {
        delete node;
        return 1;
    } else {
        return 0;
    }
}

/// - Public Function Definitions

/// @name Time

/// convert milliseconds into the internal "ticks" time representation
minar::tick_t minar::milliseconds(uint32_t milliseconds){
    const uint64_t ticks = (((uint64_t) milliseconds) * ((uint64_t) minar::platform::Time_Base)) / 1000;
    CORE_UTIL_ASSERT(ticks < minar::platform::Time_Mask);//, @"Callback delay greater than time wrap-around.");
    return (minar::tick_t) (minar::platform::Time_Mask & ticks);
}

/// convert ticks into the milliseconds time representation
uint32_t minar::ticks(minar::platform::tick_t ticks){
    uint64_t milliseconds = ((uint64_t)ticks * 1000U) / minar::platform::Time_Base;
    assert(milliseconds <= 0xFFFFFFFF);
    return (uint32_t)milliseconds;
}

/// Return the scheduled execution time of the current callback. This lags
/// behind the wall clock time if the system is busy.
///
/// Note that this time is NOT monotonic. If callbacks are executed in an order
/// different to their scheduled order because of the resources they need, then
/// this time will jump backwards.
minar::tick_t minar::getTime(){
    return Scheduler::instance()->getTime();
}

static minar::tick_t minar::wrapTime(minar::tick_t time){
    return time & minar::platform::Time_Mask;
}

static minar::tick_t minar::smallestTimeIncrement(minar::tick_t from, minar::tick_t to_a, minar::tick_t or_b){
    if((to_a >= from && or_b >= from) || (to_a < from && or_b < from))
        return (to_a < or_b)? to_a : or_b;
    if(to_a > from && or_b < from)
        return to_a;
    //if(to_a < from && or_b > from)
    CORE_UTIL_ASSERT(to_a < from && or_b >= from);//, @" ");
    return or_b;
}

static void* minar::addressForFunction(minar::callback_t){
    // !!! FIXME: need to poke into std::function to get an address that's
    // useful when debugging
    return NULL;
}

static bool minar::timeIsInPeriod(minar::tick_t start, minar::tick_t time, minar::tick_t end){
    // Taking care to handle wrapping: (M = now + Minumum_Sleep)
    //   Case (A.1)
    //                       S    T   E
    //      0 ---------------|----|---|-- 0xf
    //
    //   Case (A.2): this case also allows S==T==E
    //         E                 S    T
    //      0 -|-----------------|----|-- 0xf
    //
    //   Case (B)
    //         T   E                 S
    //      0 -|---|-----------------|--- 0xf
    //
    if((time >= start && ( time < end ||    // (A.1)
                          start >= end)) || // (A.2)
        (time < start && end < start && end > time)){  // (B)
        return true;
    }
    return false;
}

