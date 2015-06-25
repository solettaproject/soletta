#include "common.h"

// The process module is a bit of an alien WRT logging, as it has to
// share the domain symbol externally with the various .o objects that
// will be linked together. Also, log_init() is defined here instead.
SOL_LOG_INTERNAL_DECLARE(_log_domain, "flow-process");

static void
log_init(void)
{
    SOL_LOG_INTERNAL_INIT_ONCE;
}


#include "process-gen.c"
