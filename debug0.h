
#ifndef NO_DEBUG

#if defined (__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
# define DEBUG(...) do {debug0(__FILE__, __LINE__, __VA_ARGS__);} while (0)
#elif defined (__GNUC__)
# define DEBUG(format...) do {debug0(__FILE__, __LINE__, format);} while (0)
#endif

#define DEBUG_DECL(decl) decl

#else

#if defined (__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
# define DEBUG(...)	    //
#elif defined (__GNUC__)
# define DEBUG(format...)   //
#endif

#define DEBUG_DECL(decl) //

#endif

void debug0(char *file, int line, char *format, ...) __attribute__ ((format (printf, 3, 4)));
