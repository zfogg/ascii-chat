/* Windows compatibility stub for sys/param.h */
#ifndef _SYS_PARAM_H_COMPAT
#define _SYS_PARAM_H_COMPAT

#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif

#ifndef MAX
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#endif

#endif /* _SYS_PARAM_H_COMPAT */
