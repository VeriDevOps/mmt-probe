/*
 * configure.c
 *
 *  Created on: Dec 12, 2017
 *          by: Huu Nghia
 */

#include <confuse.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include "lib/log.h"
#include "lib/malloc.h"
#include "lib/memory.h"

#include "configure.h"

#define DLT_EN10MB 1/**< Ethernet (10Mb) */

//bool conf_parse_security_ignore_mode( int *result, const char *string ){
//	if (IS_EQUAL_STRINGS(string, "NONE") )
//		*result = SEC_IGNORE_FLOW_NONE;
//	else if (IS_EQUAL_STRINGS(string, "SAME-RULE") )
//		*result = SEC_IGNORE_FLOW_SAME_RULE;
//	else if (IS_EQUAL_STRINGS(string, "ALL-RULE") )
//			*result = SEC_IGNORE_FLOW_ALL_RULE;
//	else {
//		return false;
//	}
//	return true;
//}
//
//static int _conf_parse_security_ignore_mode(cfg_t *cfg, cfg_opt_t *opt, const char *value, void *result) {
//	if( conf_parse_security_ignore_mode(result, value) )
//		return 0;
//	cfg_error(cfg, "invalid value for option '%s': %s", cfg_opt_name(opt), value);
//	return -1;
//}

//parse forward default-aciton
bool conf_parse_forward_default_action(int *result, const char *value) {
	if (IS_EQUAL_STRINGS(value, "FORWARD") )
		*result = ACTION_FORWARD;
	else if (IS_EQUAL_STRINGS(value, "DROP") )
		*result = ACTION_DROP;
	else {
		return false;
	}
	return true;
}

static int _conf_parse_forward_default_action(cfg_t *cfg, cfg_opt_t *opt, const char *value, void *result) {
	if ( conf_parse_forward_default_action( result, value) )
		return 0;
	cfg_error(cfg, "invalid value for option '%s': %s. Expected either FORWARD or DROP.", cfg_opt_name(opt), value);
	return -1;
}

// parse values for the input-mode option
bool conf_parse_input_mode(int *result, const char *value) {
	if (IS_EQUAL_STRINGS(value, "ONLINE") )
		*result = ONLINE_ANALYSIS;
	else if (IS_EQUAL_STRINGS(value, "OFFLINE") )
		*result = OFFLINE_ANALYSIS;
	else {
		return false;
	}
	return true;
}

static int _conf_parse_input_mode(cfg_t *cfg, cfg_opt_t *opt, const char *value, void *result) {
	if ( conf_parse_input_mode( result, value) )
		return 0;
	cfg_error(cfg, "invalid value for option '%s': %s. Expected either ONLINE or OFFLINE.", cfg_opt_name(opt), value);
	return -1;
}

/* parse values for the rtt-base option */
bool conf_parse_rtt_base(int *result, const char *value) {
	if (IS_EQUAL_STRINGS(value, "PREFER_SENDER") )
		*result = CONF_RTT_BASE_PREFER_SENDER;
	else if (IS_EQUAL_STRINGS(value, "SENDER") )
		*result = CONF_RTT_BASE_SENDER;
	else if (IS_EQUAL_STRINGS(value, "CAPTOR") )
		*result = CONF_RTT_BASE_CAPTOR;
	else
		return false;
	return true;
}

static int _conf_parse_rtt_base(cfg_t *cfg, cfg_opt_t *opt, const char *value, void *result) {
	if ( conf_parse_rtt_base( result, value) )
		return 0;
	cfg_error(cfg, "invalid value for option '%s': %s. Expected either PREFER_SENDER, SENDER, or CAPTOR.", cfg_opt_name(opt), value);
	return -1;
}

/* parse values for the output format option */
int conf_parse_output_format(cfg_t *cfg, cfg_opt_t *opt, const char *value, void *result) {
	if( value == NULL || value[0] == '\0' || strcasecmp(value, "CSV") == 0)
		*(int *) result = OUTPUT_FORMAT_CSV;
	else if (strcasecmp(value, "JSON") == 0)
		*(int *) result = OUTPUT_FORMAT_JSON;
	else {
		cfg_error(cfg, "Invalid '%s' for option '%s'. Use either CSV or JSON.", value, cfg_opt_name(opt));
		return -1;
	}
	return 0;
}

static int _conf_parse_socket_type(cfg_t *cfg, cfg_opt_t *opt, const char *value, void *result) {
	if (IS_EQUAL_STRINGS(value, "UNIX") )
		*(int *) result = SOCKET_TYPE_UNIX;
	else if (IS_EQUAL_STRINGS(value, "INTERNET") )
		*(int *) result = SOCKET_TYPE_INTERNET;
	else if (IS_EQUAL_STRINGS(value, "BOTH") )
		*(int *) result = SOCKET_TYPE_ANY;
	else{
		cfg_error(cfg, "invalid value for option '%s': %s", cfg_opt_name(opt), value);
		return -1;
	}
	return 0;
}


static int _conf_parse_ip_encapsulation_index(cfg_t *cfg, cfg_opt_t *opt, const char *value, void *result) {
	if (IS_EQUAL_STRINGS(value, "FIRST") )
		*(int *) result = CONF_IP_ENCAPSULATION_INDEX_FIRST;
	else if (IS_EQUAL_STRINGS(value, "LAST") )
		*(int *) result = CONF_IP_ENCAPSULATION_INDEX_LAST;
	else{
		int val = atoi( value );
		if( val >= CONF_IP_ENCAPSULATION_INDEX_FIRST ){
			if( val <= CONF_IP_ENCAPSULATION_INDEX_LAST )
				*(int *) result = val;
			else
				*(int *) result = CONF_IP_ENCAPSULATION_INDEX_LAST;
		}else{
			cfg_error(cfg, "invalid value for option '%s': %s", cfg_opt_name(opt), value);
			return -1;
		}
	}
	DEBUG( "security.ip-encapsulation-index = %d", *(int *) result );
	return 0;
}

