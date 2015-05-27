#include "common.h"

SOL_LOG_INTERNAL_DECLARE(_log_domain, "flow-process");

void
process_log_init(void)
{
    SOL_LOG_INTERNAL_INIT_ONCE;
}


#include "process-gen.c"
