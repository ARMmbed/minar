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

#if YOTTA_CFG_MINAR_NO_RUNTIME_WARNINGS
#define ytWarning(...) do{}while(0)
#else
#include <stdio.h>
#define ytWarning(...) printf(__VA_ARGS__)
#endif

#endif  // #ifndef __MINAR_TRACE_H__