static inline cfg_t *_load_cfg_from_file(const char *filename) {
	cfg_opt_t micro_flows_opts[] = {
			CFG_BOOL("enable", false, CFGF_NONE),
			CFG_INT("packet-threshold", 10, CFGF_NONE),
			CFG_INT("byte-threshold", 5, CFGF_NONE),
			CFG_INT("report-packet-count", 10000, CFGF_NONE),
			CFG_INT("report-byte-count", 5000, CFGF_NONE),
			CFG_INT("report-flow-count", 1000, CFGF_NONE),
			CFG_STR_LIST("output-channel", "{}", CFGF_NONE),
			CFG_END()
	};
	cfg_opt_t session_timeout_opts[] = {
			CFG_INT("default-session-timeout", 60, CFGF_NONE),
			CFG_INT("long-session-timeout", 600, CFGF_NONE),
			CFG_INT("short-session-timeout", 15, CFGF_NONE),
			CFG_INT("live-session-timeout", 1500, CFGF_NONE),
			CFG_END()
	};

	cfg_opt_t redis_output_opts[] = {
			CFG_STR("hostname", "localhost", CFGF_NONE),
			CFG_STR("channel", "report", CFGF_NONE),
			CFG_INT("port", 6379, CFGF_NONE),
			CFG_BOOL("enable", false, CFGF_NONE),
			CFG_END()
	};

	cfg_opt_t kafka_output_opts[] = {
			CFG_STR("hostname", "localhost", CFGF_NONE),
			CFG_STR("topic", "report", CFGF_NONE),
			CFG_INT("port", 9092, CFGF_NONE),
			CFG_BOOL("enable", false, CFGF_NONE),
			CFG_END()
	};

	cfg_opt_t mongodb_output_opts[] = {
			CFG_STR("hostname", "localhost", CFGF_NONE),
			CFG_INT("port", 27017, CFGF_NONE),
			CFG_BOOL("enable", false, CFGF_NONE),
			CFG_STR("database", "mmt-data", CFGF_NONE),
			CFG_STR("collection", "reports", CFGF_NONE),
			CFG_INT("limit-size", 0, CFGF_NONE),
			CFG_END()
	};

	cfg_opt_t dynamic_conf_opts[] = {
			CFG_STR("descriptor", "", CFGF_NONE),
			CFG_BOOL("enable", false, CFGF_NONE),
			CFG_END()
	};
	cfg_opt_t file_output_opts[] = {
			CFG_BOOL("enable", false, CFGF_NONE),
			CFG_STR("output-file", 0, CFGF_NONE),
			CFG_STR("output-dir", 0, CFGF_NONE),
			CFG_INT("retain-files", 0, CFGF_NONE),
			CFG_BOOL("sample-file", true, CFGF_NONE),
			CFG_END()
	};

	cfg_opt_t dump_pcap_opts[] = {
			CFG_BOOL("enable", false, CFGF_NONE),
			CFG_STR("output-dir", 0, CFGF_NONE),
			CFG_STR_LIST("protocols", "{}", CFGF_NONE),
			CFG_INT("period", 0, CFGF_NONE),
			CFG_INT("retain-files", 0, CFGF_NONE),
			CFG_INT("snap-len", 0, CFGF_NONE),
			CFG_END()
		};

	cfg_opt_t forward_packet_opts[] = {
			CFG_BOOL("enable", false, CFGF_NONE),
			CFG_STR("output-nic", 0, CFGF_NONE),
			CFG_INT("snap-len", 0, CFGF_NONE),
			CFG_INT("nb-copies", 1, CFGF_NONE),
			CFG_INT("promisc", 0, CFGF_NONE),
			CFG_INT_CB("default", 0, CFGF_NONE, _conf_parse_forward_default_action),
			CFG_STR_LIST("target-protocols", "{}", CFGF_NONE),
			CFG_STR_LIST("target-hosts", "{}", CFGF_NONE),
			CFG_STR_LIST("target-ports", "{}", CFGF_NONE),
			CFG_END()
		};


	cfg_opt_t security2_opts[] = {
			CFG_BOOL("enable", false, CFGF_NONE),
			CFG_BOOL("report-rule-description", true, CFGF_NONE),
			CFG_INT("thread-nb",    0, CFGF_NONE),
			CFG_STR("rules-mask",   0, CFGF_NONE),
			CFG_STR("exclude-rules",   0, CFGF_NONE),
			CFG_STR_LIST("output-channel", "{}", CFGF_NONE),

			CFG_INT("input.max-message-size",    0, CFGF_NONE),
			CFG_INT("security.max-instances",    0, CFGF_NONE),
			CFG_INT("security.smp.ring-size",    0, CFGF_NONE),
			CFG_BOOL("ignore-remain-flow",     0, CFGF_NONE ),
			CFG_INT_CB("ip-encapsulation-index", 0, CFGF_NONE, _conf_parse_ip_encapsulation_index),
			CFG_END()
	};

	cfg_opt_t cpu_mem_report_opts[] = {
			CFG_BOOL("enable", false, CFGF_NONE),
			CFG_INT("period", 0, CFGF_NONE),
			CFG_STR_LIST("output-channel", "{}", CFGF_NONE),
			CFG_END()
	};

	cfg_opt_t reconstruct_data_opts[] = {
			CFG_BOOL("enable", false, CFGF_NONE),
			CFG_STR("output-dir", 0, CFGF_NONE),
			CFG_STR_LIST("output-channel", "{}", CFGF_NONE),
			CFG_END()
	};

	cfg_opt_t radius_output_opts[] = {
			CFG_BOOL("enable", false, CFGF_NONE),
			CFG_INT("message-id", 0, CFGF_NONE),
			CFG_STR_LIST("output-channel", "{}", CFGF_NONE),
			CFG_END()
	};

	cfg_opt_t data_output_opts[] = {
			CFG_INT("include-user-agent", MMT_USER_AGENT_THRESHOLD, CFGF_NONE),
			CFG_END()
	};

	cfg_opt_t event_report_opts[] = {
			CFG_BOOL("enable", false, CFGF_NONE),
			CFG_STR("event", "", CFGF_NONE),
			CFG_STR_LIST("attributes", "{}", CFGF_NONE),
			CFG_STR_LIST("output-channel", "{}", CFGF_NONE),
			CFG_END()
	};

	cfg_opt_t socket_opts[] = {
			CFG_BOOL("enable", false, CFGF_NONE),
			CFG_INT_CB("type", 0, CFGF_NONE, _conf_parse_socket_type),
			CFG_INT("port", 0, CFGF_NONE),
			CFG_STR("hostname", "localhost", CFGF_NONE),
			CFG_STR("descriptor", "/tmp/probe-output.sock", CFGF_NONE),
			CFG_END()
	};
	cfg_opt_t session_report_opts[] = {
			CFG_BOOL("enable", false, CFGF_NONE),
			CFG_BOOL("ftp", false, CFGF_NONE),
			CFG_BOOL("rtp", false, CFGF_NONE),
			CFG_BOOL("http", false, CFGF_NONE),
			CFG_BOOL("ssl", false, CFGF_NONE),
			CFG_BOOL("gtp", false, CFGF_NONE),
			CFG_INT_CB("rtt-base", CONF_RTT_BASE_SENDER, CFGF_NONE, _conf_parse_rtt_base),
			CFG_STR_LIST("output-channel", "{}", CFGF_NONE),
			CFG_END()
	};

	cfg_opt_t input_opts[] = {
			CFG_INT_CB("mode", 0, CFGF_NONE, _conf_parse_input_mode),
			CFG_STR("source", "", CFGF_NONE),
			CFG_INT("snap-len", 65535, CFGF_NONE),
			CFG_STR("dpdk-option", "", CFGF_NONE), //keep this for compatibility
			CFG_END()
	};

	cfg_opt_t output_opts[] = {
			CFG_INT_CB("format", OUTPUT_FORMAT_CSV, CFGF_NONE, conf_parse_output_format),
			CFG_INT("cache-max", 1000, CFGF_NONE),
			CFG_INT("cache-period", 5, CFGF_NONE),
			CFG_END()
		};

	cfg_opt_t opts[] = {
			CFG_SEC("input", input_opts, CFGF_NONE),
			CFG_SEC("micro-flows", micro_flows_opts, CFGF_NONE),
			CFG_SEC("session-timeout", session_timeout_opts, CFGF_NONE),
			CFG_SEC("output", output_opts, CFGF_NONE),

			CFG_SEC("file-output", file_output_opts, CFGF_NONE),
			CFG_SEC("redis-output", redis_output_opts, CFGF_NONE),
			CFG_SEC("kafka-output", kafka_output_opts, CFGF_NONE),
			CFG_SEC("data-output", data_output_opts, CFGF_NONE),
			CFG_SEC("socket-output", socket_opts, CFGF_NONE),

			CFG_SEC("security", security2_opts, CFGF_NONE),
			CFG_SEC("system-report", cpu_mem_report_opts, CFGF_NONE),

			CFG_SEC("behaviour", file_output_opts, CFGF_NONE),
			CFG_SEC("reconstruct-data", reconstruct_data_opts, CFGF_TITLE | CFGF_MULTI ),
			CFG_SEC("mongodb-output", mongodb_output_opts, CFGF_NONE),
			CFG_SEC("radius-report", radius_output_opts, CFGF_NONE),
			CFG_SEC("dump-pcap", dump_pcap_opts, CFGF_NONE),
			CFG_SEC("forward-packet", forward_packet_opts, CFGF_NONE),

			CFG_INT("stats-period", 5, CFGF_NONE),
			CFG_BOOL("enable-proto-without-session-report", false, CFGF_NONE),
			CFG_BOOL("enable-ip-fragmentation-report", false, CFGF_NONE),
			CFG_BOOL("enable-ip-defragmentation", false, CFGF_NONE),
			CFG_BOOL("enable-tcp-reassembly", false, CFGF_NONE),

			CFG_INT("thread-nb", 1, CFGF_NONE),
			CFG_INT("thread-queue", 0, CFGF_NONE),
			CFG_INT("probe-id", 0, CFGF_NONE),
			CFG_INT("stack-type", DLT_EN10MB, CFGF_NONE),

			CFG_STR("logfile", 0, CFGF_NONE),
			CFG_STR("license", 0, CFGF_NONE),
			CFG_INT("loglevel", 2, CFGF_NONE),
			CFG_STR("dpdk-option", 0, CFGF_NONE),

			CFG_SEC("event-report", event_report_opts, CFGF_TITLE | CFGF_MULTI),
			CFG_SEC("session-report", session_report_opts, CFGF_NONE),
			CFG_SEC("dynamic-config", dynamic_conf_opts, CFGF_NONE),
			CFG_END()
	};

	cfg_t *cfg = cfg_init(opts, CFGF_NONE);
	switch (cfg_parse(cfg, filename)) {
	case CFG_FILE_ERROR:
		//log_write(LOG_ERR, "Error: configuration file '%s' could not be read: %s\n", filename, strerror(errno));
		cfg_free( cfg );
		return NULL;
	case CFG_SUCCESS:
		break;
	case CFG_PARSE_ERROR:
		log_write(LOG_ERR, "Error: configuration file '%s' could not be parsed.\n", filename );
		cfg_free( cfg );
		return NULL;
	}

	return cfg;
}

