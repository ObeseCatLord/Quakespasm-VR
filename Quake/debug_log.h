#ifndef DEBUG_LOG_H
#define DEBUG_LOG_H

void DebugLog(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void DebugLog_Init(void);
void DebugLog_Close(void);

#endif
