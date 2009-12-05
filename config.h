#ifndef CONFIG_H
#define CONFIG_H

/* Define if we have the ALSA backend */
#define HAVE_ALSA

/* Define if we have the OSS backend */
#define HAVE_OSS

/* Define if we have the DSound backend */
/* #undef HAVE_DSOUND */

/* Define if we have the Windows Multimedia backend */
/* #undef HAVE_WINMM */

/* Define if we have the pulseaudio backend */
#define HAVE_PULSEAUDIO

/* Define if we have dlfcn.h */
#define HAVE_DLFCN_H

/* Define if we have the sqrtf function */
#define HAVE_SQRTF

/* Define if we have the acosf function */
#define HAVE_ACOSF

/* Define if we have the strtof function */
#define HAVE_STRTOF

/* Define if we have stdint.h */
#define HAVE_STDINT_H

/* Define if we have the __int64 type */
/* #undef HAVE___INT64 */

/* Define to the size of a long int type */
#define SIZEOF_LONG 4

/* Define to the size of a long long int type */
#define SIZEOF_LONG_LONG 8

/* Define to the size of an unsigned int type */
#define SIZEOF_UINT 4

/* Define to the size of a void pointer type */
#define SIZEOF_VOIDP 4

/* Define if we have GCC's destructor attribute */
#define HAVE_GCC_DESTRUCTOR

#endif