static inline char * _cfg_get_str( cfg_t *cfg, const char *header ){
	const char *str = cfg_getstr( cfg, header );
	if (str == NULL)
		return NULL;
	return mmt_strdup( str );
}

static inline char * _cfg_get_dir( cfg_t *cfg, const char *header ){
	const char *str = cfg_getstr( cfg, header );
	if (str == NULL)
		return NULL;
	size_t len = strlen( str );
	//ensure that a directory path is always end by '/'
	char *dir = mmt_alloc( len + 1 + 1 ); //+1 for '\0'; +1 for eventually '/'
	memcpy( dir, str, len + 1 ); //+1 for '\0'

	if( dir[ len - 1 ] != '/' ){
		dir[ len ]    = '/';  //append '/' if it is not there
		dir[ len + 1] = '\0'; //ensure NULL-terminated
	}
	return dir;
}

static inline cfg_t* _get_first_cfg_block( cfg_t *cfg, const char* block_name ){
	if( ! cfg_size( cfg, block_name) )
		return NULL;
	//DEBUG( "Parsing block '%s'", block_name );
	return cfg_getnsec( cfg, block_name, 0 );
}


static inline long int _cfg_getint( cfg_t *cfg, const char *ident, long int min, long int max, long int def_val, long int replaced_val ){
	long int val = cfg_getint( cfg, ident );
	if( val < min || val > max ){
		log_write( LOG_WARNING, "Not expected %ld for %s. Used default value %ld.", val, ident, replaced_val );
		return replaced_val;
	}

	if( val == def_val ){
		log_write( LOG_INFO, "Used default value %ld for %s", replaced_val, ident );
		return replaced_val;
	}
	return val;
}

