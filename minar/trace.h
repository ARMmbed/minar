#ifndef __MINAR_TRACE_H__
#define __MINAR_TRACE_H__

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

#endif  // #ifndef __MINAR_TRACE_H__

