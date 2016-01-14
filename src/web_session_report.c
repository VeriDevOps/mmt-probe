#include <stdio.h>
#include <string.h>
#include <time.h>

#define __STDC_FORMAT_MACROS
#include <inttypes.h>

#include "mmt_core.h"
//#include "mmt/tcpip/mmt_tcpip_protocols.h"
//#include "mmt/tcpip/mmt_tcpip_attributes.h"
#include "mmt/tcpip/mmt_tcpip.h"
#include "processing.h"


#define TIMEVAL_2_MSEC(tval) ((tval.tv_sec << 10) + (tval.tv_usec >> 10))

static struct timeval mmt_time_diff(struct timeval tstart, struct timeval tend) {
    tstart.tv_sec = tend.tv_sec - tstart.tv_sec;
    tstart.tv_usec = tend.tv_usec - tstart.tv_usec;
    if ((int) tstart.tv_usec < 0) {
        tstart.tv_usec += 1000000;
        tstart.tv_sec -= 1;
    }
    return tstart;
}

typedef struct http_line_struct {
    const uint8_t *ptr;
    uint16_t len;
} http_line_struct_t;


void mime_handle(const ipacket_t * ipacket, attribute_t * attribute, void * user_args) {
    if(ipacket->session == NULL) return;
    mmt_probe_context_t * probe_context = get_probe_context_config();

    http_line_struct_t * mime = (http_line_struct_t *) attribute->data;
    session_struct_t *temp_session = (session_struct_t *) get_user_session_context_from_packet(ipacket);
    if (temp_session == NULL || temp_session->app_data == NULL) {
        return;
    }
    if (mime != NULL && temp_session->app_format_id == probe_context->web_id) {
        int max = (mime->len > 63) ? 63 : mime->len;

        strncpy(((web_session_attr_t *) temp_session->app_data)->mimetype, (char *) mime->ptr, max);
        ((web_session_attr_t *) temp_session->app_data)->mimetype[max] = '\0';
        char * semi_column = strchr(((web_session_attr_t *) temp_session->app_data)->mimetype, ';');
        if (semi_column) {
            //Semi column found, replace it by an en of string '\0'
            *semi_column = '\0';
        }
        temp_session->contentclass = get_content_class_by_content_type(((web_session_attr_t *) temp_session->app_data)->mimetype);
    }
}

void host_handle(const ipacket_t * ipacket, attribute_t * attribute, void * user_args) {
    if(ipacket->session == NULL) return;
    mmt_probe_context_t * probe_context = get_probe_context_config();

    http_line_struct_t * host = (http_line_struct_t *) attribute->data;
    session_struct_t *temp_session = (session_struct_t *) get_user_session_context_from_packet(ipacket);
    if (temp_session == NULL || temp_session->app_data == NULL) {
        return;
    }
    if (host != NULL && temp_session->app_format_id == probe_context->web_id) {
        int max = (host->len > 95) ? 95 : host->len;

        strncpy(((web_session_attr_t *) temp_session->app_data)->hostname, (char *) host->ptr, max);
        ((web_session_attr_t *) temp_session->app_data)->hostname[max] = '\0';
        char * coma = strchr(((web_session_attr_t *) temp_session->app_data)->hostname, ',');
        if (coma) {
            //Semi column found, replace it by an en of string '\0'
            *coma = '\0';
        }
        ((web_session_attr_t *) temp_session->app_data)->trans_nb += 1;
        if (((web_session_attr_t *) temp_session->app_data)->trans_nb == 1) {
            ((web_session_attr_t *) temp_session->app_data)->response_time = ipacket->p_hdr->ts;
            ((web_session_attr_t *) temp_session->app_data)->first_request_time = ipacket->p_hdr->ts;
        }
    }
}

void http_method_handle(const ipacket_t * ipacket, attribute_t * attribute, void * user_args) {
    if(ipacket->session == NULL) return;
    session_struct_t *temp_session = (session_struct_t *) get_user_session_context_from_packet(ipacket);
    mmt_probe_context_t * probe_context = get_probe_context_config();
    if (temp_session != NULL) {
        if (temp_session->app_data == NULL) {
            web_session_attr_t * http_data = (web_session_attr_t *) malloc(sizeof (web_session_attr_t));
            if (http_data != NULL) {
                memset(http_data, '\0', sizeof (web_session_attr_t));
                temp_session->app_format_id = probe_context->web_id;
                temp_session->app_data = (void *) http_data;
            } else {
                mmt_log(probe_context, MMT_L_WARNING, MMT_P_MEM_ERROR, "Memory error while creating HTTP reporting context");
                //fprintf(stderr, "Out of memory error when creating HTTP specific data structure!\n");
                return;
            }
        }

        //((web_session_attr_t *) temp_session->app_data)->trans_nb += 1;
        //if (((web_session_attr_t *) temp_session->app_data)->trans_nb == 1) {
        //    ((web_session_attr_t *) temp_session->app_data)->response_time = ipacket->p_hdr->ts;
        //    ((web_session_attr_t *) temp_session->app_data)->first_request_time = ipacket->p_hdr->ts;
        //}
    }
}