static inline input_source_conf_t * _parse_input_source( cfg_t *cfg ){
	cfg = _get_first_cfg_block( cfg, "input" );
	if( cfg == NULL )
		return NULL;

	input_source_conf_t *ret = mmt_alloc( sizeof( input_source_conf_t ));

	ret->input_mode   = cfg_getint(cfg, "mode");
	ret->input_source = _cfg_get_str(cfg, "source");

#ifndef DPDK_CAPTURE_MODULE
#ifndef PCAP_CAPTURE_MODULE
#ifndef STREAM_CAPTURE_MODULE
	#error("Neither DPDK nor PCAP nor STREAM is defined")
#endif
#endif
#endif

#if defined DPDK_CAPTURE_MODULE && defined PCAP_CAPTURE_MODULE && defined STREAM_CAPTURE_MODULE
	#error("Either DPDK_CAPTURE_MODULE or PCAP_CAPTURE_MODULE or STREAM_CAPTURE_MODULE is defined but must not all of them")
#endif


#ifdef DPDK_CAPTURE_MODULE
	ret->capture_mode = DPDK_CAPTURE;
#endif

#ifdef PCAP_CAPTURE_MODULE
	ret->capture_mode = PCAP_CAPTURE;
#endif

	ret->snap_len = cfg_getint( cfg, "snap-len" );

	if( ret->snap_len == 0 )
		ret->snap_len = UINT16_MAX;

	return ret;
}


static inline file_output_conf_t *_parse_output_to_file( cfg_t *cfg ){
	cfg_t * c = _get_first_cfg_block( cfg, "file-output" );
	if( c == NULL )
		return NULL;

	file_output_conf_t *ret = mmt_alloc( sizeof( file_output_conf_t ));

	ret->is_enable  = cfg_getbool( c, "enable" );
	ret->directory  = _cfg_get_dir(c, "output-dir");
	ret->filename   = _cfg_get_str(c, "output-file");
	ret->is_sampled    = cfg_getbool(c, "sample-file");
	ret->retained_files_count = cfg_getint( c, "retain-files" );

	return ret;
}

static inline pcap_dump_conf_t *_parse_dump_to_file( cfg_t *cfg ){
	cfg = _get_first_cfg_block( cfg, "dump-pcap" );
	if( cfg == NULL )
		return NULL;

	pcap_dump_conf_t *ret = mmt_alloc( sizeof( pcap_dump_conf_t ));

	ret->is_enable  = cfg_getbool( cfg, "enable" );
	ret->directory  = _cfg_get_dir(cfg, "output-dir");
	ret->frequency  = cfg_getint( cfg, "period");
	ret->retained_files_count = cfg_getint( cfg, "retain-files" );
	ret->snap_len = cfg_getint( cfg, "snap-len" );

	ret->protocols_size = cfg_size( cfg, "protocols");

	ret->protocols = mmt_alloc( sizeof( void* ) * ret->protocols_size );
	int i;
	char *str;
	for( i=0; i<ret->protocols_size; i++) {
		str = cfg_getnstr(cfg, "protocols", i);
		ret->protocols[i] = mmt_strdup( str );
	}
	return ret;
}

static inline forward_packet_conf_t *_parse_forward_packet( cfg_t *cfg ){
	cfg = _get_first_cfg_block( cfg, "forward-packet" );
	if( cfg == NULL )
		return NULL;

	forward_packet_conf_t *ret = mmt_alloc( sizeof( forward_packet_conf_t ));

	ret->is_enable  = cfg_getbool( cfg, "enable" );
	ret->output_nic = _cfg_get_str(cfg, "output-nic");
	ret->snap_len = cfg_getint( cfg, "snap-len" );
	ret->nb_copies = cfg_getint( cfg, "nb-copies" );
	ret->promisc = cfg_getint( cfg, "promisc" );
	ret->default_action = cfg_getint( cfg, "default" );

	ret->target_size = cfg_size( cfg, "target-protocols");
	ASSERT( ret->target_size == cfg_size( cfg, "target-hosts"), "Number of elements in target-protocols and target-hosts are different");
	ASSERT( ret->target_size == cfg_size( cfg, "target-ports"), "Number of elements in target-protocols and target-ports are different");

	ret->targets = mmt_alloc( sizeof( forward_packet_target_conf_t ) * ret->target_size );
	int i;
	char *str;
	for( i=0; i<ret->target_size; i++) {
		//protocol
		str = cfg_getnstr(cfg, "target-protocols", i);
		if( IS_EQUAL_STRINGS(str, "SCTP") )
			ret->targets[i].protocol = FORWARD_PACKET_PROTO_SCTP;
		else if( IS_EQUAL_STRINGS(str, "UDP") )
			ret->targets[i].protocol = FORWARD_PACKET_PROTO_UDP;
		else
			ABORT("Does not support yet the protocol: %s", str);
		//host
		str = cfg_getnstr(cfg, "target-hosts", i);
		ret->targets[i].host = mmt_strdup( str );
		//port
		//ret->targets[i].port = cfg_getnint(cfg, "target-ports", i);
		//do not understand why ret->targets[i].port is zero
		str = cfg_getnstr(cfg, "target-ports", i);
		ret->targets[i].port = atoi( str );
	}
	return ret;
}

static inline kafka_output_conf_t *_parse_output_to_kafka( cfg_t *cfg ){
	if( (cfg = _get_first_cfg_block( cfg, "kafka-output")) == NULL )
		return NULL;

	kafka_output_conf_t *ret = mmt_alloc( sizeof( kafka_output_conf_t ));

	ret->is_enable        = cfg_getbool( cfg,  "enable" );
	ret->host.host_name   = _cfg_get_str(cfg, "hostname");
	ret->host.port_number = cfg_getint( cfg,  "port" );
	ret->topic_name       = _cfg_get_str(cfg, "topic");

	return ret;
}

static inline mongodb_output_conf_t *_parse_output_to_mongodb( cfg_t *cfg ){
	if( (cfg = _get_first_cfg_block( cfg, "mongodb-output")) == NULL )
		return NULL;

	mongodb_output_conf_t *ret = mmt_alloc( sizeof( mongodb_output_conf_t ));

	ret->is_enable        = cfg_getbool( cfg,  "enable" );
	ret->host.host_name   = _cfg_get_str(cfg, "hostname");
	ret->host.port_number = cfg_getint( cfg,  "port" );
	ret->database_name    = _cfg_get_str(cfg, "database");
	ret->collection_name  = _cfg_get_str(cfg, "collection");
	ret->limit_size       = cfg_getint( cfg,  "limit-size" );

	return ret;
}

