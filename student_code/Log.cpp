#include "Log.h"

// 全局日志输出锁，确保单个线程可以完整的输出一条语句
MutexLock global_log_lock;