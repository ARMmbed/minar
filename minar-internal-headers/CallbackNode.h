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

#ifndef __MINAR_CALLBACKNODE_H__
#define __MINAR_CALLBACKNODE_H__

#include "minar/minar.h"
#include "core-util/ExtendablePoolAllocator.h"
#include "core-util/assert.h"
#include "minar/trace.h"

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

    static mbed::util::ExtendablePoolAllocator *get_allocator() {
        static mbed::util::ExtendablePoolAllocator *allocator = NULL;

        if (NULL == allocator) {
            UAllocTraits_t traits;
            traits.flags = UALLOC_TRAITS_NEVER_FREE; // allocate in the never-free heap
            allocator = new mbed::util::ExtendablePoolAllocator;
            if (allocator == NULL) {
                CORE_UTIL_RUNTIME_ERROR("Unable to create allocator for CallbackNode");
            }
            if (!allocator->init(YOTTA_CFG_MINAR_INITIAL_EVENT_POOL_SIZE, YOTTA_CFG_MINAR_ADDITIONAL_EVENT_POOLS_SIZE, sizeof(CallbackNode), traits)) {
                CORE_UTIL_RUNTIME_ERROR("Unable to initialize allocator for CallbackNode");
            }
        }
        return allocator;
    }
}; // struct CallbackNode

} // namespace minar

#endif // #ifndef __MINAR_CALLBACKNODE_H__