static inline redis_output_conf_t *_parse_output_to_redis( cfg_t *cfg ){
	if( (cfg = _get_first_cfg_block( cfg, "redis-output")) == NULL )
		return NULL;

	redis_output_conf_t *ret = mmt_alloc( sizeof( redis_output_conf_t ));

	ret->is_enable        = cfg_getbool( cfg, "enable" );
	ret->host.host_name   = _cfg_get_str(cfg, "hostname");
	ret->channel_name     = _cfg_get_str(cfg, "channel");
	ret->host.port_number = cfg_getint( cfg,  "port" );

	return ret;
}

static inline dynamic_config_conf_t *_parse_dynamic_config_block( cfg_t *cfg ){
	cfg_t * c = _get_first_cfg_block( cfg, "dynamic-config" );
	if( c == NULL )
		return NULL;

	dynamic_config_conf_t *ret = mmt_alloc( sizeof( dynamic_config_conf_t ));

	ret->is_enable  = cfg_getbool( c, "enable" );
	ret->descriptor  = _cfg_get_str(c, "descriptor");

	return ret;
}


static inline multi_thread_conf_t * _parse_thread( cfg_t *cfg ){
	multi_thread_conf_t *ret = mmt_alloc( sizeof( multi_thread_conf_t ));
	ret->thread_count                  = cfg_getint( cfg, "thread-nb" );
	ret->thread_queue_packet_threshold = cfg_getint( cfg, "thread-queue" );
	return ret;
}

static inline file_output_conf_t *_parse_behaviour_block( cfg_t *cfg ){
	cfg_t * c = _get_first_cfg_block( cfg, "behaviour" );
	if( c == NULL )
		return NULL;

	file_output_conf_t *ret = mmt_alloc( sizeof( file_output_conf_t ));

	ret->is_enable  = cfg_getbool( c, "enable" );
	ret->directory  = _cfg_get_dir(c, "output-dir");
	ret->filename   = _cfg_get_str(c, "output-file");
	ret->is_sampled    = true;
	ret->retained_files_count = cfg_getint( c, "retain-files" );

	return ret;
}

static inline  output_channel_conf_t _parse_output_channel( cfg_t *cfg ){
	int nb_output_channel = cfg_size( cfg, "output-channel");
	int i;
	const char *channel_name;

	output_channel_conf_t out = CONF_OUTPUT_CHANNEL_FILE; //default is to output to file

	for( i=0; i<nb_output_channel; i++) {
		channel_name = cfg_getnstr(cfg, "output-channel", i);
		if ( strncmp( channel_name, "file", 4 ) == 0 )
			out |= CONF_OUTPUT_CHANNEL_FILE;
		else if ( strncmp( channel_name, "kafka", 5 ) == 0 )
			out |= CONF_OUTPUT_CHANNEL_KAFKA;
		else if ( strncmp( channel_name, "redis", 5 ) == 0 )
			out |= CONF_OUTPUT_CHANNEL_REDIS;
		else if ( strncmp( channel_name, "mongodb", 5 ) == 0 )
			out |= CONF_OUTPUT_CHANNEL_MONGODB;
		else if ( strncmp( channel_name, "socket", 5 ) == 0 )
			out |= CONF_OUTPUT_CHANNEL_SOCKET;
		else
			log_write( LOG_WARNING, "Unexpected channel '%s'", channel_name );
	}
	return out;
}

output_channel_conf_t conf_parse_output_channel( const char *string ){
	output_channel_conf_t out = CONF_OUTPUT_CHANNEL_FILE; //default is to output to file

	const size_t len = strlen(string) +  sizeof("output-channel={}");
	char buffer[ len ];
	//put string in form
	snprintf( buffer, len, "output-channel={%s}", string );

	cfg_opt_t opts[] = {
			CFG_STR_LIST("output-channel", "{}", CFGF_NONE),
			CFG_END()
	};

	cfg_t *cfg = cfg_init( opts, CFGF_NONE );
	if( cfg_parse_buf(cfg, buffer) == CFG_PARSE_ERROR )
		log_write(LOG_ERR, "Error: output-channel '%s' could not be parsed.", string );
	else
		out = _parse_output_channel( cfg );

	cfg_free( cfg );
	return out;
}

size_t conf_parse_list( const char *string, char ***proto_lst ){
	size_t ret = 0;
	int i;
	char **lst;
	const char *str;
	const size_t len = strlen(string) +  sizeof("X={}");
	char buffer[ len ];
	ASSERT( proto_lst != NULL, "Must not be NULL" );
	//put string in form
	snprintf( buffer, len, "X={%s}", string );

	cfg_opt_t opts[] = {
			CFG_STR_LIST("X", "{}", CFGF_NONE),
			CFG_END()
	};

	cfg_t *cfg = cfg_init( opts, CFGF_NONE );
	if( cfg_parse_buf(cfg, buffer) == CFG_PARSE_ERROR )
		log_write(LOG_ERR, "Error: protocols '%s' could not be parsed.", string );
	else{
		ret = cfg_size( cfg, "X");

		lst = mmt_alloc( sizeof( void* ) * ret );

		for( i=0; i<ret; i++) {
			str = cfg_getnstr(cfg, "X", i);
			lst[i] = mmt_strdup( str );
		}
		*proto_lst = lst;
	}
	cfg_free( cfg );
	return ret;
}

static inline system_stats_conf_t *_parse_cpu_mem_block( cfg_t *cfg ){
	if( (cfg = _get_first_cfg_block( cfg, "system-report")) == NULL )
		return NULL;

	system_stats_conf_t *ret = mmt_alloc( sizeof( system_stats_conf_t ));
	ret->is_enable = cfg_getbool( cfg, "enable" );
	ret->frequency = cfg_getint( cfg, "period" );
	ret->output_channels = _parse_output_channel( cfg );
	return ret;
}

static inline void _parse_dpi_protocol_attribute( dpi_protocol_attribute_t * out, const char* str ){
	out->attribute_name = NULL;
	out->proto_name     = NULL;

	//str = HTTP.METHOD
	int index = 0;
	while( str[index] != '.' )
		index ++;
	//not found '.' even goto the end of string
	if( str[index] == '\0' )
		ABORT("Attribute [%s] is not well-formatted (must be in form proto.att, e.g., HTTP.METHOD)", str );

	out->proto_name     = mmt_strndup( str, index );
	out->attribute_name = mmt_strdup( str+index+1 ); //+1 to jump over .

}

