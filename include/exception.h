/* =========================================================================
    CException - Simple Exception Handling in C
    ThrowTheSwitch.org
    Copyright (c) 2007-24 Mark VanderVoord
    SPDX-License-Identifier: MIT
	Source: https://github.com/ThrowTheSwitch/CException
========================================================================= */

#ifndef _CEXCEPTION_H
#define _CEXCEPTION_H

#include <setjmp.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define CEXCEPTION_VERSION_MAJOR    1
#define CEXCEPTION_VERSION_MINOR    3
#define CEXCEPTION_VERSION_BUILD    4
#define CEXCEPTION_VERSION          ((CEXCEPTION_VERSION_MAJOR << 16) | (CEXCEPTION_VERSION_MINOR << 8) | CEXCEPTION_VERSION_BUILD)

#ifndef CEXCEPTION_NONE
#define CEXCEPTION_NONE      (0x5A5A5A5A)
#endif

#ifndef CEXCEPTION_NUM_ID
#define CEXCEPTION_NUM_ID    (1)
#endif

#ifndef CEXCEPTION_GET_ID
#define CEXCEPTION_GET_ID    (0)
#endif

//This is an optional special handler for when there is no global Catch
#ifndef CEXCEPTION_NO_CATCH_HANDLER
#define CEXCEPTION_NO_CATCH_HANDLER(id)
#endif

// New Exception Struct
typedef struct {
    unsigned int id;
    const char* msg;
    const char* file;
    int line;
} CEXCEPTION_T;

typedef CEXCEPTION_T Exception;

typedef struct {
    jmp_buf* pFrame;
    volatile CEXCEPTION_T Exception;
} CEXCEPTION_FRAME_T;

extern volatile CEXCEPTION_FRAME_T CExceptionFrames[];
extern volatile int CEXCEPTION_g_in_try;

#define Try                                                         \
    {                                                               \
        jmp_buf *PrevFrame, NewFrame;                               \
        unsigned int MY_ID = CEXCEPTION_GET_ID;                     \
        PrevFrame = CExceptionFrames[MY_ID].pFrame;                 \
        CExceptionFrames[MY_ID].pFrame = (jmp_buf*)(&NewFrame);     \
        CExceptionFrames[MY_ID].Exception.id = CEXCEPTION_NONE;     \
        CExceptionFrames[MY_ID].Exception.file = __FILE__;			\
        CExceptionFrames[MY_ID].Exception.line = __LINE__;			\
		CEXCEPTION_g_in_try = 1; 									\
        if (setjmp(NewFrame) == 0) {                                \
            if (1)

#define Catch(e)                                                    \
            else { }                                                \
        }                                                           \
        else                                                        \
        {                                                           \
            e = CExceptionFrames[MY_ID].Exception;                  \
        }                                                           \
        CExceptionFrames[MY_ID].pFrame = PrevFrame;                 \
		CEXCEPTION_g_in_try = 0;                                    \
    }                                                               \
    if (CExceptionFrames[CEXCEPTION_GET_ID].Exception.id != CEXCEPTION_NONE)

void CException_Throw(unsigned int ExceptionID, const char* msg, const char* file, int line);
#define Throw(eid,msg) CException_Throw(eid, msg, __FILE__, __LINE__)

#define ExitTry() Throw(CEXCEPTION_NONE, "", __FILE__, __LINE__)

void CException_InstallSignalHandlers(void);

#ifdef __cplusplus
}   // extern "C"
#endif

#endif // _CEXCEPTION_H

#include <signal.h>
#include <string.h>
#include <stdio.h>
//#include "CException.h"

volatile int CEXCEPTION_g_in_try = 0; // To know if we're inside a Try block


#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif

volatile CEXCEPTION_FRAME_T CExceptionFrames[CEXCEPTION_NUM_ID] = {{ 0 }};

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif


//------------------------------------------------------------------------------------------
//  Throw
//------------------------------------------------------------------------------------------
void CException_Throw(unsigned int ExceptionID, const char* msg, const char* file, int line)
{
    unsigned int MY_ID = CEXCEPTION_GET_ID;
    CExceptionFrames[MY_ID].Exception.id = ExceptionID;
    CExceptionFrames[MY_ID].Exception.msg = msg;
    CExceptionFrames[MY_ID].Exception.file = file;
    CExceptionFrames[MY_ID].Exception.line = line;
    if (CExceptionFrames[MY_ID].pFrame)
    {
        longjmp(*CExceptionFrames[MY_ID].pFrame, 1);
    }
    CEXCEPTION_NO_CATCH_HANDLER(ExceptionID);
}


static void CException_SignalHandler(int signo) {
    unsigned int MY_ID = CEXCEPTION_GET_ID;
    if (CEXCEPTION_g_in_try) {
        // Throw a synthetic exception for the signal
        CException_Throw(0xDEAD, "Segmentation Fault (SIGSEGV) in TRY block", 
            CExceptionFrames[MY_ID].Exception.file ? CExceptionFrames[MY_ID].Exception.file : "unknown",
            CExceptionFrames[MY_ID].Exception.line);
    } else {
        // Not in a try block, print and abort
        printf("Fatal signal %d received outside of TRY block. Aborting.\n", signo);
        signal(signo, SIG_DFL);
        raise(signo);
    }
}

void CException_InstallSignalHandlers(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = CException_SignalHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGSEGV, &sa, NULL);
    // Optionally add more signals here
}


//------------------------------------------------------------------------------------------
//  Explanation of what it's all for:
//------------------------------------------------------------------------------------------
/*
#define Try
    {                                                                   <- give us some local scope.  most compilers are happy with this
        jmp_buf *PrevFrame, NewFrame;                                   <- prev frame points to the last try block's frame.  new frame gets created on stack for this Try block
        unsigned int MY_ID = CEXCEPTION_GET_ID;                         <- look up this task's id for use in frame array.  always 0 if single-tasking
        PrevFrame = CExceptionFrames[CEXCEPTION_GET_ID].pFrame;         <- set pointer to point at old frame (which array is currently pointing at)
        CExceptionFrames[MY_ID].pFrame = &NewFrame;                     <- set array to point at my new frame instead, now
        CExceptionFrames[MY_ID].Exception = CEXCEPTION_NONE;            <- initialize my exception id to be NONE
        if (setjmp(NewFrame) == 0) {                                    <- do setjmp.  it returns 1 if longjump called, otherwise 0
            if (&PrevFrame)                                             <- this is here to force proper scoping.  it requires braces or a single line to be but after Try, otherwise won't compile.  This is always true at this point.

#define Catch(e)
            else { }                                                    <- this also forces proper scoping.  Without this they could stick their own 'else' in and it would get ugly
            CExceptionFrames[MY_ID].Exception = CEXCEPTION_NONE;        <- no errors happened, so just set the exception id to NONE (in case it was corrupted)
        }
        else                                                            <- an exception occurred
        { e = CExceptionFrames[MY_ID].Exception; e=e;}                  <- assign the caught exception id to the variable passed in.
        CExceptionFrames[MY_ID].pFrame = PrevFrame;                     <- make the pointer in the array point at the previous frame again, as if NewFrame never existed.
    }                                                                   <- finish off that local scope we created to have our own variables
    if (CExceptionFrames[CEXCEPTION_GET_ID].Exception != CEXCEPTION_NONE)  <- start the actual 'catch' processing if we have an exception id saved away
 */

