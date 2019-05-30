#ifndef _ERR_
#define _ERR_

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <string>

/* Display information about system function error and ends the program */
extern void syserr(const char* fmt, ...);

/* Displays information about error and ends the program */
extern void fatal(const char* fmt, ...);

/* Displays information about error */
extern void msgerr(const std::string& msg);


#endif
