#ifndef RDP_LOG_HPP
#define RDP_LOG_HPP

#include <stdbool.h>

#ifndef PARALLEL_RDP_LOG
#define PARALLEL_RDP_LOG 0
#endif

#if PARALLEL_RDP_LOG
void rdp_log_init(void);
void rdp_log_close(void);
bool rdp_log_is_enabled(void);
void rdp_log(const char *fmt, ...);
#define RDP_LOG_MSG(...) do { if (rdp_log_is_enabled()) rdp_log(__VA_ARGS__); } while(0)
#else
static inline void rdp_log_init(void) {}
static inline void rdp_log_close(void) {}
static inline bool rdp_log_is_enabled(void) { return false; }
static inline void rdp_log(const char *fmt, ...) { (void)fmt; }
#define RDP_LOG_MSG(...) do {} while(0)
#endif

#endif // RDP_LOG_HPP
