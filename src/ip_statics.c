/*
 * ip_static.c
 *
 *  Created on: Feb 5, 2016
 *      Author: montimage
 */
#include <stdio.h>
#include <string.h>
#include <time.h>

#define __STDC_FORMAT_MACROS
#include <inttypes.h>

#include "mmt_core.h"
#include "mmt/tcpip/mmt_tcpip.h"
#include "processing.h"

void print_ip_session_report (const mmt_session_t * session){
	char message[MAX_MESS + 1];
	uint8_t *ea = 0;
	char src_mac_pretty [18], dst_mac_pretty [18];
	int keep_direction = 1;
	int valid = 0;
	mmt_probe_context_t * probe_context = get_probe_context_config();

	//printf("print_ip_session_report_total_session_count =%lu \n",total_session_count);

	session_struct_t * temp_session = (session_struct_t *) get_user_session_context(session);
	if (temp_session == NULL){
		return;
	}

	if (temp_session->session_attr == NULL) {
		temp_session->session_attr = (temp_session_statistics_t *) malloc(sizeof (temp_session_statistics_t));
		memset(temp_session->session_attr, 0, sizeof (temp_session_statistics_t));
	}
	// To  check whether the session activity occurs between the reporting time interval
	if ((uint32_t) TIMEVAL_2_MSEC(mmt_time_diff(temp_session->session_attr->last_activity_time,get_session_last_activity_time(session))) == 0) return; // check the condition if in the last interval there was a protocol activity or not

	//if (get_session_byte_count(session) - temp_session->session_attr->total_byte_count == 0)return;
	ea = temp_session->src_mac;
	snprintf(src_mac_pretty , 18, "%02x:%02x:%02x:%02x:%02x:%02x", ea[0], ea[1], ea[2], ea[3], ea[4], ea[5] );
	ea = temp_session->dst_mac;
	snprintf(dst_mac_pretty , 18, "%02x:%02x:%02x:%02x:%02x:%02x", ea[0], ea[1], ea[2], ea[3], ea[4], ea[5] );

	char ip_src_str[46];
	char ip_dst_str[46];
	if (temp_session->ipversion == 4) {
		inet_ntop(AF_INET, (void *) &temp_session->ipclient.ipv4, ip_src_str, INET_ADDRSTRLEN);
		inet_ntop(AF_INET, (void *) &temp_session->ipserver.ipv4, ip_dst_str, INET_ADDRSTRLEN);
		keep_direction = is_local_net(temp_session->ipclient.ipv4);
	} else if(temp_session->ipversion == 6) {
		inet_ntop(AF_INET6, (void *) &temp_session->ipclient.ipv6, ip_src_str, INET6_ADDRSTRLEN);
		inet_ntop(AF_INET6, (void *) &temp_session->ipserver.ipv6, ip_dst_str, INET6_ADDRSTRLEN);
		keep_direction = is_localv6_net(ip_src_str);//to do
	}
	const proto_hierarchy_t * proto_hierarchy = get_session_protocol_hierarchy(session);
	int proto_id = proto_hierarchy->proto_path[ proto_hierarchy->len - 1 ];
	temp_session->session_attr->start_time = get_session_init_time(session);
	proto_hierarchy_ids_to_str(get_session_protocol_hierarchy(session), temp_session->path);
	temp_session->session_attr->last_activity_time = get_session_last_activity_time(session);
	int sslindex;

	snprintf(message, MAX_MESS,"%u,%u,\"%s\",%lu.%lu,%u,\"%s\",%"PRIu64" ,%"PRIu64",%"PRIu64",%"PRIu64",%"PRIu64",%"PRIu64",%"PRIu64",%"PRIu64",%"PRIu64",%"PRIu64",%lu.%lu,\"%s\",\"%s\",\"%s\",\"%s\",%"PRIu64",%hu,%hu",
			MMT_STATISTICS_FLOW_REPORT_FORMAT, probe_context->probe_id_number, probe_context->input_source,temp_session->session_attr->last_activity_time.tv_sec, temp_session->session_attr->last_activity_time.tv_usec,
			proto_id,
			temp_session->path, total_session_count,
			get_session_byte_count(session) - temp_session->session_attr->total_byte_count,
			get_session_data_byte_count(session) - temp_session->session_attr->total_data_byte_count,
			get_session_packet_count(session) - temp_session->session_attr->total_packet_count,
			((keep_direction)?get_session_ul_byte_count(session):get_session_dl_byte_count(session)) - temp_session->session_attr->byte_count[0],
			((keep_direction)?get_session_ul_data_byte_count(session):get_session_dl_data_byte_count(session)) - temp_session->session_attr->data_byte_count[0],
			((keep_direction)?get_session_ul_packet_count(session):get_session_dl_packet_count(session)) - temp_session->session_attr->packet_count[0],
			((keep_direction)?get_session_dl_byte_count(session):get_session_ul_byte_count(session)) - temp_session->session_attr->byte_count[1],
			((keep_direction)?get_session_dl_data_byte_count(session):get_session_ul_data_byte_count(session)) - temp_session->session_attr->data_byte_count[1],
			((keep_direction)?get_session_dl_packet_count(session):get_session_ul_packet_count(session)) - temp_session->session_attr->packet_count[1],
			temp_session->session_attr->start_time.tv_sec, temp_session->session_attr->start_time.tv_usec,
			ip_src_str, ip_dst_str, src_mac_pretty, dst_mac_pretty,get_session_id(session),
			temp_session->serverport, temp_session->clientport);
	valid = strlen(message);

	//To inform what is comming at the start of the flow report

	if (temp_session->app_format_id == probe_context->web_id && temp_session->session_attr->touched == 0 && probe_context->web_enable == 1) print_initial_web_report(session,temp_session,message,valid);
	else if (temp_session->app_format_id == probe_context->rtp_id && temp_session->session_attr->touched == 0 && probe_context->rtp_enable == 1) print_initial_rtp_report(session,temp_session,message,valid);
	else if (temp_session->app_format_id == probe_context->ssl_id && temp_session->session_attr->touched == 0 && probe_context->ssl_enable == 1) print_initial_ssl_report(session,temp_session,message,valid);
	else if (temp_session->app_format_id == probe_context->ftp_id && temp_session->session_attr->touched == 0 && probe_context->ftp_enable == 1) print_initial_ftp_report(session,temp_session,message,valid);
	else if(temp_session->session_attr->touched == 0){
		sslindex = get_protocol_index_from_session(proto_hierarchy, PROTO_SSL);
		if (sslindex != -1) print_initial_ssl_report(session,temp_session,message,valid);
		else print_initial_default_report(session,temp_session,message,valid);
	}
	valid = strlen(message);
	message[ valid ] = '\0'; // correct end of string in case of truncated message

	if (probe_context->output_to_file_enable==1)send_message_to_file (message);
	if (probe_context->redis_enable==1)send_message_to_redis ("session.flow.report", message);

	temp_session->session_attr->total_byte_count = get_session_byte_count(session);
	temp_session->session_attr->total_data_byte_count = get_session_data_byte_count(session);
	temp_session->session_attr->total_packet_count = get_session_packet_count(session);

	temp_session->session_attr->byte_count[0] = (keep_direction)?get_session_ul_byte_count(session):get_session_dl_byte_count(session);
	temp_session->session_attr->byte_count[1] = (keep_direction)?get_session_dl_byte_count(session):get_session_ul_byte_count(session);

	temp_session->session_attr->data_byte_count[0] = (keep_direction)?get_session_ul_data_byte_count(session):get_session_dl_data_byte_count(session);
	temp_session->session_attr->data_byte_count[1] = (keep_direction)?get_session_dl_data_byte_count(session):get_session_ul_data_byte_count(session);

	temp_session->session_attr->packet_count[0] = (keep_direction)?get_session_ul_packet_count(session):get_session_dl_packet_count(session);
	temp_session->session_attr->packet_count[1] = (keep_direction)?get_session_dl_packet_count(session):get_session_ul_packet_count(session);

}

void go_through_session(const mmt_session_t * session){

	const mmt_session_t * session_next = get_session_next(session);
	const mmt_session_t * session_previous = get_session_previous(session);

	print_ip_session_report (session);

	while(session_previous != NULL){

		print_ip_session_report (session_previous);
		session_previous = get_session_previous(session_previous);
	}

	while(session_next != NULL){
		print_ip_session_report (session_next);
		session_next = get_session_next(session_next);
	}
}
