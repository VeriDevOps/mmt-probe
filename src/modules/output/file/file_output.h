/*
 * file_output.h
 *
 *  Created on: Dec 19, 2017
 *      Author: nhnghia
 */

#ifndef SRC_MODULES_OUTPUT_FILE_FILE_OUTPUT_H_
#define SRC_MODULES_OUTPUT_FILE_FILE_OUTPUT_H_

#include "../../../lib/configure.h"

typedef struct file_output_struct file_output_t;

file_output_t* file_output_alloc_init( const file_output_conf_t*config,  uint16_t id );

int file_output_write( file_output_t * output, const char *message );

int _file_output_write( file_output_t * output, const char *format, ...)
__attribute__((format (printf, 2, 3)));

void file_output_flush( file_output_t * output);

void file_output_release( file_output_t * output);

#endif /* SRC_MODULES_OUTPUT_FILE_FILE_OUTPUT_H_ */
