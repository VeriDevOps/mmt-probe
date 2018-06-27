/*
 * log.h
 *
 *  Created on: Dec 13, 2017
 *          by: Huu Nghia
 */

#ifndef SRC_LIB_LOG_H_
#define SRC_LIB_LOG_H_

#include <syslog.h>
#include <execinfo.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

/**
 * Signal to restart the current (child) process
 */
#define SIGRES SIGUSR2


/**
 * Open system log file. This function must be called before any calls of log_write
 */
static inline void log_open(){
	openlog( "mmt-probe", LOG_NDELAY | LOG_CONS | LOG_PERROR, LOG_USER);
}

#define log_write syslog

/**
 * A macro for outputting message only when debugging
 */
#ifdef DEBUG_MODE
#define DEBUG( format, ... ) \
    printf( "DEBUG %s:%d: " format "\n", __FILE__, __LINE__ ,## __VA_ARGS__ )
#else
	#define DEBUG( ... )
#endif

/**
 * Close log file.
 * This must be done only at the end of the execution of MMT-Probe
 */
static inline void log_close(){
	closelog();
}

/**
 * Abort the current execution.
 */
#define ABORT( format, ... )                                                \
	do{                                                                     \
		log_write( LOG_ERR, format,## __VA_ARGS__ );                        \
		abort();                                                            \
	}while( 0 )

/**
 * Ensure that exp is true, otherwise, we will abort the current execution
 */
#define ASSERT( exp, format, ... )                                          \
	while( !(exp) ){                                                        \
		log_write( LOG_ERR, "%s:%d"format,__FILE__, __LINE__,## __VA_ARGS__ );\
		abort();                                                            \
	}


/**
 *  Obtain the current execution trace
 */
static inline void log_execution_trace () {
	void *array[10];
	size_t size;
	char **strings;
	size_t i;
	size    = backtrace (array, 10);
	strings = backtrace_symbols (array, size);
	//i=2: ignore 2 first elements in trace as they are: this fun, then mmt_log
	for (i = 2; i < size; i++)
		log_write( LOG_ERR, "%zu. %s\n", (i-1), strings[i] );

	free (strings);
}

/**
 * This macro can be used to raise errors causing by inconsistency of using library functions.
 * For example, the calling of function A requires the calling of B before, but programmers did not do.
 */
#define MY_MISTAKE( format, ... )                                           \
	do{                                                                     \
		log_write( LOG_ERR, format,## __VA_ARGS__ );                        \
		fprintf( stderr, format,## __VA_ARGS__ );                           \
		log_execution_trace();                                              \
		exit( 0 );                                                          \
	}while( 0 )

#endif /* SRC_LIB_LOG_H_ */
