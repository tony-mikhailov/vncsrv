#ifndef HHH
#define HHH

#define error_print(...)              \
    do                                \
    {                                 \
        fprintf(stderr, __VA_ARGS__); \
    } while (0)

#define info_print(...)               \
    do                                \
    {                                 \
        fprintf(stdout, __VA_ARGS__); \
    } while (0)

extern int verbose;

#define debug_print(...)                  \
    do                                    \
    {                                     \
        if (verbose == 1)                  \
            fprintf(stdout, __VA_ARGS__); \
    } while (0)

#endif