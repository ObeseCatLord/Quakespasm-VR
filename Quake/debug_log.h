#ifndef DEBUG_LOG_H
#define DEBUG_LOG_H

#ifdef __GNUC__
#define DEBUG_LOG_PRINTF __attribute__((format(printf, 1, 2)))
#else
#define DEBUG_LOG_PRINTF
#endif

void DebugLog(const char *fmt, ...) DEBUG_LOG_PRINTF;
void DebugLog_Init(void);
void DebugLog_Close(void);

#endif
