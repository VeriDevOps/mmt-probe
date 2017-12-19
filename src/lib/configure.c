/*
 * configure.c
 *
 *  Created on: Dec 12, 2017
 *      Author: nhnghia
 */

#include <confuse.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include "alloc.h"
#include "log.h"
#include "configure.h"

/* parse values for the input-mode option */
static int _conf_parse_input_mode(cfg_t *cfg, cfg_opt_t *opt, const char *value, void *result) {
	if (strcmp(value, "online") == 0)
		*(int *) result = ONLINE_ANALYSIS;
	else if (strcmp(value, "offline") == 0)
		*(int *) result = OFFLINE_ANALYSIS;
	else {
		cfg_error(cfg, "invalid value for option '%s': %s", cfg_opt_name(opt), value);
		return -1;
	}
	return 0;
}

static inline cfg_t *_load_cfg_from_file(const char *filename) {
	cfg_opt_t micro_flows_opts[] = {
			CFG_INT("enable", 0, CFGF_NONE),
			CFG_INT("include-packet-count", 10, CFGF_NONE),
			CFG_INT("include-byte-count", 5, CFGF_NONE),
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
			CFG_INT("port", 6379, CFGF_NONE),
			CFG_INT("enable", 0, CFGF_NONE),
			CFG_END()
	};

	cfg_opt_t kafka_output_opts[] = {
			CFG_STR("hostname", "localhost", CFGF_NONE),
			CFG_INT("port", 9092, CFGF_NONE),
			CFG_INT("enable", 0, CFGF_NONE),
			CFG_END()
	};

	cfg_opt_t output_opts[] = {
			CFG_INT("enable", 0, CFGF_NONE),
			CFG_STR("data-file", 0, CFGF_NONE),
			CFG_STR("location", 0, CFGF_NONE),
			CFG_INT("retain-files", 0, CFGF_NONE),
			CFG_INT("sampled-report", 0, CFGF_NONE),
			CFG_INT("file-output-period", 5, CFGF_NONE),
			CFG_END()
	};

	cfg_opt_t security1_opts[] = {
			CFG_INT("enable", 0, CFGF_NONE),
			CFG_STR("results-dir", 0, CFGF_NONE),
			CFG_STR("properties-file", 0, CFGF_NONE),
			CFG_STR_LIST("output-channel", "{}", CFGF_NONE),
			CFG_END()
	};

	cfg_opt_t security2_opts[] = {
			CFG_INT("enable",       0, CFGF_NONE),
			CFG_INT("thread-nb",    0, CFGF_NONE),
			CFG_STR("rules-mask",   0, CFGF_NONE),
			CFG_STR("exclude-rules",   0, CFGF_NONE),
			CFG_STR_LIST("output-channel", "{}", CFGF_NONE),
			CFG_END()
	};

	cfg_opt_t cpu_mem_report_opts[] = {
			CFG_INT("enable", 0, CFGF_NONE),
			CFG_INT("frequency", 0, CFGF_NONE),
			CFG_STR_LIST("output-channel", "{}", CFGF_NONE),
			CFG_END()
	};

	cfg_opt_t behaviour_opts[] = {
			CFG_INT("enable", 0, CFGF_NONE),
			CFG_STR("location", 0, CFGF_NONE),
			CFG_END()
	};

	cfg_opt_t reconstruct_ftp_opts[] = {
			CFG_INT("enable", 0, CFGF_NONE),
			CFG_STR("location", 0, CFGF_NONE),
			CFG_STR_LIST("output-channel", "{}", CFGF_NONE),
			CFG_END()
	};

	cfg_opt_t radius_output_opts[] = {
			CFG_INT("enable", 0, CFGF_NONE),
			CFG_INT("include-msg", 0, CFGF_NONE),
			CFG_INT("include-condition", 0, CFGF_NONE),
			CFG_STR_LIST("output-channel", "{}", CFGF_NONE),
			CFG_END()
	};

	cfg_opt_t data_output_opts[] = {
			CFG_INT("include-user-agent", MMT_USER_AGENT_THRESHOLD, CFGF_NONE),
			CFG_END()
	};

	cfg_opt_t event_report_opts[] = {
			CFG_INT("enable", 0, CFGF_NONE),
			CFG_STR("event", "", CFGF_NONE),
			CFG_STR_LIST("attributes", "{}", CFGF_NONE),
			CFG_STR_LIST("output-channel", "{}", CFGF_NONE),
			CFG_END()
	};

	cfg_opt_t condition_report_opts[] = {
			CFG_INT("enable", 0, CFGF_NONE),
			CFG_STR("condition", "", CFGF_NONE),
			CFG_STR("location", 0, CFGF_NONE),
			CFG_STR_LIST("attributes", "{}", CFGF_NONE),
			CFG_STR_LIST("handlers", "{}", CFGF_NONE),
			CFG_END()
	};
	cfg_opt_t socket_opts[] = {
			CFG_INT("enable", 0, CFGF_NONE),
			CFG_INT("domain", 0, CFGF_NONE),
			CFG_STR_LIST("port", "{}", CFGF_NONE),
			CFG_STR_LIST("server-address", "{}", CFGF_NONE),
			CFG_STR("socket-descriptor", "", CFGF_NONE),
			CFG_INT("one-socket-server", 1, CFGF_NONE),
			CFG_END()
	};
	cfg_opt_t security_report_opts[] = {
			CFG_INT("enable", 0, CFGF_NONE),
			CFG_STR_LIST("event", "{}", CFGF_NONE),
			CFG_INT("rule-type", 0, CFGF_NONE),
			CFG_STR_LIST("attributes", "{}", CFGF_NONE),
			CFG_END()
	};
	cfg_opt_t security_report_multisession_opts[] = {
			CFG_INT("enable", 0, CFGF_NONE),
			CFG_INT("file-output", 0, CFGF_NONE),
			CFG_INT("redis-output", 0, CFGF_NONE),
			CFG_STR_LIST("attributes", "{}", CFGF_NONE),
			CFG_STR_LIST("output-channel", "{}", CFGF_NONE),
			CFG_END()
	};
	cfg_opt_t session_report_opts[] = {
			CFG_INT("enable", 0, CFGF_NONE),
			CFG_STR_LIST("output-channel", "{}", CFGF_NONE),
			CFG_END()
	};

	cfg_opt_t opts[] = {
			CFG_SEC("micro-flows", micro_flows_opts, CFGF_NONE),
			CFG_SEC("session-timeout", session_timeout_opts, CFGF_NONE),
			CFG_SEC("file-output", output_opts, CFGF_NONE),
			CFG_SEC("redis-output", redis_output_opts, CFGF_NONE),
			CFG_SEC("kafka-output", redis_output_opts, CFGF_NONE),
			CFG_SEC("data-output", data_output_opts, CFGF_NONE),
			CFG_SEC("security1", security1_opts, CFGF_NONE),
			CFG_SEC("security2", security2_opts, CFGF_NONE),
			CFG_SEC("cpu-mem-usage", cpu_mem_report_opts, CFGF_NONE),
			CFG_SEC("socket", socket_opts, CFGF_NONE),
			CFG_SEC("behaviour", behaviour_opts, CFGF_NONE),
			CFG_SEC("reconstruct-ftp", reconstruct_ftp_opts, CFGF_NONE),
			CFG_SEC("radius-output", radius_output_opts, CFGF_NONE),
			CFG_INT("stats-period", 5, CFGF_NONE),
			CFG_INT("enable-proto-without-session-stat", 0, CFGF_NONE),
			CFG_INT("enable-IP-fragmentation-report", 0, CFGF_NONE),
			CFG_INT("enable-session-report", 0, CFGF_NONE),
			CFG_INT("thread-nb", 1, CFGF_NONE),
			CFG_INT("thread-queue", 0, CFGF_NONE),
			CFG_INT("thread-data", 0, CFGF_NONE),
			CFG_INT("snap-len", 65535, CFGF_NONE),
			CFG_INT("cache-size-for-reporting", 300000, CFGF_NONE),
			CFG_INT_CB("input-mode", 0, CFGF_NONE, _conf_parse_input_mode),
			CFG_STR("input-source", "nosource", CFGF_NONE),
			CFG_INT("probe-id", 0, CFGF_NONE),
			CFG_STR("logfile", 0, CFGF_NONE),
			CFG_STR("license_file_path", 0, CFGF_NONE),
			CFG_INT("loglevel", 2, CFGF_NONE),
			CFG_SEC("event_report", event_report_opts, CFGF_TITLE | CFGF_MULTI),
			CFG_SEC("condition_report", condition_report_opts, CFGF_TITLE | CFGF_MULTI),
			CFG_SEC("security-report", security_report_opts, CFGF_TITLE | CFGF_MULTI),
			CFG_INT("num-of-report-per-msg", 1, CFGF_NONE),
			CFG_SEC("security-report-multisession", security_report_multisession_opts, CFGF_TITLE | CFGF_MULTI),
			CFG_SEC("session-report", session_report_opts, CFGF_TITLE | CFGF_MULTI),

			CFG_END()
	};

	cfg_t *cfg = cfg_init(opts, CFGF_NONE);
	switch (cfg_parse(cfg, filename)) {
	case CFG_FILE_ERROR:
		log_write(LOG_ERR, "Error: configuration file '%s' could not be read: %s\n", filename, strerror(errno));
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
	return strdup( str );
}

static inline long int _cfg_getint( cfg_t *cfg, const char *ident, long int min, long int max, long int def_val, long int replaced_val ){
	long int val = cfg_getint( cfg, ident );
	if( val < min || val > max || val == def_val ){
		log_write( LOG_WARNING, "Not expected %ld for %s. Used default value %ld.", val, ident, replaced_val );
		return replaced_val;
	}
	return val;
}

static inline input_source_conf_t * _parse_input_source( cfg_t *cfg ){
	input_source_conf_t *ret = alloc( sizeof( input_source_conf_t ));

	ret->input_mode   = cfg_getint(cfg, "input-mode");
	ret->input_source = _cfg_get_str(cfg, "input-source");

#ifndef DPDK_MODULE
#ifndef PCAP_MODULE
#error("Neither DPDK nor PCAP is defined")
#endif
#endif

#ifdef DPDK_MODULE
	ret->capture_mode = DPDK_CAPTURE;
#endif

#ifdef PCAP_MODULE
	ret->capture_mode = PCAP_CAPTURE;
#endif

	ret->snap_len = cfg_getint( cfg, "snap-len" );

	if( ret->snap_len == 0 )
		ret->snap_len = UINT16_MAX;

	return ret;
}

static inline cfg_t* _get_first_cfg_block( cfg_t *cfg, const char* block_name ){
	if( ! cfg_size( cfg, block_name) )
		return NULL;
	return cfg_getnsec( cfg, block_name, 0 );
}

static inline file_output_conf_t *_parse_output_to_file( cfg_t *cfg ){
	cfg_t * c = _get_first_cfg_block( cfg, "file-output" );
	if( c == NULL )
		return NULL;

	file_output_conf_t *ret = alloc( sizeof( file_output_conf_t ));

	ret->is_enable  = cfg_getint( c, "enable" );
	ret->directory  = _cfg_get_str(c, "location");
	ret->filename   = _cfg_get_str(c, "data-file");
	ret->is_sampled = cfg_getint( c, "sampled-report" );
	ret->file_output_period   = cfg_getint( c, "file-output-period");
	ret->retained_files_count = cfg_getint( c, "retain-files" );

	return ret;
}


static inline kafka_output_conf_t *_parse_output_to_kafka( cfg_t *cfg ){
	if( (cfg = _get_first_cfg_block( cfg, "kafka-output")) == NULL )
		return NULL;

	kafka_output_conf_t *ret = alloc( sizeof( kafka_output_conf_t ));

	ret->is_enable        = cfg_getint( cfg,  "enable" );
	ret->host.host_name   = _cfg_get_str(cfg, "hostname");
	ret->host.port_number = cfg_getint( cfg,  "port" );

	return ret;
}


static inline redis_output_conf_t *_parse_output_to_redis( cfg_t *cfg ){
	if( (cfg = _get_first_cfg_block( cfg, "redis-output")) == NULL )
		return NULL;

	redis_output_conf_t *ret = alloc( sizeof( redis_output_conf_t ));

	ret->is_enable        = cfg_getint( cfg,  "enable" );
	ret->host.host_name   = _cfg_get_str(cfg, "hostname");
	ret->host.port_number = cfg_getint( cfg,  "port" );

	return ret;
}

static inline log_conf_t * _parse_log( cfg_t *cfg ){
	log_conf_t *ret = alloc( sizeof( log_conf_t ));
	ret->level = cfg_getint( cfg, "loglevel" );
	ret->file  = _cfg_get_str(cfg, "logfile" );
	return ret;
}

static inline multi_thread_conf_t * _parse_thread( cfg_t *cfg ){
	multi_thread_conf_t *ret = alloc( sizeof( multi_thread_conf_t ));
	ret->thread_count                  = cfg_getint( cfg, "thread-nb" );
	ret->thread_queue_data_threshold   = cfg_getint( cfg, "thread-data" );
	ret->thread_queue_packet_threshold = cfg_getint( cfg, "thread-queue" );
	return ret;
}

static inline behaviour_conf_t *_parse_behaviour_block( cfg_t *cfg ){
	if( (cfg = _get_first_cfg_block( cfg, "behaviour")) == NULL )
		return NULL;

	behaviour_conf_t *ret = alloc( sizeof( behaviour_conf_t ));
	ret->is_enable = cfg_getint( cfg, "enable" );
	ret->directory = _cfg_get_str(cfg, "location");
	return ret;
}

static inline void _parse_output_channel( output_channel_conf_t *out, cfg_t *cfg ){
	int nb_output_channel = cfg_size( cfg, "output-channel");
	int i;
	const char *channel_name;

	out->is_output_to_file  = false;
	out->is_output_to_kafka = false;
	out->is_output_to_redis = false;

	for( i=0; i<nb_output_channel; i++) {
		channel_name = cfg_getnstr(cfg, "output-channel", i);
		if ( strncmp( channel_name, "file", 4 ) == 0 )
			out->is_output_to_file = true;
		else if ( strncmp( channel_name, "kafka", 5 ) == 0 )
			out->is_output_to_kafka = true;
		else if ( strncmp( channel_name, "redis", 5 ) == 0 )
			out->is_output_to_redis = true;
	}

	out->is_enable = (out->is_output_to_file || out->is_output_to_kafka || out->is_output_to_redis );
}

static inline cpu_mem_usage_conf *_parse_cpu_mem_block( cfg_t *cfg ){
	if( (cfg = _get_first_cfg_block( cfg, "cpu-mem-usage")) == NULL )
		return NULL;

	cpu_mem_usage_conf *ret = alloc( sizeof( cpu_mem_usage_conf ));
	ret->is_enable = cfg_getint( cfg, "enable" );
	ret->frequency = cfg_getint( cfg, "frequency" );
	_parse_output_channel( & ret->output_channels, cfg );
	return ret;
}

static inline void _parse_dpi_protocol_attribute( dpi_protocol_attribute_t * out, const char* str ){
	out->attribute_name = NULL;
	out->proto_name     = NULL;

	//str = HTTP.METHOD
	int index = 0;
	while( str[index] != '.' )
		index ++;
	out->proto_name     = strndup( str, index );
	out->attribute_name = strdup( str+index+1 ); //+1 to jump over .

}

static inline uint16_t _parse_attributes_helper( cfg_t *cfg, const char* name, dpi_protocol_attribute_t**atts ){
	int i;
	uint16_t size =  cfg_size( cfg, name );
	*atts = NULL;
	if( size == 0 )
		return size;

	dpi_protocol_attribute_t *ret = NULL;
	ret = alloc( sizeof( dpi_protocol_attribute_t ) * size );
	for( i=0; i<size; i++ )
		_parse_dpi_protocol_attribute( &ret[i], cfg_getnstr( cfg, name, i ) );

	*atts = ret;
	return size;
}




static inline void _parse_event_block( event_report_conf_t *ret, cfg_t *cfg ){
	int i;
	assert( cfg != NULL );
	ret->is_enable = cfg_getint( cfg, "enable" );
	ret->event = alloc( sizeof( dpi_protocol_attribute_t ));
	_parse_dpi_protocol_attribute( ret->event, cfg_getstr( cfg, "event" ) );

	ret->attributes_size = _parse_attributes_helper( cfg, "attributes", &ret->attributes );

	_parse_output_channel( & ret->output_channels, cfg );
}

static inline micro_flow_conf_t *_parse_microflow_block( cfg_t *cfg ){
	if( (cfg = _get_first_cfg_block( cfg, "micro-flows")) == NULL )
		return NULL;

	micro_flow_conf_t *ret = alloc( sizeof( micro_flow_conf_t ));
	ret->is_enable             = cfg_getint( cfg, "enable" );
	ret->include_bytes_count   = cfg_getint( cfg, "include-byte-count" );
	ret->include_packets_count = cfg_getint( cfg, "include-packet-count" );
	ret->report_bytes_count    = cfg_getint( cfg, "report-byte-count" );
	ret->report_flows_count    = cfg_getint( cfg, "report-flow-count" );
	_parse_output_channel( & ret->output_channels, cfg );
	return ret;
}

static inline radius_conf_t *_parse_radius_block( cfg_t *cfg ){
	if( (cfg = _get_first_cfg_block( cfg, "radius-output")) == NULL )
		return NULL;

	radius_conf_t *ret     = alloc( sizeof( radius_conf_t ));
	ret->is_enable         = cfg_getint( cfg, "enable" );
	ret->include_msg       = cfg_getint( cfg, "include-msg" );
	ret->include_condition = cfg_getint( cfg, "include-condition" );
	_parse_output_channel( & ret->output_channels, cfg );
	return ret;
}


static inline reconstruct_ftp_setting_t *_parse_reconstruct_ftp_block( cfg_t *cfg ){
	if( (cfg = _get_first_cfg_block( cfg, "reconstruct-ftp")) == NULL )
		return NULL;

	reconstruct_ftp_setting_t *ret = alloc( sizeof( reconstruct_ftp_setting_t ));
	ret->is_enable = cfg_getint( cfg, "enable" );
	ret->directory = _cfg_get_str(cfg, "location" );
	_parse_output_channel( & ret->output_channels, cfg );
	return ret;
}

static inline security_conf_t *_parse_security_block( cfg_t *cfg ){
	if( (cfg = _get_first_cfg_block( cfg, "security2")) == NULL )
		return NULL;

	security_conf_t *ret = alloc( sizeof( security_conf_t ));
	ret->is_enable = cfg_getint( cfg, "enable" );
	ret->threads_size = cfg_getint( cfg, "thread-nb" );
	ret->excluded_rules = _cfg_get_str(cfg, "exclude-rules" );
	ret->rules_mask = _cfg_get_str(cfg, "rules-mask" );
	_parse_output_channel( & ret->output_channels, cfg );
	return ret;
}

//old security version 1
static inline security1_conf_t *_parse_security1_block( cfg_t *cfg ){
	if( (cfg = _get_first_cfg_block( cfg, "security1")) == NULL )
		return NULL;

	security1_conf_t *ret = alloc( sizeof( security1_conf_t ));
	ret->is_enable = cfg_getint( cfg, "enable" );
	ret->property_file = _cfg_get_str(cfg, "properties-file" );
	ret->result_directory = _cfg_get_str(cfg, "results-dir" );
	_parse_output_channel( & ret->output_channels, cfg );
	return ret;
}

static inline security_multi_sessions_conf_t *_parse_multi_session_block( cfg_t *cfg ){
	if( (cfg = _get_first_cfg_block( cfg, "security-report-multisession")) == NULL )
		return NULL;

	security_multi_sessions_conf_t *ret = alloc( sizeof( security_multi_sessions_conf_t ));
	ret->is_enable = cfg_getint( cfg, "enable" );
	ret->attributes_size = _parse_attributes_helper(cfg, "attributes", &ret->attributes );
	_parse_output_channel( & ret->output_channels, cfg );
	return ret;
}

static inline session_report_conf_t *_parse_session_block( cfg_t *cfg ){
	if( (cfg = _get_first_cfg_block( cfg, "session-report")) == NULL )
		return NULL;

	session_report_conf_t *ret = alloc( sizeof( session_report_conf_t ));
	ret->is_enable = cfg_getint( cfg, "enable" );
	_parse_output_channel( & ret->output_channels, cfg );
	return ret;
}

static inline session_timeout_conf_t *_parse_session_timeout_block( cfg_t *cfg ){
	if( (cfg = _get_first_cfg_block( cfg, "session-timeout")) == NULL )
		return NULL;

	session_timeout_conf_t *ret = alloc( sizeof( session_timeout_conf_t ));
	ret->default_session_timeout = _cfg_getint( cfg, "default-session-timeout", 0, 6000, 0,   60 );
	ret->live_session_timeout    = _cfg_getint( cfg, "live-session-timeout",    0, 6000, 0, 1500 );
	ret->long_session_timeout    = _cfg_getint( cfg, "long-session-timeout",    0, 6000, 0,  600 );
	ret->short_session_timeout   = _cfg_getint( cfg, "short-session-timeout",   0, 6000, 0,   15 );
	return ret;
}

static inline socket_output_conf_t *_parse_socket_block( cfg_t *cfg ){
	int i;
	cfg_t *c = _get_first_cfg_block( cfg, "socket");
	if( c == NULL )
		return NULL;

	socket_output_conf_t *ret = alloc( sizeof( socket_output_conf_t ));
	ret->is_enable = cfg_getint( c, "enable" );
	switch( cfg_getint( c, "domain")  ){
	case 0:
		ret->socket_type = UNIX_SOCKET_TYPE;
		break;
	case 1:
		ret->socket_type = INTERNET_SOCKET_TYPE;
		break;
	case 2:
		ret->socket_type = ANY_SOCKET_TYPE;
		break;
	}
	ret->unix_socket_descriptor = _cfg_get_str(c, "socket-descriptor" );
	ret->is_one_socket_server =  (cfg_getint( c, "one-socket-server") == 1);
	//this is inside main config
	ret->messages_per_report  = cfg_getint( cfg, "num-of-report-per-msg");

	ret->internet_sockets_size = cfg_size( c, "port" );
	if( ret->internet_sockets_size > cfg_size( c, "server-address") ){
		printf( "Error: Number of socket.port and socket.server-address are different" );
		exit( 1 );
	}

	ret->internet_sockets = NULL;// alloc( sizeof (internet_socket_output_conf_struct ))

	return ret;
}


static inline condition_report_conf_t *_parse_condition_block( cfg_t *cfg, const char*name ){
	int size = cfg_size( cfg, "condition_report");
	int i;
	cfg_t *c;

	if( size == 0 )
		return NULL;

	for( i=0; i<size; i++ ){
		c = cfg_getnsec(cfg, "condition_report", i );
		if( c == NULL )
			return NULL;
		if( strcmp(name, cfg_getstr( c, "condition")) != 0 )
			continue;

		condition_report_conf_t *ret = alloc( sizeof( condition_report_conf_t ));
		ret->is_enable = cfg_getint( c, "enable" );
		ret->attributes_size = _parse_attributes_helper(c, "attributes", &ret->attributes );

		if( ret->attributes_size != cfg_size( c, "handlers" ) ){
			printf("Error: Number of attributes and handlers for condition %s must be the same",
					cfg_getstr( c, "condition") );
			exit( 1 );
		}

		ret->handler_names = alloc( sizeof( char *) * ret->attributes_size);
		for( i=0; i<ret->attributes_size; i++ )
			ret->handler_names[i] = strdup( cfg_getnstr(c, "handlers", i)  );

		return ret;
	}
	return NULL;
}
/**
 * Public API
 * @param filename
 * @return
 */
probe_conf_t* load_configuration_from_file( const char* filename ){
	const char *str;
	int i;
	cfg_t *cfg = _load_cfg_from_file( filename );
	if( cfg == NULL ){
		exit( EXIT_FAILURE );
	}

	probe_conf_t *conf = alloc( sizeof( probe_conf_t ) );

	conf->probe_id     = cfg_getint(cfg, "probe-id");
	conf->stat_period  = cfg_getint(cfg, "stats-period");
	conf->license_file = _cfg_get_str(cfg, "license_file_path" );
	conf->is_enable_proto_no_session_stat = cfg_getint(cfg, "enable-proto-without-session-stat");
	conf->is_enable_ip_fragementation     = cfg_getint(cfg, "enable-IP-fragmentation-report");

	conf->input = _parse_input_source( cfg );
	//set of output channels
	conf->outputs.file  = _parse_output_to_file( cfg );
	conf->outputs.kafka = _parse_output_to_kafka( cfg );
	conf->outputs.redis = _parse_output_to_redis( cfg );
	//a global
	conf->outputs.is_enable = ( (conf->outputs.file != NULL && conf->outputs.file->is_enable )
									|| (conf->outputs.redis != NULL && conf->outputs.redis->is_enable)
									|| (conf->outputs.kafka != NULL && conf->outputs.kafka->is_enable ));

	//
	conf->log = _parse_log( cfg );

	//
	conf->thread = _parse_thread( cfg );

	conf->reports.behaviour = _parse_behaviour_block( cfg );
	conf->reports.cpu_mem   = _parse_cpu_mem_block( cfg );

	//events reports
	conf->reports.events_size = cfg_size( cfg, "event_report" );
	conf->reports.events  = alloc( sizeof( event_report_conf_t ) * conf->reports.events_size );
	for( i=0; i<conf->reports.events_size; i++ )
		_parse_event_block( &conf->reports.events[i], cfg_getnsec( cfg, "event_report", i) );

	//
	conf->reports.microflow = _parse_microflow_block( cfg );

	conf->reports.radius = _parse_radius_block( cfg );
	conf->reports.reconstruct_ftp = _parse_reconstruct_ftp_block( cfg );
	conf->reports.security = _parse_security_block( cfg );
	conf->reports.security1 = _parse_security1_block( cfg );
	conf->reports.security_multisession = _parse_multi_session_block( cfg );
	conf->reports.session = _parse_session_block( cfg );
	conf->reports.socket = _parse_socket_block( cfg );

	conf->session_timeout = _parse_session_timeout_block( cfg );

	//
	conf->conditions.ftp              = _parse_condition_block( cfg, "FTP" );
	conf->conditions.reconstruit_http = _parse_condition_block( cfg, "HTTP-RECONSTRUCT" );
	conf->conditions.rtp              = _parse_condition_block( cfg, "RTP" );
	conf->conditions.ssl              = _parse_condition_block( cfg, "SSL" );
	conf->conditions.web              = _parse_condition_block( cfg, "WEB" );

	cfg_free( cfg );
	return conf;
}

static inline void _free_condition_report( condition_report_conf_t *ret ){
	if( ret == NULL )
		return;
	int i;
	for( i=0; i<ret->attributes_size; i++ ){
		xfree( ret->handler_names[i] );
		xfree( ret->attributes[i].proto_name );
		xfree( ret->attributes[i].attribute_name );
	}
	xfree( ret->handler_names );
	xfree( ret->attributes );
	xfree( ret );
}

static inline void _free_event_report( event_report_conf_t *ret ){
	if( ret == NULL )
		return;
	int i;
	for( i=0; i<ret->attributes_size; i++ ){
		xfree( ret->attributes[i].proto_name );
		xfree( ret->attributes[i].attribute_name );
	}
	xfree( ret->attributes );

	xfree( ret->event->proto_name );
	xfree( ret->event->attribute_name );
	xfree( ret->event );
}

/**
 * Public API
 * Free all memory allocated by @load_configuration_from_file
 * @param
 */
void release_probe_configuration( probe_conf_t *conf){
	if( conf == NULL )
		return;

	int i;

	xfree( conf->input->input_source );
	xfree( conf->input );

	for( i=0; i<conf->reports.events_size; i++ )
		_free_event_report( &conf->reports.events[i] );
	xfree( conf->reports.events );

	if( conf->reports.behaviour ){
		xfree( conf->reports.behaviour->directory );
		xfree( conf->reports.behaviour );
	}

	xfree( conf->reports.cpu_mem );
	xfree( conf->reports.microflow );
	xfree( conf->reports.radius );
	if( conf->reports.reconstruct_ftp ){
		xfree( conf->reports.reconstruct_ftp->directory );
		xfree( conf->reports.reconstruct_ftp );
	}

	if( conf->reports.security ){
		xfree( conf->reports.security->excluded_rules );
		xfree( conf->reports.security->rules_mask );
		xfree( conf->reports.security );
	}

	if( conf->reports.security1 ){
		xfree( conf->reports.security1->result_directory );
		xfree( conf->reports.security1->property_file );
		xfree( conf->reports.security1 );
	}
	if( conf->reports.security_multisession ){
		for( i=0; i<conf->reports.security_multisession->attributes_size; i++ ){
			xfree( conf->reports.security_multisession->attributes[i].proto_name );
			xfree( conf->reports.security_multisession->attributes[i].attribute_name );
		}
		xfree( conf->reports.security_multisession->attributes );
		xfree( conf->reports.security_multisession );
	}
	xfree( conf->reports.session );

	if( conf->reports.socket ){
		xfree( conf->reports.socket->unix_socket_descriptor );
		for( i=0; i<conf->reports.socket->internet_sockets_size; i++){
			//xfree(conf->reports.socket->internet_sockets[i].host.host_name );
		}
		xfree( conf->reports.socket->internet_sockets );
		xfree( conf->reports.socket );
	}

	_free_condition_report( conf->conditions.ftp );
	_free_condition_report( conf->conditions.reconstruit_http );
	_free_condition_report( conf->conditions.rtp );
	_free_condition_report( conf->conditions.ssl );
	_free_condition_report( conf->conditions.web );

	xfree( conf->thread );
	if( conf->outputs.file ){
		xfree( conf->outputs.file->directory );
		xfree( conf->outputs.file->filename );
		xfree( conf->outputs.file );
	}
	if( conf->outputs.kafka ){
		xfree( conf->outputs.kafka->host.host_name );
		xfree( conf->outputs.kafka );
	}
	if( conf->outputs.redis ){
		xfree( conf->outputs.redis->host.host_name );
		xfree( conf->outputs.redis );
	}

	if( conf->log ){
		xfree( conf->log->file );
		xfree( conf->log );
	}

	xfree( conf->license_file );
	xfree( conf );
}
