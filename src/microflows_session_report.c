#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <errno.h>

#define __STDC_FORMAT_MACROS
#include <inttypes.h>

#include "mmt_core.h"
#include "tcpip/mmt_tcpip_protocols.h"
#include "processing.h"

/**
 * Returns 1 if the given session is a microflow, O otherwise
 * @param expired_session pointer to the session context to check
 * @return 1 if the given session is a microflow, O otherwise
 */
uint32_t is_microflow(const mmt_session_t * expired_session) {
    mmt_probe_context_t * probe_context = get_probe_context_config();

    if (probe_context->microf_enable==1){
        if ((get_session_packet_count(expired_session) <= probe_context->microf_pthreshold) || (get_session_byte_count(expired_session) <= probe_context->microf_bthreshold)) {
            return 1;
        }
    }
    return 0;
}

uint32_t is_microflow_stats_reportable(microsessions_stats_t * stats) {
    mmt_probe_context_t * probe_context = get_probe_context_config();

    if ((stats->flows_nb > probe_context->microf_report_fthreshold)
            || ((stats->dl_pcount + stats->ul_pcount) > probe_context->microf_report_pthreshold)
            || ((stats->dl_bcount + stats->ul_bcount) > probe_context->microf_report_bthreshold)) {
        return 1;
    }
    return 0;
}

void reset_microflows_stats(microsessions_stats_t * stats) {
    stats->dl_pcount = 0;
    stats->dl_bcount = 0;
    stats->ul_pcount = 0;
    stats->ul_bcount = 0;
    stats->flows_nb = 0;
}

void report_all_protocols_microflows_stats(void *args) {
    int i;
    struct smp_thread *th = (struct smp_thread *) args;

    for (i = 0; i < PROTO_MAX_IDENTIFIER; i++) {
        if (th->iprobe.mf_stats[i].flows_nb) {
            report_microflows_stats(&th->iprobe.mf_stats[i],(void *)th);
        }
    }
}

void report_microflows_stats(microsessions_stats_t * stats, void *args) {
    mmt_probe_context_t * probe_context = get_probe_context_config();
    struct smp_thread *th = (struct smp_thread *) args;

    //Format id, timestamp, App name, Nb of flows, DL Packet Count, UL Packet Count, DL Byte Count, UL Byte Count
    char message[MAX_MESS + 1];
    snprintf(message, MAX_MESS,
            "%u,%u,\"%s\",%lu.%lu,%u,%u,%u,%u,%u,%u",
            probe_context->microf_id, probe_context->probe_id_number, probe_context->input_source, stats->end_time.tv_sec, stats->end_time.tv_usec,
            stats->application_id, stats->flows_nb, stats->dl_pcount, stats->ul_pcount, stats->dl_bcount, stats->ul_bcount);

    message[ MAX_MESS ] = '\0'; // correct end of string in case of truncated message
    if (probe_context->output_to_file_enable==1)send_message_to_file_thread (message,(void *)th);
    if (probe_context->redis_enable==1)send_message_to_redis ("microflows.report", message);
    reset_microflows_stats(stats);
}

void update_microflows_stats(microsessions_stats_t * stats, const mmt_session_t * expired_session) {
    int keep_direction = 1;
    session_struct_t * temp_session = get_user_session_context(expired_session);
    if (temp_session->ipversion == 4) {
        keep_direction = is_local_net(temp_session->ipclient.ipv4);
        is_local_net(temp_session->ipserver.ipv4);
    }else if(temp_session->ipversion == 6) {
    	char ip_src_str[46];
		inet_ntop(AF_INET6, (void *) &temp_session->ipclient.ipv6, ip_src_str, INET6_ADDRSTRLEN);
		keep_direction = is_localv6_net(ip_src_str);//add more condition if any in is_localv6_net function
	}

    if(keep_direction) {
        stats->dl_pcount += get_session_dl_packet_count(expired_session);
        stats->dl_bcount += get_session_dl_byte_count(expired_session);
        stats->ul_pcount += get_session_ul_packet_count(expired_session);
        stats->ul_bcount += get_session_ul_byte_count(expired_session);
    }else {
        stats->dl_pcount += get_session_ul_packet_count(expired_session);
        stats->dl_bcount += get_session_ul_byte_count(expired_session);
        stats->ul_pcount += get_session_dl_packet_count(expired_session);
        stats->ul_bcount += get_session_dl_byte_count(expired_session);
    }
    stats->flows_nb += 1;
    stats->end_time = get_session_last_activity_time(expired_session);
}