static inline uint16_t _parse_attributes_helper( cfg_t *cfg, const char* name, dpi_protocol_attribute_t**atts ){
	int i, j;
	uint16_t size =  cfg_size( cfg, name );
	char *string;
	*atts = NULL;
	if( size == 0 )
		return size;

	dpi_protocol_attribute_t *ret = mmt_alloc_and_init_zero( sizeof( dpi_protocol_attribute_t ) * size );
	for( i=0; i<size; i++ ){
		string = cfg_getnstr( cfg, name, i );
		_parse_dpi_protocol_attribute( &ret[i], string );
	}

	*atts = ret;
	return size;
}




static inline void _parse_event_block( event_report_conf_t *ret, cfg_t *cfg ){
	int i;
	assert( cfg != NULL );
	ret->is_enable = cfg_getbool( cfg, "enable" );
	ret->title     = mmt_strdup( cfg_title(cfg) );
	ret->event = mmt_alloc( sizeof( dpi_protocol_attribute_t ));
	_parse_dpi_protocol_attribute( ret->event, cfg_getstr( cfg, "event" ) );

	ret->attributes_size = _parse_attributes_helper( cfg, "attributes", &ret->attributes );

	ret->output_channels = _parse_output_channel( cfg );
}

static inline micro_flow_conf_t *_parse_microflow_block( cfg_t *cfg ){
	if( (cfg = _get_first_cfg_block( cfg, "micro-flows")) == NULL )
		return NULL;

	micro_flow_conf_t *ret = mmt_alloc( sizeof( micro_flow_conf_t ));
	ret->is_enable             = cfg_getbool( cfg, "enable" );
	ret->byte_threshold   = cfg_getint( cfg, "byte-threshold" );
	ret->packet_threshold = cfg_getint( cfg, "packet-threshold" );
	ret->report_bytes_count    = cfg_getint( cfg, "report-byte-count" );
	ret->report_packets_count = cfg_getint( cfg, "report-packet-count" );
	ret->report_flows_count    = cfg_getint( cfg, "report-flow-count" );
	ret->output_channels = _parse_output_channel( cfg );
	return ret;
}


static inline security_conf_t *_parse_security_block( cfg_t *cfg ){
	if( (cfg = _get_first_cfg_block( cfg, "security")) == NULL )
		return NULL;

	security_conf_t *ret = mmt_alloc( sizeof( security_conf_t ));
	ret->is_enable = cfg_getbool( cfg, "enable" );
	ret->is_report_rule_description = cfg_getbool( cfg, "report-rule-description" );
	ret->threads_size = cfg_getint( cfg, "thread-nb" );
	ret->excluded_rules = _cfg_get_str(cfg, "exclude-rules" );
	ret->rules_mask = _cfg_get_str(cfg, "rules-mask" );
	ret->output_channels = _parse_output_channel( cfg );

	ret->lib.security_max_instances = cfg_getint( cfg, "security.max-instances" );
	ret->lib.security_smp_ring_size = cfg_getint( cfg, "security.smp.ring-size" );
	ret->lib.input_max_message_size = cfg_getint( cfg, "input.max-message-size" );
	ret->ignore_remain_flow         = cfg_getbool( cfg, "ignore-remain-flow");
	ret->ip_encapsulation_index     = cfg_getint(  cfg, "ip-encapsulation-index" );

	return ret;
}

static inline radius_report_conf_t *_parse_radius_block( cfg_t *cfg ){
	if( (cfg = _get_first_cfg_block( cfg, "radius-report")) == NULL )
		return NULL;

	radius_report_conf_t *ret = mmt_alloc( sizeof( radius_report_conf_t ));
	ret->is_enable = cfg_getbool( cfg, "enable" );
	ret->message_code = cfg_getint( cfg, "message-id" );
	ret->output_channels = _parse_output_channel( cfg );
	return ret;
}

static inline session_report_conf_t *_parse_session_block( cfg_t *cfg ){
	if( (cfg = _get_first_cfg_block( cfg, "session-report")) == NULL )
		return NULL;

	session_report_conf_t *ret = mmt_alloc( sizeof( session_report_conf_t ));
	ret->is_enable = cfg_getbool( cfg, "enable" );
	ret->is_ftp    = cfg_getbool( cfg, "ftp" );
	ret->is_rtp    = cfg_getbool( cfg, "rtp" );
	ret->is_http   = cfg_getbool( cfg, "http" );
	ret->is_ssl    = cfg_getbool( cfg, "ssl" );
	ret->is_gtp    = cfg_getbool( cfg, "gtp" );
	ret->rtt_base  = cfg_getint( cfg, "rtt-base" );
	ret->output_channels = _parse_output_channel( cfg );
	return ret;
}

static inline session_timeout_conf_t *_parse_session_timeout_block( cfg_t *cfg ){
	if( (cfg = _get_first_cfg_block( cfg, "session-timeout")) == NULL )
		return NULL;

	session_timeout_conf_t *ret = mmt_alloc( sizeof( session_timeout_conf_t ));
	ret->default_session_timeout = _cfg_getint( cfg, "default-session-timeout", 0, 60000, 0,   60 );
	ret->live_session_timeout    = _cfg_getint( cfg, "live-session-timeout",    0, 60000, 0, 1500 );
	ret->long_session_timeout    = _cfg_getint( cfg, "long-session-timeout",    0, 60000, 0,  600 );
	ret->short_session_timeout   = _cfg_getint( cfg, "short-session-timeout",   0, 60000, 0,   15 );
	return ret;
}

static inline socket_output_conf_t *_parse_socket_block( cfg_t *cfg ){
	int i;
	cfg_t *c = _get_first_cfg_block( cfg, "socket-output");
	if( c == NULL )
		return NULL;

	socket_output_conf_t *ret = mmt_alloc( sizeof( socket_output_conf_t ));
	ret->is_enable = cfg_getbool( c, "enable" );
	ret->socket_type = cfg_getint( c, "type");
	ret->unix_socket_descriptor = _cfg_get_str(c, "descriptor" );

	ret->internet_socket.port_number = cfg_getint( c, "port" );
	ret->internet_socket.host_name = _cfg_get_str(c, "hostname" );

	return ret;
}


