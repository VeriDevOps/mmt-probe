/*
 * main.c
 *
 *  Created on: Dec 12, 2017
 *          by: Huu Nghia
 */
#include <execinfo.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h> //usleep, sleep
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <mmt_core.h>

#include "lib/log.h"
#include "lib/version.h"
#include "lib/tools.h"
#include "lib/memory.h"
#include "lib/version.h"
#include "lib/limit.h"

#include "context.h"
#include "configure.h"
#include "modules/routine/routine.h"

#ifdef DPDK_MODULE
#include "modules/packet_capture/dpdk/dpdk_capture.h"
#endif

#ifdef PCAP_MODULE
#include "modules/packet_capture/pcap/pcap_capture.h"
#endif

#ifdef SECURITY_MODULE
#include "modules/security/security.h"
#endif


//#define DEFAULT_CONFIG_FILE "/opt/mmt/probe/mmt-probe.conf"
#define DEFAULT_CONFIG_FILE "./mmt-probe.conf"

static void _print_usage(const char * prg_name) {
	printf("%s [<option>]\n", prg_name);
	printf("Option:\n");
	printf("\t-v               : Print version information then exit.\n");
	printf("\t-c <config file> : Gives the path to the config file (default: %s).\n", DEFAULT_CONFIG_FILE);
	printf("\t-t <trace file>  : Gives the trace file to analyse.\n");
	printf("\t-i <interface>   : Gives the interface name for live traffic analysis.\n");
	printf("\t-X attr=value    : Override configuration attribute. For example \"-X file-output.output-dir=/tmp/\" will change output directory to /tmp. \n");
	printf("\t-h               : Prints this help then exit.\n");
}

static inline void _override_string_conf( char **conf, const char*new_val ){
	free( *conf );
	*conf = strdup( new_val );
}


static inline probe_conf_t* _parse_options( int argc, char ** argv ) {
	int opt, optcount = 0;
	int val;

	const char *options = "c:t:i:vhX:";
	const char *config_file = DEFAULT_CONFIG_FILE;
	probe_conf_t *conf = NULL;

	extern char *optarg;
	extern int optind;

	char *string_att, *string_val;

	bool is_user_gives_conf_file = false;

	//first parser round to get configuration file
	while ((opt = getopt(argc, argv, options )) != EOF) {
		switch (opt) {
		case 'c':
			config_file = optarg;
			is_user_gives_conf_file = true;
			break;
		case 'v':
			printf("Version:\n");
			printf( "- MMT-Probe %s\n", get_version());
			printf( "- MMT-DPI %s\n", mmt_version() );
			IF_ENABLE_SECURITY(
					printf( "- MMT-Security %s\n", security_get_version() );
			)
			printf("- Modules: %s\n", MODULES_LIST );
			exit( EXIT_SUCCESS );
		case 'h':
			_print_usage(argv[0]);
			exit( EXIT_SUCCESS );
		}
	}

	conf = conf_load_from_file( config_file );
	if( conf == NULL ){
		//config_file is indicated by user by -c parameter
		if( is_user_gives_conf_file ){
			fprintf(stderr, "Cannot read configuration file from %s\n", config_file );
			abort();
		}else{
			//try again to read config from /opt/mmt/probe/mmt-probe.conf
			config_file = "/opt/mmt/probe/mmt-probe.conf";
			conf = conf_load_from_file( config_file );
			if( conf == NULL )
				abort();
		}
	}

	log_write( LOG_INFO, "Loaded configuration from '%s'", config_file );

	//reset getopt function
	optind = 0;
	//override some options inside the configuration
	while ((opt = getopt(argc, argv, options)) != EOF) {
		switch (opt) {
		//trace file
		case 't':
			_override_string_conf( &conf->input->input_source, optarg );
			//switch to offline mode
			conf->input->input_mode = OFFLINE_ANALYSIS;
			break;
			//input interface
		case 'i':
			_override_string_conf( &conf->input->input_source, optarg );
			//switch to online mode
			conf->input->input_mode = ONLINE_ANALYSIS;
			break;

		case 'X':
			string_att = optarg;
			string_val = optarg;
			while( *string_val != '\0' ){
				//separated by = character
				if( *string_val == '=' ){
					*string_val = '\0'; //NULL ended for attribute
					//jump to the part after = character
					string_val ++;
					break;
				}
				string_val ++;
			}
			//not found = character
			if( string_val == '\0' )
				log_write( LOG_WARNING, "Input parameter '%s' is not well-formatted (must be in format parameter=value). Ignored it.", string_att );

			config_attribute_t ident = conf_get_ident_att_from_string(string_att);
			if( ident == CONF_ATT__NONE ){
				log_write( LOG_WARNING, "Ignored input parameter '%s' as no corresponding configuration parameter or no supported yet.", string_att );
			}else if( conf_override_element(conf, ident, string_val) )
				log_write( LOG_INFO, "Overridden value of configuration parameter '%s' by '%s'", string_att, string_val );
		}
	}

	return conf;
}

