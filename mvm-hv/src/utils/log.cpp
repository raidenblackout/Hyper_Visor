#define _NO_CRT_STDIO_INLINE
#include <ntifs.h>
#include <stdarg.h>
#include <ntstrsafe.h>
#include <intrin.h>
#include "log.h"
#include "hv_platform.h"
#include "boot_stubs.h"

void LogPrint(__log_type type, const char* fmt, ...)
{
    if (hv_platform::is_boot_time())
    {
        const char* tag = "[?]";
        switch (type)
        {
        case LOG_TYPE_DEBUG:
            tag = "[D]";
            break;
        case LOG_TYPE_DUMP:
            tag = "[U]";
            break;
        case LOG_TYPE_ERROR:
            tag = "[E]";
            break;
        case LOG_TYPE_INFO:
            tag = "[I]";
            break;
        default:
            break;
        }
        va_list ap;
        va_start(ap, fmt);
        (void)0;
        va_end(ap);
        return;
    }

    char*         LogType            = NULL;
    LARGE_INTEGER SystemTime         = {};
    LARGE_INTEGER LocalTime          = {};
    TIME_FIELDS   TimeFields         = {};
    char          TimeBuffer[20]     = {};
    char          MessageBuffer[412] = {};
    char*         OutputFormat       = NULL;
    char          OutputBuffer[512]  = {};
    va_list       Args               = {};

    switch (type)
    {
    case LOG_TYPE_DEBUG:
    {
        LogType = "[DEBUG]";
        break;
    }
    case LOG_TYPE_DUMP:
    {
        LogType = "[DUMP]";
        break;
    }
    case LOG_TYPE_ERROR:
    {
        LogType = "[ERROR]";
        break;
    }
    case LOG_TYPE_INFO:
    {
        LogType = "[INFORMATION]";
        break;
    }
    default:
    {
        break;
    }
    }

    KeQuerySystemTime(&SystemTime);
    ExSystemTimeToLocalTime(&SystemTime, &LocalTime);
    RtlTimeToTimeFields(&LocalTime, &TimeFields);

    RtlStringCchPrintfA(TimeBuffer,
                        sizeof(TimeBuffer),
                        "[%02hd:%02hd:%02hd.%03hd]",
                        TimeFields.Hour,
                        TimeFields.Minute,
                        TimeFields.Second,
                        TimeFields.Milliseconds);

    va_start(Args, fmt);
    RtlStringCchVPrintfA(MessageBuffer, sizeof(MessageBuffer), fmt, Args);
    va_end(Args);

    OutputFormat = "%s  %s  %s\r\n";

    RtlStringCchPrintfA(OutputBuffer, sizeof(OutputBuffer), OutputFormat, TimeBuffer, LogType, MessageBuffer);

    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "%s", OutputBuffer);
}