static inline reconstruct_data_conf_t *_parse_reconstruct_data_block( cfg_t *cfg, const char *name ){
	int size = cfg_size( cfg, "reconstruct-data");
	int i;
	cfg_t *c;

	if( size == 0 ){
		log_write( LOG_ERR, "Expected reconstruct-data blocks" );
		abort();
		return NULL;
	}

	for( i=0; i<size; i++ ){
		c = cfg_getnsec(cfg, "reconstruct-data", i );

		if( ! IS_EQUAL_STRINGS(name, cfg_title(c) ) )
			continue;

		DEBUG( "Parsing block 'reconstruct-data %s'", name );

		reconstruct_data_conf_t *ret = mmt_alloc( sizeof( reconstruct_data_conf_t ));
		ret->is_enable = cfg_getbool( c, "enable" );
		ret->directory = _cfg_get_dir(c, "output-dir" );
		ret->output_channels = _parse_output_channel( c );
		return ret;
	}
	return NULL;
}


static inline void _parse_output_block( output_conf_t *output, cfg_t *cfg ){
	cfg = _get_first_cfg_block( cfg, "output");
	if( cfg == NULL ){
		log_write( LOG_ERR, "Expected output block" );
		return;
	}

	output->cache_max = cfg_getint( cfg, "cache-max") ;
	output->cache_period = cfg_getint( cfg, "cache-period") ;
	output->format  = cfg_getint(cfg, "format");

	return;
}
/**
 * Public API
 * @param filename
 * @return
 */
probe_conf_t* conf_load_from_file( const char* filename ){
	const char *str;
	int i;
	cfg_t *cfg = _load_cfg_from_file( filename );
	if( cfg == NULL ){
		return NULL;
	}

	probe_conf_t *conf = mmt_alloc( sizeof( probe_conf_t ) );

	conf->probe_id     = cfg_getint(cfg, "probe-id");
	conf->stack_type   = cfg_getint(cfg, "stack-type");
	conf->stat_period  = cfg_getint(cfg, "stats-period");
	conf->license_file = _cfg_get_str(cfg, "license" );
	conf->dpdk_options  = _cfg_get_str(cfg, "dpdk-option" );

	conf->is_enable_proto_no_session_report  = cfg_getbool(cfg, "enable-proto-without-session-report");
	conf->is_enable_ip_fragmentation_report  = cfg_getbool(cfg, "enable-ip-fragmentation-report");
	conf->is_enable_ip_defragmentation       = cfg_getbool(cfg, "enable-ip-defragmentation");
	conf->is_enable_tcp_reassembly           = cfg_getbool(cfg, "enable-tcp-reassembly");

	conf->input = _parse_input_source( cfg );

	_parse_output_block( &conf->outputs, cfg );
	//set of output channels
	conf->outputs.file  = _parse_output_to_file( cfg );
	conf->outputs.kafka = _parse_output_to_kafka( cfg );
	conf->outputs.redis = _parse_output_to_redis( cfg );
	conf->outputs.mongodb = _parse_output_to_mongodb( cfg );
	conf->outputs.socket = _parse_socket_block( cfg );
	//a global
	conf->outputs.is_enable = ( (conf->outputs.file != NULL && conf->outputs.file->is_enable )
									|| (conf->outputs.mongodb != NULL && conf->outputs.mongodb->is_enable)
									|| (conf->outputs.redis != NULL && conf->outputs.redis->is_enable)
									|| (conf->outputs.kafka != NULL && conf->outputs.kafka->is_enable )
									|| (conf->outputs.socket != NULL && conf->outputs.socket->is_enable ));

	conf->dynamic_conf = _parse_dynamic_config_block( cfg );
	//
	conf->thread = _parse_thread( cfg );

	conf->reports.behaviour = _parse_behaviour_block( cfg );
	conf->reports.cpu_mem   = _parse_cpu_mem_block( cfg );

	//events reports
	conf->reports.events_size = cfg_size( cfg, "event-report" );
	conf->reports.events  = mmt_alloc( sizeof( event_report_conf_t ) * conf->reports.events_size );
	for( i=0; i<conf->reports.events_size; i++ )
		_parse_event_block( &conf->reports.events[i], cfg_getnsec( cfg, "event-report", i) );

	//
	conf->reports.microflow = _parse_microflow_block( cfg );

	conf->reports.security = _parse_security_block( cfg );
	conf->reports.session = _parse_session_block( cfg );


	conf->reports.pcap_dump = _parse_dump_to_file(cfg);
	conf->forward_packet = _parse_forward_packet(cfg);
	conf->reports.radius   = _parse_radius_block( cfg );

	conf->session_timeout = _parse_session_timeout_block( cfg );

	//
	conf->reconstructions.ftp = _parse_reconstruct_data_block(cfg, "ftp");
	conf->reconstructions.http = _parse_reconstruct_data_block(cfg, "http");
	cfg_free( cfg );

	//validate default values
	if( conf->reports.cpu_mem->frequency == 0 )
		conf->reports.cpu_mem->frequency = 5;

	return conf;
}

static inline void _free_event_report( event_report_conf_t *ret ){
	if( ret == NULL )
		return;
	int i;
	for( i=0; i<ret->attributes_size; i++ ){
		mmt_probe_free( ret->attributes[i].proto_name );
		mmt_probe_free( ret->attributes[i].attribute_name );
	}
	mmt_probe_free( ret->attributes );
	mmt_probe_free( ret->title );
	mmt_probe_free( ret->event->proto_name );
	mmt_probe_free( ret->event->attribute_name );
	mmt_probe_free( ret->event );
}

/**
 * Public API
 * Free all memory allocated by @load_configuration_from_file
 * @param
 */