IF_ENABLE_DEBUG(
	#warning "The debug compile option is reserved only for debugging"
)

/* Obtain a backtrace */
void print_execution_trace () {
	void *array[10];
	size_t size;
	char **strings;
	size_t i;
	size    = backtrace (array, 10);
	strings = backtrace_symbols (array, size);
	//i=2: ignore 2 first elements in trace as they are: this fun, then mmt_log
	for (i = 2; i < size; i++){
		log_write( LOG_ERR, "%zu. %s\n", (i-1), strings[i]);

		//DEBUG_MODE given by Makefile
#ifdef DEBUG_MODE
		/* find first occurence of '(' or ' ' in message[i] and assume
		 * everything before that is the file name. (Don't go beyond 0 though
		 * (string terminator)*/
		size_t p = 0, size;
		while(strings[i][p] != '(' && strings[i][p] != ' '
				&& strings[i][p] != 0)
			++p;

		char syscom[256];


		size = snprintf(syscom, sizeof( syscom ), "addr2line %p -e %.*s", array[i] , (int)p, strings[i] );
		syscom[size] = '\0';
		//last parameter is the filename of the symbol

		fprintf(stderr, "\t    ");
		if( system(syscom) ) {}
#endif

	}

	free (strings);
	fflush( stdout );
}


static inline void _stop_modules( probe_context_t *context){

	IF_ENABLE_PCAP(
		pcap_capture_stop(context);
	)

}



//no static: other file can access to this variable by using extern keyword
probe_context_t context;

/* This signal handler ensures clean exits */
void signal_handler(int type) {
	switch (type) {
	case SIGINT:
		if(  context.is_aborting ){
#ifdef DPDK_MODULE
			rte_exit_failure( "Received Ctrl+C again. Exit immediately." );
#else
			log_write(LOG_ERR, "Received Ctrl+C again. Exit immediately." );
			fflush( stdout );
			exit( EXIT_FAILURE );
#endif
		}

		fprintf(stderr, "Received Ctrl+C. Releasing resource ...\n");
		log_write(LOG_INFO, "Received Ctrl+C. Releasing resource ...");
		context.is_aborting = true;

		_stop_modules( &context );
		break;

		//segmentation fault
	case SIGSEGV:
		log_write(LOG_ERR, "Segv signal received! Exit immediately!");
		print_execution_trace();
		exit( EXIT_FAILURE );
		break;
	case SIGTERM:
		log_write(LOG_ERR, "Termination signal received! Cleaning up before exiting!");
		break;
	case SIGABRT:
#ifdef DPDK_MODULE
		rte_exit_failure( "Abort signal received! Cleaning up before exiting!" );
#else
		log_write(LOG_ERR, "Abort signal received! Cleaning up before exiting!");
		exit( EXIT_FAILURE );
#endif
		break;
	case SIGKILL:
		log_write(LOG_ERR, "Kill signal received! Cleaning up before exiting!");
		break;
	}
}

#if defined DPDK_MODULE && defined PCAP_MODULE
#error("Either DPDK_MODULE or PCAP_MODULE is defined but must not all of them")
#endif


int main( int argc, char** argv ){
	signal(SIGINT,  signal_handler);
	signal(SIGTERM, signal_handler);
	signal(SIGSEGV, signal_handler);
	signal(SIGABRT, signal_handler);

	log_open();

	IF_ENABLE_DEBUG(
			log_write( LOG_WARNING, "Must not run debug mode in production environment" );
	)

#ifdef DPDK_MODULE
	int ret = rte_eal_init(argc, argv);
	if (ret < 0)
		rte_exit_failure("Error with EAL initialization\n");

	argc -= ret;
	argv += ret;
#endif

	//init context
	context.is_aborting = false;
	context.config = _parse_options(argc, argv);

#ifdef SECURITY_MODULE
	if( context.config->reports.security != NULL )
		security_open( context.config->reports.security->excluded_rules );
#endif

	log_write( LOG_INFO, "MMT-Probe v%s is running on pid %d",
			get_version(),
			getpid() );
	log_write( LOG_INFO, "Modules: %s", MODULES_LIST );

	//DPI initialization
	if( !init_extraction() ) { // general ixE initialization
		log_write( LOG_ERR, "MMT Extraction engine initialization error! Exiting!");
		return EXIT_FAILURE;
	}else
		log_write( LOG_INFO, "MMT-DPI %s", mmt_version() );

	//other stubs, such as, system usage report
	routine_t *routine = routine_create_and_start( &context );

#ifdef DPDK_MODULE
	dpdk_capture_start( &context );
#else
	pcap_capture_start( &context );
#endif

	//end
	close_extraction();


	IF_ENABLE_SECURITY(
		security_close();
	)

	routine_stop_and_release( routine );

	log_close();

	conf_release( context.config );

	printf("Bye\n");

	return EXIT_SUCCESS;
}
