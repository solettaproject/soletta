#pragma once

#ifdef __cplusplus
extern "C" {
#endif

int sol_platform_impl_linux_set_hostname(const char *name);

const char *sol_platform_impl_linux_get_hostname(void);


#ifdef __cplusplus
}
#endif
