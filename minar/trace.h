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

#ifndef __MINAR_TRACE_H__
#define __MINAR_TRACE_H__

//#define __MINAR_TRACE_MEMORY__
//#define __MINAR_TRACE_DISPATCH__

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

/* Run time warnings are turned on by default only in debug builds
 * The user can override this behaviour by setting MINAR_NO_RUNTIME_WARNINGS
 * in yotta config. Example: yt build -d --config '{"MINAR_NO_RUNTIME_WARNINGS":1}' */
#ifndef  YOTTA_CFG_MINAR_NO_RUNTIME_WARNINGS
#define YOTTA_CFG_MINAR_NO_RUNTIME_WARNINGS NDEBUG
#endif

#if YOTTA_CFG_MINAR_NO_RUNTIME_WARNINGS
#define ytWarning(...) do{}while(0)
#else
#include <stdio.h>
#define ytWarning(...) printf(__VA_ARGS__)
#endif

#endif  // #ifndef __MINAR_TRACE_H__

