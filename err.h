#ifndef _ERR_
#define _ERR_

/* Display information about system function error and ends the program */
extern void syserr(const char *fmt, ...);

/* Displays information about error and ends the program */
extern void fatal(const char *fmt, ...);

/* Displays information about error */
extern void msgerr(const char *fmt, ...);

#endif
