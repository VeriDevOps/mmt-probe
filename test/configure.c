/*
 * configure.c
 *
 *  Created on: Dec 12, 2017
 *      Author: nhnghia
 */

#include <stdio.h>
#include "../src/lib/configure.h"

int main( int argc, const char **argv){
	if( argc != 2 ){
		printf("Usage: %s config_file\n", argv[0] );
		exit( 1 );
	}

	probe_conf_t *conf = load_configuration_from_file( argv[1] );

	release_probe_configuration( conf );
}