void referer_handle(const ipacket_t * ipacket, attribute_t * attribute, void * user_args) {
    if(ipacket->session == NULL) return;
    mmt_probe_context_t * probe_context = get_probe_context_config();

    http_line_struct_t * referer = (http_line_struct_t *) attribute->data;
    session_struct_t *temp_session = (session_struct_t *) get_user_session_context_from_packet(ipacket);
    if (temp_session == NULL || temp_session->app_data == NULL) {
        return;
    }
    if ((referer != NULL) && temp_session->app_format_id == probe_context->web_id && (((web_session_attr_t *) temp_session->app_data)->has_referer == 0)) {
        int max = (referer->len > 63) ? 63 : referer->len;

        strncpy(((web_session_attr_t *) temp_session->app_data)->referer, (char *) referer->ptr, max);
        ((web_session_attr_t *) temp_session->app_data)->referer[max] = '\0';
        char * coma = strchr(((web_session_attr_t *) temp_session->app_data)->referer, ',');
        if (coma) {
            //Semi column found, replace it by an en of string '\0'
            *coma = '\0';
        }
        ((web_session_attr_t *) temp_session->app_data)->has_referer = 1;
    }
}

void useragent_handle(const ipacket_t * ipacket, attribute_t * attribute, void * user_args) {
    if(ipacket->session == NULL) return;
    mmt_probe_context_t * probe_context = get_probe_context_config();

    http_line_struct_t * user_agent = (http_line_struct_t *) attribute->data;
    session_struct_t *temp_session = (session_struct_t *) get_user_session_context_from_packet(ipacket);
    if (temp_session == NULL || temp_session->app_data == NULL) {
        return;
    }
    if ((user_agent != NULL) && temp_session->app_format_id == probe_context->web_id && (((web_session_attr_t *) temp_session->app_data)->has_useragent == 0)) {
        int max = (user_agent->len > 63) ? 63 : user_agent->len;

        strncpy(((web_session_attr_t *) temp_session->app_data)->useragent, (char *) user_agent->ptr, max);
        ((web_session_attr_t *) temp_session->app_data)->useragent[max] = '\0';
        ((web_session_attr_t *) temp_session->app_data)->has_useragent = 1;
    }
}

void xcdn_seen_handle(const ipacket_t * ipacket, attribute_t * attribute, void * user_args) {
    if(ipacket->session == NULL) return;
    mmt_probe_context_t * probe_context = get_probe_context_config();

    uint8_t * xcdn_seen = (uint8_t *) attribute->data;
    session_struct_t *temp_session = (session_struct_t *) get_user_session_context_from_packet(ipacket);
    if (xcdn_seen != NULL && temp_session != NULL && temp_session->app_data != NULL && temp_session->app_format_id == probe_context->web_id) {
        ((web_session_attr_t *) temp_session->app_data)->xcdn_seen = 1;
    }
}

void http_response_handle(const ipacket_t * ipacket, attribute_t * attribute, void * user_args) {
    if(ipacket->session == NULL) return;
    session_struct_t *temp_session = (session_struct_t *) get_user_session_context_from_packet(ipacket);
    mmt_probe_context_t * probe_context = get_probe_context_config();
    if (temp_session != NULL) {
        if (temp_session->app_data == NULL) {
            web_session_attr_t * http_data = (web_session_attr_t *) malloc(sizeof (web_session_attr_t));
            if (http_data != NULL) {
                memset(http_data, '\0', sizeof (web_session_attr_t));
                temp_session->app_format_id = probe_context->web_id;
                temp_session->app_data = (void *) http_data;
            } else {
                mmt_log(probe_context, MMT_L_WARNING, MMT_P_MEM_ERROR, "Memory error while creating HTTP reporting context");
                //fprintf(stderr, "Out of memory error when creating HTTP specific data structure!\n");
                return;
            }
        }
        if(temp_session->app_format_id == probe_context->web_id) {
            if (((web_session_attr_t *) temp_session->app_data)->trans_nb == 1) {
                ((web_session_attr_t *) temp_session->app_data)->response_time = mmt_time_diff(((web_session_attr_t *) temp_session->app_data)->response_time, ipacket->p_hdr->ts);
                ((web_session_attr_t *) temp_session->app_data)->seen_response = 1;
            }
            ((web_session_attr_t *) temp_session->app_data)->interaction_time = ipacket->p_hdr->ts;
        }
    }
}