void conf_release( probe_conf_t *conf){
	if( conf == NULL )
		return;

	int i;

	mmt_probe_free( conf->input->input_source );
	mmt_probe_free( conf->input );

	for( i=0; i<conf->reports.events_size; i++ )
		_free_event_report( &conf->reports.events[i] );
	mmt_probe_free( conf->reports.events );

	if( conf->reports.behaviour ){
		mmt_probe_free( conf->reports.behaviour->directory );
		mmt_probe_free( conf->reports.behaviour->filename );
		mmt_probe_free( conf->reports.behaviour );
	}

	mmt_probe_free( conf->reports.cpu_mem );
	mmt_probe_free( conf->reports.microflow );

	if( conf->reports.security ){
		mmt_probe_free( conf->reports.security->excluded_rules );
		mmt_probe_free( conf->reports.security->rules_mask );
		mmt_probe_free( conf->reports.security );
	}

	if( conf->forward_packet ){
		mmt_probe_free( conf->forward_packet->output_nic );
		for( i=0; i<conf->forward_packet->target_size; i++ ){
			mmt_probe_free( conf->forward_packet->targets[i].host );
		}
		mmt_probe_free( conf->forward_packet->targets );
		mmt_probe_free( conf->forward_packet );
	}

	mmt_probe_free( conf->reports.session );

	if( conf->reports.pcap_dump ){
		mmt_probe_free( conf->reports.pcap_dump->directory );
		for( i=0; i<conf->reports.pcap_dump->protocols_size; i++ )
			mmt_probe_free( conf->reports.pcap_dump->protocols[i] );

		mmt_probe_free( conf->reports.pcap_dump->protocols );
		mmt_probe_free( conf->reports.pcap_dump );
	}

	mmt_probe_free( conf->reports.radius );


	if( conf->reconstructions.ftp ){
		mmt_probe_free( conf->reconstructions.ftp->directory );
		mmt_probe_free( conf->reconstructions.ftp );
	}
	if( conf->reconstructions.http ){
		mmt_probe_free( conf->reconstructions.http->directory );
		mmt_probe_free( conf->reconstructions.http );
	}

	mmt_probe_free( conf->thread );
	if( conf->outputs.file ){
		mmt_probe_free( conf->outputs.file->directory );
		mmt_probe_free( conf->outputs.file->filename );
		mmt_probe_free( conf->outputs.file );
	}
	if( conf->outputs.kafka ){
		mmt_probe_free( conf->outputs.kafka->host.host_name );
		mmt_probe_free( conf->outputs.kafka->topic_name );
		mmt_probe_free( conf->outputs.kafka );
	}
	if( conf->outputs.redis ){
		mmt_probe_free( conf->outputs.redis->host.host_name );
		mmt_probe_free( conf->outputs.redis->channel_name );
		mmt_probe_free( conf->outputs.redis );
	}
	if( conf->outputs.mongodb ){
		mmt_probe_free( conf->outputs.mongodb->collection_name );
		mmt_probe_free( conf->outputs.mongodb->database_name );
		mmt_probe_free( conf->outputs.mongodb->host.host_name );
		mmt_probe_free( conf->outputs.mongodb );
	}

	if( conf->outputs.socket ){
		mmt_probe_free( conf->outputs.socket->unix_socket_descriptor );
		mmt_probe_free( conf->outputs.socket->internet_socket.host_name );
		mmt_probe_free( conf->outputs.socket );
	}

	if( conf->dynamic_conf ){
		mmt_probe_free( conf->dynamic_conf->descriptor );
		mmt_probe_free( conf->dynamic_conf );
	}


	mmt_probe_free( conf->session_timeout );

	mmt_probe_free( conf->license_file );
	mmt_probe_free( conf->dpdk_options );
	mmt_probe_free( conf );
}

int conf_validate( probe_conf_t *conf ){
	int ret = 0;
	//forward packet requires security
#ifdef FORWARD_PACKET_MODULE
	if( conf->forward_packet->is_enable ){
		bool is_enable_security = false;
		//when security module is availabe inside mmt-probe => take into account its setting parameter
		IF_ENABLE_SECURITY( is_enable_security = conf->reports.security->is_enable; )
		ASSERT( is_enable_security == true, "Forward packet module requires security module");
		ASSERT( conf->forward_packet->nb_copies > 0, "Number of copies of packet to be sent must be greater than 0: nb-copies > 0");
	}
#endif

	if( conf->outputs.mongodb ){
	}

	if( conf->reports.microflow ){
		if( conf->reports.microflow->report_bytes_count == 0)
			conf->reports.microflow->report_bytes_count = INT32_MAX;
		if( conf->reports.microflow->byte_threshold == 0 )
			conf->reports.microflow->byte_threshold = INT32_MAX;
	}

	if( conf->reports.pcap_dump ){
		if( conf->reports.pcap_dump->frequency == 0 )
			conf->reports.pcap_dump->frequency = 3600;
	}

	if( conf->reports.session->is_enable ){
		if( conf->reports.session->is_gtp
			&& (conf->reports.session->is_http
			 || conf->reports.session->is_ssl
			 || conf->reports.session->is_ftp
			 || conf->reports.session->is_rtp )){
			log_write( LOG_ERR, "session-report.is_gtp=true needs to disable other options: is_http, is_ftp, is_rtp, is_ssl are false");
			ret ++;
		}
	}

	if( conf->reports.behaviour->is_enable ){
		if( conf->reports.session->is_enable == false ){
			log_write( LOG_ERR, "behaviour.enable=true requires system-report.enable=true");
			ret ++;
		}
	}

#ifdef TCP_REASSEMBLY_MODULE
	if( ! conf->is_enable_ip_defragmentation && conf->is_enable_tcp_reassembly ){
		log_write( LOG_ERR, "enable-tcp-reassembly=true needs to enable-ip-defragmentation=true");
		ret ++;
	}
#endif

#ifdef FTP_RECONSTRUCT_MODULE
	if( ! conf->is_enable_tcp_reassembly && conf->reconstructions.ftp->is_enable){
		log_write( LOG_ERR, "FTP data reconstruction needs to enable enable-tcp-reassembly=true" );
		ret ++;
	}
#endif

#ifdef HTTP_RECONSTRUCT_MODULE
	if( ! conf->is_enable_tcp_reassembly && conf->reconstructions.http->is_enable){
		log_write( LOG_ERR, "HTTP data reconstruction needs to enable-tcp-reassembly=true" );
		ret ++;
	}
#endif


#ifdef DPDK_CAPTURE_MODULE
	if( conf->input->input_mode == OFFLINE_ANALYSIS ){
		log_write(LOG_ERR, "input.mode must be ONLINE in DPDK_CAPTURE mode");
		ret ++;
	}

	if( conf->thread->thread_count == 0 ){
		log_write(LOG_ERR, "thread-nb must be greater than 0 in DPDK_CAPTURE mode");
		ret ++;
	}
	if( ! is_power_of_two( conf->thread->thread_queue_packet_threshold ) ){
		log_write(LOG_ERR, "thread-queue must be power of two");
		ret ++;
	}
#endif
	return ret;
}