void print_web_app_format(const mmt_session_t * expired_session, probe_internal_t * iprobe) {
    int keep_direction = 1;
    session_struct_t * temp_session = get_user_session_context(expired_session);
    char path[128];
    char message[MAX_MESS + 1];

    //common fields
    //format id, timestamp
    //Flow_id, Start timestamp, IP version, Server_Address, Client_Address, Server_Port, Client_Port, Transport Protocol ID,
    //Uplink Packet Count, Downlink Packet Count, Uplink Byte Count, Downlink Byte Count, TCP RTT, Retransmissions,
    //Application_Family, Content Class, Protocol_Path, Application_Name

    //proto_hierarchy_to_str(&expired_session->proto_path, path);

    mmt_probe_context_t * probe_context = get_probe_context_config();
    proto_hierarchy_ids_to_str(get_session_protocol_hierarchy(expired_session), path);

    uint64_t session_id = get_session_id(expired_session);
    if (probe_context->thread_nb > 1) {
        session_id <<= probe_context->thread_nb_2_power;
        session_id |= iprobe->instance_id;
    }
    char dev_prop[12];

    if ((probe_context->user_agent_parsing_threshold) && (get_session_byte_count(expired_session) > probe_context->user_agent_parsing_threshold)) {
        mmt_dev_properties_t dev_p = get_dev_properties_from_user_agent(((web_session_attr_t *) temp_session->app_data)->useragent, 128);
        sprintf(dev_prop, "%hu:%hu", dev_p.dev_id, dev_p.os_id);
    } else {
        dev_prop[0] = '\0';
    }
    //IP strings
    char ip_src_str[46];
    char ip_dst_str[46];
    if (temp_session->ipversion == 4) {
        inet_ntop(AF_INET, (void *) &temp_session->ipclient.ipv4, ip_src_str, INET_ADDRSTRLEN);
        inet_ntop(AF_INET, (void *) &temp_session->ipserver.ipv4, ip_dst_str, INET_ADDRSTRLEN);
        keep_direction = is_local_net(temp_session->ipclient.ipv4);
    } else {
        inet_ntop(AF_INET6, (void *) &temp_session->ipclient.ipv6, ip_src_str, INET6_ADDRSTRLEN);
        inet_ntop(AF_INET6, (void *) &temp_session->ipserver.ipv6, ip_dst_str, INET6_ADDRSTRLEN);
    }

    uint32_t rtt_ms = TIMEVAL_2_MSEC(get_session_rtt(expired_session));
    uint32_t cdn_flag = 0;

    if (((web_session_attr_t *) temp_session->app_data)->xcdn_seen) cdn_flag = ((web_session_attr_t *) temp_session->app_data)->xcdn_seen;
    else if (get_session_content_flags(expired_session) & MMT_CONTENT_CDN) cdn_flag = 2;

    struct timeval init_time = get_session_init_time(expired_session);
    struct timeval end_time = get_session_last_activity_time(expired_session);
    const proto_hierarchy_t * proto_hierarchy = get_session_protocol_hierarchy(expired_session);

    snprintf(message, MAX_MESS,
            "%u,%u,\"%s\",%lu.%lu,%"PRIu64",%lu.%lu,%u,\"%s\",\"%s\",%hu,%hu,%hu,%"PRIu64",%"PRIu64",%"PRIu64",%"PRIu64",%u,%u,%u,%u,\"%s\",%u,%u,%u,%u,\"%s\",\"%s\",\"%s\",\"%s\",%u", // app specific
            temp_session->app_format_id, probe_context->probe_id_number, probe_context->input_source, end_time.tv_sec, end_time.tv_usec,
            session_id,
            init_time.tv_sec, init_time.tv_usec,
            (int) temp_session->ipversion,
            ip_dst_str, ip_src_str,
            temp_session->serverport, temp_session->clientport, (unsigned short) temp_session->proto,
            (keep_direction)?get_session_ul_packet_count(expired_session):get_session_dl_packet_count(expired_session),
                    (keep_direction)?get_session_dl_packet_count(expired_session):get_session_ul_packet_count(expired_session),
                            (keep_direction)?get_session_ul_byte_count(expired_session):get_session_dl_byte_count(expired_session),
                                    (keep_direction)?get_session_dl_byte_count(expired_session):get_session_ul_byte_count(expired_session),
                                            rtt_ms, get_session_retransmission_count(expired_session),
                                            get_application_class_by_protocol_id(proto_hierarchy->proto_path[(proto_hierarchy->len <= 16)?(proto_hierarchy->len - 1):(16 - 1)]),
                                            temp_session->contentclass, path, proto_hierarchy->proto_path[(proto_hierarchy->len <= 16)?(proto_hierarchy->len - 1):(16 - 1)],
                                            (((web_session_attr_t *) temp_session->app_data)->seen_response) ? (uint32_t) TIMEVAL_2_MSEC(((web_session_attr_t *) temp_session->app_data)->response_time) : 0,
                                                    (((web_session_attr_t *) temp_session->app_data)->seen_response) ? ((web_session_attr_t *) temp_session->app_data)->trans_nb : 0,
                                                            (((web_session_attr_t *) temp_session->app_data)->seen_response) ? (uint32_t) TIMEVAL_2_MSEC(mmt_time_diff(((web_session_attr_t *) temp_session->app_data)->first_request_time, ((web_session_attr_t *) temp_session->app_data)->interaction_time)) : 0,
                                                                    ((web_session_attr_t *) temp_session->app_data)->hostname,
                                                                    ((web_session_attr_t *) temp_session->app_data)->mimetype, ((web_session_attr_t *) temp_session->app_data)->referer,
                                                                    dev_prop, cdn_flag
    );

    message[ MAX_MESS ] = '\0'; // correct end of string in case of truncated message
    if (probe_context->output_to_file_enable==1)send_message_to_file (message);
    if (probe_context->redis_enable==1)send_message_to_redis ("web.flow.report", message);

}

void print_initial_web_report(ip_statistics_t *p, char message [MAX_MESS + 1],int valid){
    uint32_t cdn_flag = 0;
    if (((web_session_attr_t *) p->ip_temp_session->app_data)->xcdn_seen) cdn_flag = ((web_session_attr_t *) p->ip_temp_session->app_data)->xcdn_seen;
    else if (get_session_content_flags(p->mmt_session) & MMT_CONTENT_CDN) cdn_flag = 2;

    snprintf(&message[valid], MAX_MESS-valid,",%u,%u,%u,%u,%u,%u,\"%s\",\"%s\",\"%s\",%u", // app specific
            p->ip_temp_session->app_format_id,get_application_class_by_protocol_id(p->proto_stats->proto_hierarchy->proto_path[(p->proto_stats->proto_hierarchy->len <= 16)?(p->proto_stats->proto_hierarchy->len - 1):(16 - 1)]),
            p->ip_temp_session->contentclass,
            (((web_session_attr_t *) p->ip_temp_session->app_data)->seen_response) ? (uint32_t) TIMEVAL_2_MSEC(((web_session_attr_t *) p->ip_temp_session->app_data)->response_time) : 0,
                    (((web_session_attr_t *) p->ip_temp_session->app_data)->seen_response) ? ((web_session_attr_t *) p->ip_temp_session->app_data)->trans_nb : 0,
                            (((web_session_attr_t *) p->ip_temp_session->app_data)->seen_response) ? (uint32_t) TIMEVAL_2_MSEC(mmt_time_diff(((web_session_attr_t *) p->ip_temp_session->app_data)->first_request_time, ((web_session_attr_t *) p->ip_temp_session->app_data)->interaction_time)) : 0,
                                    ((web_session_attr_t *) p->ip_temp_session->app_data)->hostname,
                                    ((web_session_attr_t *) p->ip_temp_session->app_data)->mimetype, ((web_session_attr_t *) p->ip_temp_session->app_data)->referer,cdn_flag

    );
    p->counter=1;

}

/*
void register_web_attributes(void * handler){
    int i = 1;
    i &= register_attribute_handler(handler, PROTO_HTTP, RFC2822_METHOD, http_method_handle, NULL, NULL);
    i &= register_attribute_handler(handler, PROTO_HTTP, RFC2822_RESPONSE, http_response_handle, NULL, NULL);
    i &= register_attribute_handler(handler, PROTO_HTTP, RFC2822_CONTENT_TYPE, mime_handle, NULL, NULL);
    i &= register_attribute_handler(handler, PROTO_HTTP, RFC2822_HOST, host_handle, NULL, NULL);
    i &= register_attribute_handler(handler, PROTO_HTTP, RFC2822_REFERER, referer_handle, NULL, NULL);
    i &= register_attribute_handler(handler, PROTO_HTTP, RFC2822_USER_AGENT, useragent_handle, NULL, NULL);
    i &= register_attribute_handler(handler, PROTO_HTTP, RFC2822_XCDN_SEEN, xcdn_seen_handle, NULL, NULL);

    if(!i) {
        //TODO: we need a sound error handling mechanism! Anyway, we should never get here :)
        fprintf(stderr, "Error while initializing MMT handlers and extractions!\n");
    }

}*/

