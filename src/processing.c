#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>

#define __STDC_FORMAT_MACROS
#include <inttypes.h>

#include "mmt_core.h"
#include "tcpip/mmt_tcpip.h"
#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <arpa/inet.h> //inet_ntop
#include <netinet/in.h>
#endif

#ifdef _WIN32
#include <time.h>
#include <windows.h>
#endif

#include "processing.h"
#include <hiredis/hiredis.h>
#include "thredis.h"

static redisContext *redis = NULL;
static thredis_t* thredis = NULL;

#ifdef _WIN32
#ifndef socklen_t
typedef int socklen_t;
#define socklen_t socklen_t
#endif
#endif

#if (_WIN32_WINNT)
void WSAAPI freeaddrinfo(struct addrinfo*);
int WSAAPI getaddrinfo(const char*, const char*, const struct addrinfo*, struct addrinfo**);
int WSAAPI getnameinfo(const struct sockaddr*, socklen_t, char*, DWORD, char*, DWORD, int);
#endif

#ifdef _WIN32

const char *inet_ntop(int af, const void *src, char *dst, socklen_t cnt) {
    if (af == AF_INET) {
        struct sockaddr_in in;
        memset(&in, 0, sizeof (in));
        in.sin_family = AF_INET;
        memcpy(&in.sin_addr, src, sizeof (struct in_addr));
        getnameinfo((struct sockaddr *) &in, sizeof (struct
                sockaddr_in), dst, cnt, NULL, 0, NI_NUMERICHOST);
        return dst;
    } else if (af == AF_INET6) {
        struct sockaddr_in6 in;
        memset(&in, 0, sizeof (in));
        in.sin6_family = AF_INET6;
        memcpy(&in.sin6_addr, src, sizeof (struct in_addr6));
        getnameinfo((struct sockaddr *) &in, sizeof (struct
                sockaddr_in6), dst, cnt, NULL, 0, NI_NUMERICHOST);
        return dst;
    }
    return NULL;
}
#endif

#define TIMEVAL_2_MSEC(tval) ((tval.tv_sec << 10) + (tval.tv_usec >> 10))

#define MAX_MESS 2000
char message[MAX_MESS];

int is_local_net(int addr) {
    if ((ntohl(addr) & 0xFF000000 /* 255.0.0.0 */) == 0x0A000000 /* 10.0.0.0 */) {
        return 1;
    }
    return 0;
}

void mmt_log(mmt_probe_context_t * mmt_conf, int level, int code, const char * log_msg) {
    if (level >= mmt_conf->log_level) {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        FILE * log_file = (mmt_conf->log_output != NULL) ? mmt_conf->log_output : stdout;
        fprintf(log_file, "%i\t%lu\t%i\t[%s]\n", level, tv.tv_sec, code, log_msg);
        fflush(log_file);
    }
}

int proto_hierarchy_ids_to_str(const proto_hierarchy_t * proto_hierarchy, char * dest) {
    int offset = 0;
    if (proto_hierarchy->len < 1) {
        offset += sprintf(dest, ".");
    } else {
        int index = 1;
        offset += sprintf(dest, "%u", proto_hierarchy->proto_path[index]);
        index++;
        for (; index < proto_hierarchy->len && index < 16; index++) {
            offset += sprintf(&dest[offset], ".%u", proto_hierarchy->proto_path[index]);
        }
    }
    return offset;
}

int get_protocol_index_from_session(const proto_hierarchy_t * proto_hierarchy, uint32_t proto_id) {
    int index = 0;
    for (; index < proto_hierarchy->len && index < 16; index++) {
        if (proto_hierarchy->proto_path[index] == proto_id) return index;
    }
    return -1;
}

static struct timeval mmt_time_diff(struct timeval tstart, struct timeval tend) {
    tstart.tv_sec = tend.tv_sec - tstart.tv_sec;
    tstart.tv_usec = tend.tv_usec - tstart.tv_usec;
    if ((int) tstart.tv_usec < 0) {
        tstart.tv_usec += 1000000;
        tstart.tv_sec -= 1;
    }
    return tstart;
}

static mmt_probe_context_t probe_context = {0};

mmt_probe_context_t * get_probe_context_config() {
    return & probe_context;
}

typedef struct http_line_struct {
    const uint8_t *ptr;
    uint16_t len;
} http_line_struct_t;

void send_message (FILE * out_file, char *channel, char * message){
    struct timeval timeout = { 1, 500000 }; // 1.5 seconds
    const char *hostname = "127.0.0.1";
    int port = 6379;

    if(message==NULL || message[strlen(message)-1]!= '"'){
        printf("Warning: truncated message not sent\n");
    }

    fprintf (out_file, "%s\n", message);

    // Connect ro redis if not yet done
    if (redis == NULL){
        redis = redisConnectWithTimeout(hostname, port, timeout);
        if (redis == NULL || redis->err) {
            if (redis) {
                printf("Connection error nb %d: %s\n", redis->err, redis->errstr);
                redisFree(redis);
            } else {
                printf("Connection error: can't allocate redis context\n");
            }
            exit(1);
        }
        if (thredis == NULL){
            thredis = thredis_new(redis);
            if(thredis == NULL) {
                 printf("Thredis wrapper thredis_new failed\n");
                 exit(1);
            }
        }
    }

    // Publish an event 
    redisReply *reply;
    //reply = (redisReply *) redisCommand    (  redis, "PUBLISH %s %s", channel, message );
    reply   = (redisReply *) thredis_command (thredis, "PUBLISH %s %s", channel, message );

    if(reply == NULL){
        printf("Redis command error: can't allocate reply context\n");
    }else{
        if(redis->err != 0){
           printf("Redis command error nb %d: %s\n", redis->err, redis->errstr);
        }
        if(reply->type == REDIS_REPLY_ERROR){
           printf("Redis reply error nb %d: %s\n", reply->type, reply->str);
        }
        freeReplyObject(reply);
    }
}

void protocols_stats_iterator(uint32_t proto_id, void * args) {
    FILE * out_file = (probe_context.data_out_file != NULL) ? probe_context.data_out_file : stdout;
    mmt_handler_t * mmt_handler = (mmt_handler_t *) args;
    if (proto_id <= 1) return; //ignor META and UNknown protocols
    proto_statistics_t * proto_stats = get_protocol_stats(mmt_handler, proto_id);
    proto_hierarchy_t proto_hierarchy = {0};
    struct timeval ts = get_last_activity_time(mmt_handler);
    while (proto_stats != NULL) {
        get_protocol_stats_path(mmt_handler, proto_stats, &proto_hierarchy);
        char path[128];
        //proto_hierarchy_to_str(&proto_hierarchy, path);
        proto_hierarchy_ids_to_str(&proto_hierarchy, path);
        /* 
        proto_statistics_t children_stats = {0};
        get_children_stats(proto_stats, & children_stats);
        if ((children_stats.packets_count != 0) && ((proto_stats->packets_count - children_stats.packets_count) != 0)) {
            //The stats instance has children, report the global stats first
            fprintf(out_file, "%u,%lu.%lu,%u,%s,%u,"
                    "%"PRIu64",%"PRIu64",%"PRIu64",%"PRIu64"\n", MMT_STATISTICS_REPORT_FORMAT, ipacket->p_hdr->ts.tv_sec, ipacket->p_hdr->ts.tv_usec, proto_id, path, 0,
                    proto_stats->sessions_count - proto_stats->timedout_sessions_count,
                    proto_stats->data_volume, proto_stats->payload_volume, proto_stats->packets_count);

            fprintf(out_file, "%u,%lu.%lu,%u,%s,%u,"
                    "%"PRIu64",%"PRIu64",%"PRIu64",%"PRIu64"\n", MMT_STATISTICS_REPORT_FORMAT, ipacket->p_hdr->ts.tv_sec, ipacket->p_hdr->ts.tv_usec, proto_id, path, 1,
                    (proto_stats->sessions_count)?(proto_stats->sessions_count - proto_stats->timedout_sessions_count) - (children_stats.sessions_count - children_stats.timedout_sessions_count):0,
                    proto_stats->data_volume - children_stats.data_volume,
                    proto_stats->payload_volume - children_stats.payload_volume,
                    proto_stats->packets_count - children_stats.packets_count);
        } else {
            fprintf(out_file, "%u,%lu.%lu,%u,%s,%u,"
                    "%"PRIu64",%"PRIu64",%"PRIu64",%"PRIu64"\n", MMT_STATISTICS_REPORT_FORMAT, ipacket->p_hdr->ts.tv_sec, ipacket->p_hdr->ts.tv_usec, proto_id, path, 1,
                    proto_stats->sessions_count - proto_stats->timedout_sessions_count,
                    proto_stats->data_volume, proto_stats->payload_volume, proto_stats->packets_count);
        }
        */
	    //report the stats instance if there is anything to report
	    if(proto_stats->touched) {
            snprintf(message, MAX_MESS, 
                "\"%u,%u,'%s',%lu.%lu,%u,'%s',%"PRIu64",%"PRIi64",%"PRIu64",%"PRIu64",%"PRIu64"\"",
                MMT_STATISTICS_REPORT_FORMAT, probe_context.probe_id_number, probe_context.input_source, ts.tv_sec, ts.tv_usec, proto_id, path,
                proto_stats->sessions_count, proto_stats->sessions_count - proto_stats->timedout_sessions_count,
                proto_stats->data_volume, proto_stats->payload_volume, proto_stats->packets_count);
            if(strlen(message)>MAX_MESS-1){
                mmt_log(&probe_context, MMT_L_WARNING, MMT_P_MEM_ERROR, "Memory error while creating message for stats.");
                //fprintf(stderr, "Out of memory error when creating message for stats.\n");
            }
            send_message (out_file, "protocol.stat", message);
            /*
            fprintf(out_file, "%u,%lu.%lu,%u,%s,"
                "%"PRIu64",%"PRIi64",%"PRIu64",%"PRIu64",%"PRIu64"\n", MMT_STATISTICS_REPORT_FORMAT, ts.tv_sec, ts.tv_usec, proto_id, path, 
                proto_stats->sessions_count, proto_stats->sessions_count - proto_stats->timedout_sessions_count,
                //proto_stats->sessions_count, ((int64_t) (proto_stats->sessions_count - proto_stats->timedout_sessions_count) > 0)?proto_stats->sessions_count - proto_stats->timedout_sessions_count:0,
                proto_stats->data_volume, proto_stats->payload_volume, proto_stats->packets_count);
            */
	    }
        reset_statistics(proto_stats);
        proto_stats = proto_stats->next;
    }
}

void packet_handler(const ipacket_t * ipacket, void * args) {
    static time_t last_report_time = 0;
    if (last_report_time == 0) {
        last_report_time = ipacket->p_hdr->ts.tv_sec;
        return;
    }

    if ((ipacket->p_hdr->ts.tv_sec - last_report_time) >= probe_context.stats_reporting_period) {
        iterate_through_protocols(protocols_stats_iterator, (void *) ipacket->mmt_handler);
        last_report_time = ipacket->p_hdr->ts.tv_sec;
    }
}

void rtp_version_handle(const ipacket_t * ipacket, attribute_t * attribute, void * user_args) {
    if(ipacket->session == NULL) return;
    session_struct_t * temp_session = (session_struct_t *) get_user_session_context_from_packet(ipacket);
    if (temp_session != NULL) {
        if (temp_session->app_data == NULL) {
            rtp_session_attr_t * rtp_attr = (rtp_session_attr_t *) malloc(sizeof (rtp_session_attr_t));
            if (rtp_attr != NULL) {
                memset(rtp_attr, '\0', sizeof (rtp_session_attr_t));
                temp_session->app_data = (void *) rtp_attr;
                temp_session->app_format_id = MMT_RTP_APP_REPORT_FORMAT;
                rtp_attr->packets_nb += 1;
            } else {
                mmt_log(&probe_context, MMT_L_WARNING, MMT_P_MEM_ERROR, "Memory error while creating RTP reporting context");
                //fprintf(stderr, "Out of memory error when creating RTP specific data structure!\n");
            }
        } else if(temp_session->app_format_id == MMT_RTP_APP_REPORT_FORMAT) {
            ((rtp_session_attr_t*) temp_session->app_data)->packets_nb += 1;
        }
    }
}

void rtp_jitter_handle(const ipacket_t * ipacket, attribute_t * attribute, void * user_args) {
    if(ipacket->session == NULL) return;
    session_struct_t *temp_session = (session_struct_t *) get_user_session_context_from_packet(ipacket);
    if (temp_session != NULL && temp_session->app_data != NULL) {
        uint32_t * jitter = (uint32_t *) attribute->data;
        if (jitter != NULL && temp_session->app_format_id == MMT_RTP_APP_REPORT_FORMAT) {
            if (*jitter > ((rtp_session_attr_t*) temp_session->app_data)->jitter) {
                ((rtp_session_attr_t*) temp_session->app_data)->jitter = *jitter;
            }
        }
    }
}

void rtp_loss_handle(const ipacket_t * ipacket, attribute_t * attribute, void * user_args) {
    if(ipacket->session == NULL) return;
    session_struct_t *temp_session = (session_struct_t *) get_user_session_context_from_packet(ipacket);
    if (temp_session != NULL && temp_session->app_data != NULL) {
        uint16_t * loss = (uint16_t *) attribute->data;
        if (loss != NULL && temp_session->app_format_id == MMT_RTP_APP_REPORT_FORMAT) {
            ((rtp_session_attr_t*) temp_session->app_data)->nb_lost += *loss;
        }
    }
}

void rtp_order_error_handle(const ipacket_t * ipacket, attribute_t * attribute, void * user_args) {
    if(ipacket->session == NULL) return;
    session_struct_t *temp_session = (session_struct_t *) get_user_session_context_from_packet(ipacket);
    if (temp_session != NULL && temp_session->app_data != NULL) {
        uint16_t * order_error = (uint16_t *) attribute->data;
        if (order_error != NULL && temp_session->app_format_id == MMT_RTP_APP_REPORT_FORMAT) {
            ((rtp_session_attr_t*) temp_session->app_data)->nb_order_error += *order_error;
        }
    }
}

void rtp_burst_loss_handle(const ipacket_t * ipacket, attribute_t * attribute, void * user_args) {
    if(ipacket->session == NULL) return;
    session_struct_t *temp_session = (session_struct_t *) get_user_session_context_from_packet(ipacket);
    if (temp_session != NULL && temp_session->app_data != NULL) {
        uint16_t * burst_loss = (uint16_t *) attribute->data;
        if (burst_loss != NULL && temp_session->app_format_id == MMT_RTP_APP_REPORT_FORMAT) {
            ((rtp_session_attr_t*) temp_session->app_data)->nb_loss_bursts += 1;
        }
    }
}

void ssl_server_name_handle(const ipacket_t * ipacket, attribute_t * attribute, void * user_args) {
    if(ipacket->session == NULL) return;
    session_struct_t *temp_session = (session_struct_t *) get_user_session_context_from_packet(ipacket);
    if (temp_session != NULL) {
        if (temp_session->app_data == NULL) {
            ssl_session_attr_t * ssl_data = (ssl_session_attr_t *) malloc(sizeof (ssl_session_attr_t));
            if (ssl_data != NULL) {
                memset(ssl_data, '\0', sizeof (ssl_session_attr_t));
                temp_session->app_format_id = MMT_SSL_APP_REPORT_FORMAT;
                temp_session->app_data = (void *) ssl_data;
            } else {
                mmt_log(&probe_context, MMT_L_WARNING, MMT_P_MEM_ERROR, "Memory error while creating SSL reporting context");
                //fprintf(stderr, "Out of memory error when creating SSL specific data structure!\n");
                return;
            }
        }
        http_line_struct_t * server_name = (http_line_struct_t *) attribute->data;
        if (server_name != NULL && temp_session->app_format_id == MMT_SSL_APP_REPORT_FORMAT) {
            uint16_t max = ((uint16_t) server_name->len > 63) ? 63 : server_name->len;
            strncpy(((ssl_session_attr_t *) temp_session->app_data)->hostname, (char *) server_name->ptr, max);
            ((ssl_session_attr_t *) temp_session->app_data)->hostname[max] = '\0';
        }
    }
}

void mime_handle(const ipacket_t * ipacket, attribute_t * attribute, void * user_args) {
    if(ipacket->session == NULL) return;
    http_line_struct_t * mime = (http_line_struct_t *) attribute->data;
    session_struct_t *temp_session = (session_struct_t *) get_user_session_context_from_packet(ipacket);
    if (temp_session == NULL || temp_session->app_data == NULL) {
        return;
    }
    if (mime != NULL && temp_session->app_format_id == MMT_WEB_APP_REPORT_FORMAT) {
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
    http_line_struct_t * host = (http_line_struct_t *) attribute->data;
    session_struct_t *temp_session = (session_struct_t *) get_user_session_context_from_packet(ipacket);
    if (temp_session == NULL || temp_session->app_data == NULL) {
        return;
    }
    if (host != NULL && temp_session->app_format_id == MMT_WEB_APP_REPORT_FORMAT) {
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
    if (temp_session != NULL) {
        if (temp_session->app_data == NULL) {
            web_session_attr_t * http_data = (web_session_attr_t *) malloc(sizeof (web_session_attr_t));
            if (http_data != NULL) {
                memset(http_data, '\0', sizeof (web_session_attr_t));
                temp_session->app_format_id = MMT_WEB_APP_REPORT_FORMAT;
                temp_session->app_data = (void *) http_data;
            } else {
                mmt_log(&probe_context, MMT_L_WARNING, MMT_P_MEM_ERROR, "Memory error while creating HTTP reporting context");
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
    http_line_struct_t * referer = (http_line_struct_t *) attribute->data;
    session_struct_t *temp_session = (session_struct_t *) get_user_session_context_from_packet(ipacket);
    if (temp_session == NULL || temp_session->app_data == NULL) {
        return;
    }
    if ((referer != NULL) && temp_session->app_format_id == MMT_WEB_APP_REPORT_FORMAT && (((web_session_attr_t *) temp_session->app_data)->has_referer == 0)) {
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
    http_line_struct_t * user_agent = (http_line_struct_t *) attribute->data;
    session_struct_t *temp_session = (session_struct_t *) get_user_session_context_from_packet(ipacket);
    if (temp_session == NULL || temp_session->app_data == NULL) {
        return;
    }
    if ((user_agent != NULL) && temp_session->app_format_id == MMT_WEB_APP_REPORT_FORMAT && (((web_session_attr_t *) temp_session->app_data)->has_useragent == 0)) {
        int max = (user_agent->len > 63) ? 63 : user_agent->len;

        strncpy(((web_session_attr_t *) temp_session->app_data)->useragent, (char *) user_agent->ptr, max);
        ((web_session_attr_t *) temp_session->app_data)->useragent[max] = '\0';
        ((web_session_attr_t *) temp_session->app_data)->has_useragent = 1;
    }
}

void xcdn_seen_handle(const ipacket_t * ipacket, attribute_t * attribute, void * user_args) {
    if(ipacket->session == NULL) return;
    uint8_t * xcdn_seen = (uint8_t *) attribute->data;
    session_struct_t *temp_session = (session_struct_t *) get_user_session_context_from_packet(ipacket);
    if (xcdn_seen != NULL && temp_session != NULL && temp_session->app_data != NULL && temp_session->app_format_id == MMT_WEB_APP_REPORT_FORMAT) {
        ((web_session_attr_t *) temp_session->app_data)->xcdn_seen = 1;
    }
}

void http_response_handle(const ipacket_t * ipacket, attribute_t * attribute, void * user_args) {
    if(ipacket->session == NULL) return;
    session_struct_t *temp_session = (session_struct_t *) get_user_session_context_from_packet(ipacket);
    if (temp_session != NULL) {
        if (temp_session->app_data == NULL) {
            web_session_attr_t * http_data = (web_session_attr_t *) malloc(sizeof (web_session_attr_t));
            if (http_data != NULL) {
                memset(http_data, '\0', sizeof (web_session_attr_t));
                temp_session->app_format_id = MMT_WEB_APP_REPORT_FORMAT;
                temp_session->app_data = (void *) http_data;
            } else {
                mmt_log(&probe_context, MMT_L_WARNING, MMT_P_MEM_ERROR, "Memory error while creating HTTP reporting context");
                //fprintf(stderr, "Out of memory error when creating HTTP specific data structure!\n");
                return;
            }
        }
        if(temp_session->app_format_id == MMT_WEB_APP_REPORT_FORMAT) {
            if (((web_session_attr_t *) temp_session->app_data)->trans_nb == 1) {
                ((web_session_attr_t *) temp_session->app_data)->response_time = mmt_time_diff(((web_session_attr_t *) temp_session->app_data)->response_time, ipacket->p_hdr->ts);
                ((web_session_attr_t *) temp_session->app_data)->seen_response = 1;
            }
            ((web_session_attr_t *) temp_session->app_data)->interaction_time = ipacket->p_hdr->ts;
        }
    }
}

/*
void uri_handle(const ipacket_t * ipacket, attribute_t * attribute, void * user_args) {
    http_line_struct_t * uri = (http_line_struct_t *) attribute->data;
    if (uri != NULL) {
        int max = (uri->len > 1024) ? 1024 : uri->len;

        session_struct_t *temp_session = (session_struct_t *) get_user_session_context(ipacket);
        session_struct_t *temp_session = (session_struct_t *) ipacket->session->user_data;
        strncpy(temp_session->uri, (char *) uri->ptr, max);
        temp_session->uri[max] = '\0';
    }
}
 */

void flow_nb_handle(const ipacket_t * ipacket, attribute_t * attribute, void * user_args) {
    mmt_session_t * session = get_session_from_packet(ipacket);
    if(session == NULL) return;
    //fprintf(stdout, "Test from new flow\n");

    if (attribute->data == NULL) {
        return; //This should never happen! check it anyway
    }

    session_struct_t *temp_session = malloc(sizeof (session_struct_t));

    if (temp_session == NULL) {
        mmt_log(&probe_context, MMT_L_WARNING, MMT_P_MEM_ERROR, "Memory error while creating new flow reporting context");
        //fprintf(stderr, "Memory allocation failed when creating a new file reporting struct! This flow will be ignored! Sorry!");
        return;
    }

    memset(temp_session, '\0', sizeof (session_struct_t));

    temp_session->format_id = MMT_FLOW_REPORT_FORMAT;
    temp_session->app_format_id = MMT_DEFAULT_APP_REPORT_FORMAT;

    if (temp_session->isFlowExtracted)
        return;

    // Flow extraction
    int ipindex = get_protocol_index_by_id(ipacket, PROTO_IP);
    //printf("IP index = %i\n", ipindex);
    //Process IPv4 flows
    if (ipindex != -1) {
        
        uint32_t * ip_src = (uint32_t *) get_attribute_extracted_data(ipacket, PROTO_IP, IP_SRC);
        uint32_t * ip_dst = (uint32_t *) get_attribute_extracted_data(ipacket, PROTO_IP, IP_DST);

        if (ip_src) {
            //printf("HAS IP ADDRESS \n");
            temp_session->ipclient.ipv4 = (*ip_src);
        }
        if (ip_dst) {
            temp_session->ipserver.ipv4 = (*ip_dst);
        }

        uint8_t * proto_id = (uint8_t *) get_attribute_extracted_data(ipacket, PROTO_IP, IP_PROTO_ID);
        if (proto_id != NULL) {
            temp_session->proto = *proto_id;
        } else {
            temp_session->proto = 0;
        }
        temp_session->ipversion = 4;
        uint16_t * cport = (uint16_t *) get_attribute_extracted_data(ipacket, PROTO_IP, IP_CLIENT_PORT);
        uint16_t * dport = (uint16_t *) get_attribute_extracted_data(ipacket, PROTO_IP, IP_SERVER_PORT);
        if (cport) {
            temp_session->clientport = *cport;
        }
        if (dport) {
            temp_session->serverport = *dport;
        }

    } else {
        void * ipv6_src = (void *) get_attribute_extracted_data(ipacket, PROTO_IPV6, IP6_SRC);
        void * ipv6_dst = (void *) get_attribute_extracted_data(ipacket, PROTO_IPV6, IP6_DST);
        if (ipv6_src) {
            memcpy(&temp_session->ipclient.ipv6, ipv6_src, 16);
        }
        if (ipv6_dst) {
            memcpy(&temp_session->ipserver.ipv6, ipv6_dst, 16);
        }

        uint8_t * proto_id = (uint8_t *) get_attribute_extracted_data(ipacket, PROTO_IPV6, IP6_NEXT_PROTO);
        if (proto_id != NULL) {
            temp_session->proto = *proto_id;
        } else {
            temp_session->proto = 0;
        }
        temp_session->ipversion = 6;
        uint16_t * cport = (uint16_t *) get_attribute_extracted_data(ipacket, PROTO_IPV6, IP6_CLIENT_PORT);
        uint16_t * dport = (uint16_t *) get_attribute_extracted_data(ipacket, PROTO_IPV6, IP6_SERVER_PORT);
        if (cport) {
            temp_session->clientport = *cport;
        }
        if (dport) {
            temp_session->serverport = *dport;
        }
    }

    temp_session->isFlowExtracted = 1;

    set_user_session_context(session, temp_session);
}

void proto_stats_init(void * handler) {
    register_packet_handler(handler, 1, packet_handler, NULL);
}

void proto_stats_cleanup(void * handler) {
    iterate_through_protocols(protocols_stats_iterator, handler);
    (void) unregister_packet_handler((mmt_handler_t *) handler, 1);
}

void flowstruct_init(void * handler) {
    int i = 1;
    i &= register_extraction_attribute(handler, PROTO_TCP, TCP_SRC_PORT);
    i &= register_extraction_attribute(handler, PROTO_TCP, TCP_DEST_PORT);
    i &= register_extraction_attribute(handler, PROTO_UDP, UDP_SRC_PORT);
    i &= register_extraction_attribute(handler, PROTO_UDP, UDP_DEST_PORT);

    i &= register_extraction_attribute(handler, PROTO_IP, IP_SRC);
    i &= register_extraction_attribute(handler, PROTO_IP, IP_DST);
    i &= register_extraction_attribute(handler, PROTO_IP, IP_PROTO_ID);
    i &= register_extraction_attribute(handler, PROTO_IP, IP_SERVER_PORT);
    i &= register_extraction_attribute(handler, PROTO_IP, IP_CLIENT_PORT);

    i &= register_extraction_attribute(handler, PROTO_IPV6, IP6_NEXT_PROTO);
    i &= register_extraction_attribute(handler, PROTO_IPV6, IP6_SRC);
    i &= register_extraction_attribute(handler, PROTO_IPV6, IP6_DST);
    i &= register_extraction_attribute(handler, PROTO_IPV6, IP6_SERVER_PORT);
    i &= register_extraction_attribute(handler, PROTO_IPV6, IP6_CLIENT_PORT);

    i &= register_attribute_handler(handler, PROTO_IP, PROTO_SESSION, flow_nb_handle, NULL, NULL);
    i &= register_attribute_handler(handler, PROTO_IPV6, PROTO_SESSION, flow_nb_handle, NULL, NULL);

    i &= register_attribute_handler(handler, PROTO_SSL, SSL_SERVER_NAME, ssl_server_name_handle, NULL, NULL);
    i &= register_attribute_handler(handler, PROTO_HTTP, RFC2822_METHOD, http_method_handle, NULL, NULL);
    i &= register_attribute_handler(handler, PROTO_HTTP, RFC2822_RESPONSE, http_response_handle, NULL, NULL);
    i &= register_attribute_handler(handler, PROTO_HTTP, RFC2822_CONTENT_TYPE, mime_handle, NULL, NULL);
    i &= register_attribute_handler(handler, PROTO_HTTP, RFC2822_HOST, host_handle, NULL, NULL);
    i &= register_attribute_handler(handler, PROTO_HTTP, RFC2822_REFERER, referer_handle, NULL, NULL);
    i &= register_attribute_handler(handler, PROTO_HTTP, RFC2822_USER_AGENT, useragent_handle, NULL, NULL);
    i &= register_attribute_handler(handler, PROTO_HTTP, RFC2822_XCDN_SEEN, xcdn_seen_handle, NULL, NULL);
    i &= register_attribute_handler(handler, PROTO_RTP, RTP_VERSION, rtp_version_handle, NULL, NULL);
    i &= register_attribute_handler(handler, PROTO_RTP, RTP_JITTER, rtp_jitter_handle, NULL, NULL);
    i &= register_attribute_handler(handler, PROTO_RTP, RTP_LOSS, rtp_loss_handle, NULL, NULL);
    i &= register_attribute_handler(handler, PROTO_RTP, RTP_UNORDER, rtp_order_error_handle, NULL, NULL);
    i &= register_attribute_handler(handler, PROTO_RTP, RTP_ERROR_ORDER, rtp_order_error_handle, NULL, NULL);
    i &= register_attribute_handler(handler, PROTO_RTP, RTP_BURST_LOSS, rtp_burst_loss_handle, NULL, NULL);
    
    if(!i) {
        //TODO: we need a sound error handling mechanism! Anyway, we should never get here :)
        fprintf(stderr, "Error while initializing MMT handlers and etractions!\n");
    }
}

void flowstruct_cleanup(void * handler) {
}

struct mmt_location_info_struct {
    uint32_t field_len;
    uint32_t opaque;
    uint16_t cell_lac;
    uint16_t cell_id;
};

void radius_code_handle(const ipacket_t * ipacket, attribute_t * attribute, void * user_args) {
    //FILE * out_file = (user_args != NULL) ? (FILE *) user_args : stdout;
    if(ipacket->session == NULL) return;
    FILE * out_file = (probe_context.radius_out_file != NULL) ? probe_context.radius_out_file : stdout;

    //Mark this flow as SKIP REPORTING one! Yeah we don't want to report RADIUS flows 
    //Just report the RADIUS specific report
    session_struct_t *temp_session = (session_struct_t *) get_user_session_context_from_packet(ipacket);
    if (temp_session != NULL) {
        temp_session->format_id = MMT_RADIUS_REPORT_FORMAT;
        //temp_session->app_format_id = MMT_SKIP_APP_REPORT_FORMAT;
    }

    if (attribute->data) {
        char f_ipv4[INET_ADDRSTRLEN];
        char sgsn_ip[INET_ADDRSTRLEN];
        char ggsn_ip[INET_ADDRSTRLEN];
        uint8_t code = *((uint8_t *) attribute->data);

        //If report ALL or The code is the one we need to report, then report :)
        if ((probe_context.radius_starategy == MMT_RADIUS_REPORT_ALL) || (code == probe_context.radius_message_id)) {
            char * calling_station_id = get_attribute_extracted_data(ipacket, PROTO_RADIUS, RADIUS_CALLING_STATION_ID);
            uint32_t * framed_ip_address = get_attribute_extracted_data(ipacket, PROTO_RADIUS, RADIUS_FRAMED_IP_ADDRESS);

            //Report if we have a reporting condition and the condition is met
            if (probe_context.radius_condition_id == MMT_RADIUS_IP_MSISDN_PRESENT) {
                if ((calling_station_id != NULL) && (framed_ip_address != NULL)) {
                    uint32_t * account_status_type = get_attribute_extracted_data(ipacket, PROTO_RADIUS, RADIUS_ACCT_STATUS_TYPE);
                    char * account_session_id = get_attribute_extracted_data(ipacket, PROTO_RADIUS, RADIUS_ACCT_SESSION_ID);
                    char * imsi = get_attribute_extracted_data(ipacket, PROTO_RADIUS, RADIUS_3GPP_IMSI);
                    char * imei = get_attribute_extracted_data(ipacket, PROTO_RADIUS, RADIUS_3GPP_IMEISV);
                    char * user_loc = get_attribute_extracted_data(ipacket, PROTO_RADIUS, RADIUS_3GPP_USER_LOCATION);
                    char * charg_charact = get_attribute_extracted_data(ipacket, PROTO_RADIUS, RADIUS_3GPP_CHARGIN_CHARACT);
                    uint8_t * rat_type = get_attribute_extracted_data(ipacket, PROTO_RADIUS, RADIUS_3GPP_RAT_TYPE);
                    uint32_t * sgsn_ip_address = get_attribute_extracted_data(ipacket, PROTO_RADIUS, RADIUS_3GPP_SGSN_ADDRESS);
                    uint32_t * ggsn_ip_address = get_attribute_extracted_data(ipacket, PROTO_RADIUS, RADIUS_3GPP_GGSN_ADDRESS);
                    //ipv6_addr_t * sgsn_ipv6 = get_attribute_extracted_data(ipacket, PROTO_RADIUS, RADIUS_3GPP_SGSN_IPV6);
                    //ipv6_addr_t * ggsn_ipv6 = get_attribute_extracted_data(ipacket, PROTO_RADIUS, RADIUS_3GPP_GGSN_IPV6);
                    char * sgsn_mccmnc = get_attribute_extracted_data(ipacket, PROTO_RADIUS, RADIUS_3GPP_SGSN_MCCMNC);
                    if (framed_ip_address) {
                        inet_ntop(AF_INET, framed_ip_address, f_ipv4, INET_ADDRSTRLEN);
                    }
                    if (sgsn_ip_address) {
                        inet_ntop(AF_INET, sgsn_ip_address, sgsn_ip, INET_ADDRSTRLEN);
                    }
                    if (ggsn_ip_address) {
                        inet_ntop(AF_INET, ggsn_ip_address, ggsn_ip, INET_ADDRSTRLEN);
                    }

                    //format id, timestamp, msg code, IP address, MSISDN, Acct_session_id, Acct_status_type, IMSI, IMEI, GGSN IP, SGSN IP, SGSN-MCC-MNC, RAT type, Charging class, LAC id, Cell id
                    snprintf(message, MAX_MESS, 
                        "\"%u,%u,'%s',%lu.%lu,%i,'%s','%s',%i,'%s','%s','%s','%s','%s','%s',%i,'%s',%i,%i\"",
                        MMT_RADIUS_REPORT_FORMAT, probe_context.probe_id_number, probe_context.input_source, ipacket->p_hdr->ts.tv_sec, ipacket->p_hdr->ts.tv_usec, 
                        (int) code, (framed_ip_address != NULL) ? f_ipv4 : "",
                        (calling_station_id != NULL) ? &calling_station_id[4] : "",
                        (account_status_type != NULL) ? *account_status_type : 0,
                        (account_session_id != NULL) ? &account_session_id[4] : "",
                        (imsi != NULL) ? &imsi[4] : "",
                        (imei != NULL) ? &imei[4] : "",
                        (ggsn_ip_address != NULL) ? ggsn_ip : "",
                        (sgsn_ip_address != NULL) ? sgsn_ip : "",
                        (sgsn_mccmnc != NULL) ? &sgsn_mccmnc[4] : "",
                        (rat_type != NULL) ? (int) *((uint8_t *) rat_type) : 0,
                        (charg_charact != NULL) ? &charg_charact[4] : "",
                        (user_loc != NULL) ? (int) ntohs(((struct mmt_location_info_struct *) user_loc)->cell_lac) : 0,
                        (user_loc != NULL) ? (int) ntohs(((struct mmt_location_info_struct *) user_loc)->cell_id) : 0
                    );
                    if(strlen(message)>MAX_MESS-1){
                        mmt_log(&probe_context, MMT_L_WARNING, MMT_P_MEM_ERROR, "Memory error while creating message for radius.");
                        //fprintf(stderr, "Out of memory error when creating message for stats.\n");
                    }
                    send_message (out_file, "radius.report", message);
                    /* 
                    fprintf(out_file, "%i,%lu.%lu,%i,%s,%s,%i,%s,%s,%s,%s,%s,%s,%i,%s,%i,%i\n", MMT_RADIUS_REPORT_FORMAT, ipacket->p_hdr->ts.tv_sec, ipacket->p_hdr->ts.tv_usec,
                        (int) code, (framed_ip_address != NULL) ? f_ipv4 : "",
                        (calling_station_id != NULL) ? &calling_station_id[4] : "",
                        (account_status_type != NULL) ? *account_status_type : 0,
                        (account_session_id != NULL) ? &account_session_id[4] : "",
                        (imsi != NULL) ? &imsi[4] : "",
                        (imei != NULL) ? &imei[4] : "",
                        (ggsn_ip_address != NULL) ? ggsn_ip : "",
                        (sgsn_ip_address != NULL) ? sgsn_ip : "",
                        (sgsn_mccmnc != NULL) ? &sgsn_mccmnc[4] : "",
                        (rat_type != NULL) ? (int) *((uint8_t *) rat_type) : 0,
                        (charg_charact != NULL) ? &charg_charact[4] : "",
                        (user_loc != NULL) ? (int) ntohs(((struct mmt_location_info_struct *) user_loc)->cell_lac) : 0,
                        (user_loc != NULL) ? (int) ntohs(((struct mmt_location_info_struct *) user_loc)->cell_id) : 0
                    );
                    */
                }
            } else { //Report anyway
                uint32_t * account_status_type = get_attribute_extracted_data(ipacket, PROTO_RADIUS, RADIUS_ACCT_STATUS_TYPE);
                char * account_session_id = get_attribute_extracted_data(ipacket, PROTO_RADIUS, RADIUS_ACCT_SESSION_ID);
                char * imsi = get_attribute_extracted_data(ipacket, PROTO_RADIUS, RADIUS_3GPP_IMSI);
                char * imei = get_attribute_extracted_data(ipacket, PROTO_RADIUS, RADIUS_3GPP_IMEISV);
                char * user_loc = get_attribute_extracted_data(ipacket, PROTO_RADIUS, RADIUS_3GPP_USER_LOCATION);
                char * charg_charact = get_attribute_extracted_data(ipacket, PROTO_RADIUS, RADIUS_3GPP_CHARGIN_CHARACT);
                uint8_t * rat_type = get_attribute_extracted_data(ipacket, PROTO_RADIUS, RADIUS_3GPP_RAT_TYPE);
                uint32_t * sgsn_ip_address = get_attribute_extracted_data(ipacket, PROTO_RADIUS, RADIUS_3GPP_SGSN_ADDRESS);
                uint32_t * ggsn_ip_address = get_attribute_extracted_data(ipacket, PROTO_RADIUS, RADIUS_3GPP_GGSN_ADDRESS);
                //ipv6_addr_t * sgsn_ipv6 = get_attribute_extracted_data(ipacket, PROTO_RADIUS, RADIUS_3GPP_SGSN_IPV6);
                //ipv6_addr_t * ggsn_ipv6 = get_attribute_extracted_data(ipacket, PROTO_RADIUS, RADIUS_3GPP_GGSN_IPV6);
                char * sgsn_mccmnc = get_attribute_extracted_data(ipacket, PROTO_RADIUS, RADIUS_3GPP_SGSN_MCCMNC);
                if (framed_ip_address) {
                    inet_ntop(AF_INET, framed_ip_address, f_ipv4, INET_ADDRSTRLEN);
                }
                if (sgsn_ip_address) {
                    inet_ntop(AF_INET, sgsn_ip_address, sgsn_ip, INET_ADDRSTRLEN);
                }
                if (ggsn_ip_address) {
                    inet_ntop(AF_INET, ggsn_ip_address, ggsn_ip, INET_ADDRSTRLEN);
                }

                //format id, timestamp, msg code, IP address, MSISDN, Acct_session_id, Acct_status_type, IMSI, IMEI, GGSN IP, SGSN IP, SGSN-MCC-MNC, RAT type, Charging class, LAC id, Cell id
                snprintf(message, MAX_MESS, 
                    "\"%u,%u,'%s',%lu.%lu,%i,'%s','%s',%i,'%s','%s','%s','%s','%s','%s',%i,'%s',%i,%i\"",
                    MMT_RADIUS_REPORT_FORMAT, probe_context.probe_id_number, probe_context.input_source, ipacket->p_hdr->ts.tv_sec, ipacket->p_hdr->ts.tv_usec, 
                    (int) code, (framed_ip_address != NULL) ? f_ipv4 : "",
                    (calling_station_id != NULL) ? &calling_station_id[4] : "",
                    (account_status_type != NULL) ? *account_status_type : 0,
                    (account_session_id != NULL) ? &account_session_id[4] : "",
                    (imsi != NULL) ? &imsi[4] : "",
                    (imei != NULL) ? &imei[4] : "",
                    (ggsn_ip_address != NULL) ? ggsn_ip : "",
                    (sgsn_ip_address != NULL) ? sgsn_ip : "",
                    (sgsn_mccmnc != NULL) ? &sgsn_mccmnc[4] : "",
                    (rat_type != NULL) ? (int) *((uint8_t *) rat_type) : 0,
                    (charg_charact != NULL) ? &charg_charact[4] : "",
                    (user_loc != NULL) ? (int) ntohs(((struct mmt_location_info_struct *) user_loc)->cell_lac) : 0,
                    (user_loc != NULL) ? (int) ntohs(((struct mmt_location_info_struct *) user_loc)->cell_id) : 0
                    );
                if(strlen(message)>MAX_MESS-1){
                    mmt_log(&probe_context, MMT_L_WARNING, MMT_P_MEM_ERROR, "Memory error while creating message for radius2.");
                    //fprintf(stderr, "Out of memory error when creating message for stats.\n");
                }
                send_message (out_file, "radius.report", message);
                /* 
                fprintf(out_file, "%i,%lu.%lu,%i,%s,%s,%i,%s,%s,%s,%s,%s,%s,%i,%s,%i,%i\n", MMT_RADIUS_REPORT_FORMAT, ipacket->p_hdr->ts.tv_sec, ipacket->p_hdr->ts.tv_usec,
                    (int) code, (framed_ip_address != NULL) ? f_ipv4 : "",
                    (calling_station_id != NULL) ? &calling_station_id[4] : "",
                    (account_status_type != NULL) ? *account_status_type : 0,
                    (account_session_id != NULL) ? &account_session_id[4] : "",
                    (imsi != NULL) ? &imsi[4] : "",
                    (imei != NULL) ? &imei[4] : "",
                    (ggsn_ip_address != NULL) ? ggsn_ip : "",
                    (sgsn_ip_address != NULL) ? sgsn_ip : "",
                    (sgsn_mccmnc != NULL) ? &sgsn_mccmnc[4] : "",
                    (rat_type != NULL) ? (int) *((uint8_t *) rat_type) : 0,
                    (charg_charact != NULL) ? &charg_charact[4] : "",
                    (user_loc != NULL) ? (int) ntohs(((struct mmt_location_info_struct *) user_loc)->cell_lac) : 0,
                    (user_loc != NULL) ? (int) ntohs(((struct mmt_location_info_struct *) user_loc)->cell_id) : 0
                    );
                */
            }
        }
    }
}

void radius_ext_init(void * handler) {
    register_attribute_handler(handler, PROTO_RADIUS, RADIUS_CODE, radius_code_handle, NULL, NULL);
    register_extraction_attribute(handler, PROTO_RADIUS, RADIUS_CALLING_STATION_ID);
    register_extraction_attribute(handler, PROTO_RADIUS, RADIUS_FRAMED_IP_ADDRESS);
    register_extraction_attribute(handler, PROTO_RADIUS, RADIUS_ACCT_STATUS_TYPE);
    register_extraction_attribute(handler, PROTO_RADIUS, RADIUS_ACCT_SESSION_ID);
    register_extraction_attribute(handler, PROTO_RADIUS, RADIUS_3GPP_IMSI);
    register_extraction_attribute(handler, PROTO_RADIUS, RADIUS_3GPP_IMEISV);
    register_extraction_attribute(handler, PROTO_RADIUS, RADIUS_3GPP_USER_LOCATION);
    register_extraction_attribute(handler, PROTO_RADIUS, RADIUS_3GPP_CHARGIN_CHARACT);
    register_extraction_attribute(handler, PROTO_RADIUS, RADIUS_3GPP_RAT_TYPE);
    register_extraction_attribute(handler, PROTO_RADIUS, RADIUS_3GPP_SGSN_ADDRESS);
    register_extraction_attribute(handler, PROTO_RADIUS, RADIUS_3GPP_GGSN_ADDRESS);
    register_extraction_attribute(handler, PROTO_RADIUS, RADIUS_3GPP_SGSN_IPV6);
    register_extraction_attribute(handler, PROTO_RADIUS, RADIUS_3GPP_GGSN_IPV6);
    register_extraction_attribute(handler, PROTO_RADIUS, RADIUS_3GPP_SGSN_MCCMNC);
}

void radius_ext_cleanup(void * handler) {
}

/*
 ** encodeblock
 **
 ** encode 3 8-bit binary bytes as 4 '6-bit' characters
 */
inline void encodeblock(unsigned char in[3], unsigned char out[4], int len) {
    out[0] = cb64[ in[0] >> 2 ];
    out[1] = cb64[ ((in[0] & 0x03) << 4) | ((in[1] & 0xf0) >> 4) ];
    out[2] = (unsigned char) (len > 1 ? cb64[ ((in[1] & 0x0f) << 2) | ((in[2] & 0xc0) >> 6) ] : '=');
    out[3] = (unsigned char) (len > 2 ? cb64[ in[2] & 0x3f ] : '=');
}

/*
 ** encode
 **
 ** base64 encode a string.
 */
inline int encode_str(const char *infile, char *out_file) {
    unsigned char in[3], out[4];
    int i, len;
    int copiedBytes = 0;
    while (infile[0] != '\0') {
        len = 0;
        for (i = 0; i < 3; i++) {
            in[i] = infile[0];
            if (infile[0] != '\0') {
                len++;
            } else {
                in[i] = 0;
            }
            infile++;
        }
        if (len) {
            encodeblock(in, out, len);
            for (i = 0; i < 4; i++) {
                out_file[copiedBytes] = out[i];
                copiedBytes++;
            }
        }
    }
    out_file[copiedBytes] = '\0';
    return copiedBytes;
}

int time_diff(struct timeval t1, struct timeval t2) {
    return (((t2.tv_sec - t1.tv_sec) * 1000000) + (t2.tv_usec - t1.tv_usec)) / 1000;
}

/**
 * Returns 1 if the given session is a microflow, O otherwise
 * @param expired_session pointer to the session context to check
 * @return 1 if the given session is a microflow, O otherwise
 */
uint32_t is_microflow(const mmt_session_t * expired_session) {
    if ((get_session_packet_count(expired_session) <= probe_context.microf_pthreshold) || (get_session_byte_count(expired_session) <= probe_context.microf_bthreshold)) {
        return 1;
    }
    return 0;
}

uint32_t is_microflow_stats_reportable(microsessions_stats_t * stats) {
    if ((stats->flows_nb > probe_context.microf_report_fthreshold)
            || ((stats->dl_pcount + stats->ul_pcount) > probe_context.microf_report_pthreshold)
            || ((stats->dl_bcount + stats->ul_bcount) > probe_context.microf_report_bthreshold)) {
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

void report_all_protocols_microflows_stats(probe_internal_t * iprobe) {
    int i;
    FILE * out_file = (probe_context.data_out_file != NULL) ? probe_context.data_out_file : stdout;
    for (i = 0; i < PROTO_MAX_IDENTIFIER; i++) {
        if (iprobe->mf_stats[i].flows_nb) {
            report_microflows_stats(&iprobe->mf_stats[i], out_file);
        }
    }
}

void report_microflows_stats(microsessions_stats_t * stats, FILE * out_file) {
    //Format id, timestamp, App name, Nb of flows, DL Packet Count, UL Packet Count, DL Byte Count, UL Byte Count
    snprintf(message, MAX_MESS, 
          "\"%u,%u,'%s',%lu.%lu,%u,%u,%u,%u,%u,%u\"",
          MMT_MICROFLOWS_STATS_FORMAT, probe_context.probe_id_number, probe_context.input_source, stats->end_time.tv_sec, stats->end_time.tv_usec,
         stats->application_id, stats->flows_nb, stats->dl_pcount, stats->ul_pcount, stats->dl_bcount, stats->ul_bcount);
     if(strlen(message)>MAX_MESS-1){
         mmt_log(&probe_context, MMT_L_WARNING, MMT_P_MEM_ERROR, "Memory error while creating message for microflows.");
         //fprintf(stderr, "Out of memory error when creating message for stats.\n");
     }
     send_message (out_file, "microflows.report", message);
     /* 
     fprintf(out_file, "%i,%lu.%lu,"
         //"%lu.%lu,"
         "%u,%u,%u,%u,%u,%u\n",
         MMT_MICROFLOWS_STATS_FORMAT,
         //stats->start_time.tv_sec, stats->start_time.tv_usec,
         stats->end_time.tv_sec, stats->end_time.tv_usec,
         stats->application_id,
         stats->flows_nb, stats->dl_pcount, stats->ul_pcount, stats->dl_bcount, stats->ul_bcount);
     */
     //Now clean the stats
     reset_microflows_stats(stats);
}

void update_microflows_stats(microsessions_stats_t * stats, const mmt_session_t * expired_session) {
    int keep_direction = 1;
    session_struct_t * temp_session = get_user_session_context(expired_session);
    if (temp_session->ipversion == 4) {
        keep_direction = is_local_net(temp_session->ipclient.ipv4);
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

void print_default_app_format(const mmt_session_t * expired_session, FILE * out_file, probe_internal_t * iprobe) {
    int keep_direction = 1;
    session_struct_t * temp_session = get_user_session_context(expired_session);
    char path[128];
    //common fields
    //format id, timestamp
    //Flow_id, Start timestamp, IP version, Server_Address, Client_Address, Server_Port, Client_Port, Transport Protocol ID,
    //Uplink Packet Count, Downlink Packet Count, Uplink Byte Count, Downlink Byte Count, TCP RTT, Retransmissions,
    //Application_Family, Content Class, Protocol_Path, Application_Name

    uint64_t session_id = get_session_id(expired_session);
    if (probe_context.thread_nb > 1) {
        session_id <<= probe_context.thread_nb_2_power;
        session_id |= iprobe->instance_id;
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
    //proto_hierarchy_to_str(&expired_session->proto_path, path);
    proto_hierarchy_ids_to_str(get_session_protocol_hierarchy(expired_session), path);

    uint32_t rtt_ms = TIMEVAL_2_MSEC(get_session_rtt(expired_session));

    struct timeval init_time = get_session_init_time(expired_session);
    struct timeval end_time = get_session_last_activity_time(expired_session);
    const proto_hierarchy_t * proto_hierarchy = get_session_protocol_hierarchy(expired_session);
    
    snprintf(message, MAX_MESS, 
        "\"%u,%u,'%s',%lu.%lu,%"PRIu64",%lu.%lu,%u,'%s','%s',%hu,%hu,%hu,%"PRIu64",%"PRIu64",%"PRIu64",%"PRIu64",%u,%u,%u,%u,'%s',%u\"", // app specific 
        temp_session->app_format_id, probe_context.probe_id_number, probe_context.input_source, end_time.tv_sec, end_time.tv_usec, 
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
        temp_session->contentclass, path, proto_hierarchy->proto_path[(proto_hierarchy->len <= 16)?(proto_hierarchy->len - 1):(16 - 1)]
    );
    if(strlen(message)>MAX_MESS-1){
        mmt_log(&probe_context, MMT_L_WARNING, MMT_P_MEM_ERROR, "Memory error while creating message for default.");
        //fprintf(stderr, "Out of memory error when creating message for stats.\n");
    }
    send_message (out_file, "flow.report", message);
    /* 
    fprintf(out_file, "%hu,%lu.%lu,%"PRIu64",%lu.%lu,%u,%s,%s,%hu,%hu,%hu,%"PRIu64",%"PRIu64",%"PRIu64",%"PRIu64",%u,%u,%u,%u,%s,%u,\n", // app specific 
        temp_session->app_format_id, end_time.tv_sec, end_time.tv_usec,
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
        temp_session->contentclass, path, proto_hierarchy->proto_path[(proto_hierarchy->len <= 16)?(proto_hierarchy->len - 1):(16 - 1)]
    );
    */
}

//Response time, Transactions Nb, Interaction time, Hostname, MIME type, Referer, User agent, xcdn_seen

void print_web_app_format(const mmt_session_t * expired_session, FILE * out_file, probe_internal_t * iprobe) {
    int keep_direction = 1;
    session_struct_t * temp_session = get_user_session_context(expired_session);
    char path[128];
    //common fields
    //format id, timestamp
    //Flow_id, Start timestamp, IP version, Server_Address, Client_Address, Server_Port, Client_Port, Transport Protocol ID,
    //Uplink Packet Count, Downlink Packet Count, Uplink Byte Count, Downlink Byte Count, TCP RTT, Retransmissions,
    //Application_Family, Content Class, Protocol_Path, Application_Name

    //proto_hierarchy_to_str(&expired_session->proto_path, path);
    proto_hierarchy_ids_to_str(get_session_protocol_hierarchy(expired_session), path);

    uint64_t session_id = get_session_id(expired_session);
    if (probe_context.thread_nb > 1) {
        session_id <<= probe_context.thread_nb_2_power;
        session_id |= iprobe->instance_id;
    }
    char dev_prop[12];

    if ((probe_context.user_agent_parsing_threshold) && (get_session_byte_count(expired_session) > probe_context.user_agent_parsing_threshold)) {
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
        "\"%u,%u,'%s',%lu.%lu,%"PRIu64",%lu.%lu,%u,'%s','%s',%hu,%hu,%hu,%"PRIu64",%"PRIu64",%"PRIu64",%"PRIu64",%u,%u,%u,%u,'%s',%u,%u,%u,%u,'%s','%s','%s','%s',%u\"", // app specific 
        temp_session->app_format_id, probe_context.probe_id_number, probe_context.input_source, end_time.tv_sec, end_time.tv_usec,
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
    if(strlen(message)>MAX_MESS-1){
        mmt_log(&probe_context, MMT_L_WARNING, MMT_P_MEM_ERROR, "Memory error while creating message for web.");
        //fprintf(stderr, "Out of memory error when creating message for stats.\n");
    }
    send_message (out_file, "web.flow.report", message);
    /* 
    fprintf(out_file, "%hu,%lu.%lu,%"PRIu64",%lu.%lu,%u,%s,%s,%hu,%hu,%hu,%"PRIu64",%"PRIu64",%"PRIu64",%"PRIu64",%u,%u,%u,%u,%s,%u,%u,%u,%u,%s,%s,%s,%s,"
        //"%s,"
        "%u\n", // app specific 
        temp_session->app_format_id, end_time.tv_sec, end_time.tv_usec,
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
        //temp_session->web_attr.useragent,
        dev_prop, cdn_flag
    );
    */
}

void print_ssl_app_format(const mmt_session_t * expired_session, FILE * out_file, probe_internal_t * iprobe) {
    int keep_direction = 1;
    session_struct_t * temp_session = get_user_session_context(expired_session);
    char path[128];
    //common fields
    //format id, timestamp
    //Flow_id, Start timestamp, IP version, Server_Address, Client_Address, Server_Port, Client_Port, Transport Protocol ID,
    //Uplink Packet Count, Downlink Packet Count, Uplink Byte Count, Downlink Byte Count, TCP RTT, Retransmissions,
    //Application_Family, Content Class, Protocol_Path, Application_Name

    uint64_t session_id = get_session_id(expired_session);
    if (probe_context.thread_nb > 1) {
        session_id <<= probe_context.thread_nb_2_power;
        session_id |= iprobe->instance_id;
    }
    //proto_hierarchy_to_str(&expired_session->proto_path, path);
    proto_hierarchy_ids_to_str(get_session_protocol_hierarchy(expired_session), path);
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

    temp_session->contentclass = get_content_class_by_content_flags(get_session_content_flags(expired_session));

    uint32_t rtt_ms = TIMEVAL_2_MSEC(get_session_rtt(expired_session));

    struct timeval init_time = get_session_init_time(expired_session);
    struct timeval end_time = get_session_last_activity_time(expired_session);
    const proto_hierarchy_t * proto_hierarchy = get_session_protocol_hierarchy(expired_session);

    snprintf(message, MAX_MESS, 
        "\"%u,%u,'%s',%lu.%lu,%"PRIu64",%lu.%lu,%u,'%s','%s',%hu,%hu,%hu,%"PRIu64",%"PRIu64",%"PRIu64",%"PRIu64",%u,%u,%u,%u,'%s',%u,'%s',%u\"", // app specific
        temp_session->app_format_id, probe_context.probe_id_number, probe_context.input_source, end_time.tv_sec, end_time.tv_usec,
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
        (((ssl_session_attr_t *) temp_session->app_data) != NULL) ? ((ssl_session_attr_t *) temp_session->app_data)->hostname : "", 
        (get_session_content_flags(expired_session) & MMT_CONTENT_CDN) ? 2 : 0
    );
    if(strlen(message)>MAX_MESS-1){
        mmt_log(&probe_context, MMT_L_WARNING, MMT_P_MEM_ERROR, "Memory error while creating message for ssl.");
        //fprintf(stderr, "Out of memory error when creating message for stats.\n");
    }
    send_message (out_file, "ssl.flow.report", message);
    /* 
    fprintf(out_file, "%hu,%lu.%lu,%"PRIu64",%lu.%lu,%u,%s,%s,%hu,%hu,%hu,%"PRIu64",%"PRIu64",%"PRIu64",%"PRIu64",%u,%u,%u,%u,%s,%u,%s,%u\n", // app specific
        (uint16_t) MMT_SSL_APP_REPORT_FORMAT, end_time.tv_sec, end_time.tv_usec,
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
        (((ssl_session_attr_t *) temp_session->app_data) != NULL) ? ((ssl_session_attr_t *) temp_session->app_data)->hostname : "", (get_session_content_flags(expired_session) & MMT_CONTENT_CDN) ? 2 : 0
    );
    */
}

void print_rtp_app_format(const mmt_session_t * expired_session, FILE * out_file, probe_internal_t * iprobe) {
    int keep_direction = 1;
    session_struct_t * temp_session = get_user_session_context(expired_session);
    //common fields
    //format id, timestamp
    //Flow_id, Start timestamp, IP version, Server_Address, Client_Address, Server_Port, Client_Port, Transport Protocol ID,
    //Uplink Packet Count, Downlink Packet Count, Uplink Byte Count, Downlink Byte Count, TCP RTT, Retransmissions,
    //Application_Family, Content Class, Protocol_Path, Application_Name

    char path[128];
    //proto_hierarchy_to_str(&expired_session->proto_path, path);
    uint64_t session_id = get_session_id(expired_session);
    if (probe_context.thread_nb > 1) {
        session_id <<= probe_context.thread_nb_2_power;
        session_id |= iprobe->instance_id;
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

    proto_hierarchy_ids_to_str(get_session_protocol_hierarchy(expired_session), path);

    uint32_t rtt_ms = TIMEVAL_2_MSEC(get_session_rtt(expired_session));

    double loss_rate, loss_burstiness = 0, order_error = 0;
    loss_rate = (double) ((double) ((rtp_session_attr_t*) temp_session->app_data)->nb_lost / (((rtp_session_attr_t*) temp_session->app_data)->nb_lost + ((rtp_session_attr_t*) temp_session->app_data)->packets_nb + 1));
    if (((rtp_session_attr_t*) temp_session->app_data)->nb_loss_bursts) {
        loss_burstiness = (double) ((double) ((rtp_session_attr_t*) temp_session->app_data)->nb_lost / ((rtp_session_attr_t*) temp_session->app_data)->nb_loss_bursts);
    }
    order_error = (double) ((double) ((rtp_session_attr_t*) temp_session->app_data)->nb_order_error / (((rtp_session_attr_t*) temp_session->app_data)->packets_nb + 1));

    uint32_t app_class = PROTO_CLASS_STREAMING;
    if(get_session_content_flags(expired_session) & MMT_CONTENT_CONVERSATIONAL) {
        app_class = PROTO_CLASS_CONVERSATIONAL;
    }else if(get_session_ul_data_packet_count(expired_session) &&  get_session_dl_data_packet_count(expired_session)) {
        app_class = PROTO_CLASS_CONVERSATIONAL;
    }

    struct timeval init_time = get_session_init_time(expired_session);
    struct timeval end_time = get_session_last_activity_time(expired_session);
    const proto_hierarchy_t * proto_hierarchy = get_session_protocol_hierarchy(expired_session);

    snprintf(message, MAX_MESS, 
        "\"%u,%u,'%s',%lu.%lu,%"PRIu64",%lu.%lu,%u,'%s','%s',%hu,%hu,%hu,%"PRIu64",%"PRIu64",%"PRIu64",%"PRIu64",%u,%u,%u,%u,'%s',%u,%f,%f,%u,%f\"", // app specific 
        temp_session->app_format_id, probe_context.probe_id_number, probe_context.input_source, end_time.tv_sec, end_time.tv_usec,
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
        app_class,
        temp_session->contentclass, path, proto_hierarchy->proto_path[(proto_hierarchy->len <= 16)?(proto_hierarchy->len - 1):(16 - 1)],
        loss_rate,
        loss_burstiness,
        ((rtp_session_attr_t*) temp_session->app_data)->jitter, order_error
    );
    if(strlen(message)>MAX_MESS-1){
        mmt_log(&probe_context, MMT_L_WARNING, MMT_P_MEM_ERROR, "Memory error while creating message for rtp.");
        //fprintf(stderr, "Out of memory error when creating message for stats.\n");
    }
    send_message (out_file, "rtp.flow.report", message);
    /* 
    // Packet loss rate, Packet loss burstiness, max jitter, Order error rate 
    fprintf(out_file, "%hu,%lu.%lu,%"PRIu64",%lu.%lu,%u,%s,%s,%hu,%hu,%hu,%"PRIu64",%"PRIu64",%"PRIu64",%"PRIu64",%u,%u,%u,%u,%s,%u,%f,%f,%u,%f\n", // app specific 
        temp_session->app_format_id, end_time.tv_sec, end_time.tv_usec,
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
        //get_application_class_by_protocol_id(expired_session->proto_path.proto_path[expired_session->proto_path.len - 1]),
        //(expired_session->content_flags & MMT_CONTENT_CONVERSATIONAL) ? PROTO_CLASS_CONVERSATIONAL : PROTO_CLASS_STREAMING,
        app_class,
        temp_session->contentclass, path, proto_hierarchy->proto_path[(proto_hierarchy->len <= 16)?(proto_hierarchy->len - 1):(16 - 1)],
        loss_rate,
        loss_burstiness,
        ((rtp_session_attr_t*) temp_session->app_data)->jitter, order_error
    );
    */
}

void classification_expiry_session(const mmt_session_t * expired_session, void * args) {
    //fprintf(stdout, "Test from expiry session\n");
    session_struct_t * temp_session = get_user_session_context(expired_session);
    if (temp_session == NULL) {
        return;
    }
    probe_internal_t * iprobe = (probe_internal_t *) args;
    //FILE * out_file = (iprobe != NULL && iprobe->data_out != NULL) ? iprobe->data_out : stdout;
    //FILE * radius_out = (iprobe != NULL && iprobe->radius_out != NULL) ? iprobe->data_out : stdout;
    //FILE * out_file = stdout;
    FILE * out_file = (probe_context.data_out_file != NULL) ? probe_context.data_out_file : stdout;

    //printf("Temp session to delete %p\n", temp_session);
    int sslindex;

    if (is_microflow(expired_session)) {
        microsessions_stats_t * mf_stats = &iprobe->mf_stats[get_session_protocol_hierarchy(expired_session)->proto_path[(get_session_protocol_hierarchy(expired_session)->len <= 16)?(get_session_protocol_hierarchy(expired_session)->len - 1):(16 - 1)]];
        update_microflows_stats(mf_stats, expired_session);
        if (is_microflow_stats_reportable(mf_stats)) {
            report_microflows_stats(mf_stats, out_file);
        }
    } else {
        //First we check if we should skip the reporting for this flow
        if (temp_session->app_format_id != MMT_SKIP_APP_REPORT_FORMAT) {
            //We should report this flow.
            switch (temp_session->app_format_id) {
                case MMT_WEB_APP_REPORT_FORMAT:
                    print_web_app_format(expired_session, out_file, iprobe);
                    break;
                case MMT_SSL_APP_REPORT_FORMAT:
                    print_ssl_app_format(expired_session, out_file, iprobe);
                    break;
                case MMT_RTP_APP_REPORT_FORMAT:
                    print_rtp_app_format(expired_session, out_file, iprobe);
                    break;
                default:
                    sslindex = get_protocol_index_from_session(get_session_protocol_hierarchy(expired_session), PROTO_SSL);
                    if (sslindex != -1) print_ssl_app_format(expired_session, out_file, iprobe);
                    else print_default_app_format(expired_session, out_file, iprobe);
                    break;
            }
        }
    }

    if (temp_session->app_data != NULL) {
        //Free the application specific data
        free(temp_session->app_data);
    }
    free(temp_session);
}

mmt_dev_properties_t get_dev_properties_from_user_agent(char * user_agent, uint32_t len) {
    mmt_dev_properties_t retval = {0};
    if ((len > 8) && (mmt_strncasecmp(user_agent, "Mozilla/", 8) == 0)) {
        if ((len > 20) && (mmt_strncasecmp(&user_agent[12], "(iPhone;", 8) == 0)) {
            retval.os_id = OS_IOS;
            retval.dev_id = DEV_IPHONE;
        } else if ((len > 18) && (mmt_strncasecmp(&user_agent[12], "(iPod;", 6) == 0)) {
            retval.os_id = OS_IOS;
            retval.dev_id = DEV_IPOD;
        } else if ((len > 18) && (mmt_strncasecmp(&user_agent[12], "(iPad;", 6) == 0)) {
            retval.os_id = OS_IOS;
            retval.dev_id = DEV_IPAD;
        } else if ((len > 30) && (mmt_strncasecmp(&user_agent[12], "(Linux; U; Android", 18) == 0)) {
            retval.os_id = OS_AND;
            retval.dev_id = DEV_MOB;
        } else if ((len > 20) && (mmt_strncasecmp(&user_agent[12], "(Android", 8) == 0)) {
            retval.os_id = OS_AND;
            retval.dev_id = DEV_MOB;
        } else if ((len > 24) && (mmt_strncasecmp(&user_agent[12], "(BlackBerry;", 12) == 0)) {
            retval.os_id = OS_BLB;
            retval.dev_id = DEV_BLB;
        } else if ((len > 17) && (mmt_strncasecmp(&user_agent[12], "(X11;", 5) == 0)) {
            retval.os_id = OS_NUX;
            retval.dev_id = DEV_PC;
        } else if ((len > 23) && (mmt_strncasecmp(&user_agent[12], "(Macintosh;", 11) == 0)) {
            retval.os_id = OS_MAC;
            retval.dev_id = DEV_MAC;
        } else if ((len > 29) && (mmt_strncasecmp(&user_agent[12], "(Windows; U; MSIE", 17) == 0)) {
            retval.os_id = OS_WIN;
            retval.dev_id = DEV_PC;
        } else if ((len > 23) && (mmt_strncasecmp(&user_agent[12], "(Windows NT", 11) == 0)) {
            retval.os_id = OS_WIN;
            retval.dev_id = DEV_PC;
        } else if ((len > 35) && (mmt_strncasecmp(&user_agent[12], "(Windows; U; Windows NT", 23) == 0)) {
            retval.os_id = OS_WIN;
            retval.dev_id = DEV_PC;
        } else if ((len > 36) && (mmt_strncasecmp(&user_agent[12], "(compatible; Windows; U;", 24) == 0)) {
            retval.os_id = OS_WIN;
            retval.dev_id = DEV_PC;
        } else if ((len > 46) && (mmt_strncasecmp(&user_agent[12], "(compatible; MSIE 10.0; Macintosh;", 34) == 0)) {
            retval.os_id = OS_MAC;
            retval.dev_id = DEV_MAC;
        } else if ((len > 48) && (mmt_strncasecmp(&user_agent[12], "(compatible; MSIE 9.0; Windows Phone", 36) == 0)) {
            retval.os_id = OS_WPN;
            retval.dev_id = DEV_MOB;
        } else if ((len > 56) && (mmt_strncasecmp(&user_agent[12], "(compatible; MSIE 10.0; Windows NT 6.2; ARM;", 44) == 0)) {
            retval.os_id = OS_WPN;
            retval.dev_id = DEV_MOB;
        } else if ((len > 29) && (mmt_strncasecmp(&user_agent[12], "(compatible; MSIE", 17) == 0)) {
            retval.os_id = OS_WIN;
            retval.dev_id = DEV_PC;
        }
    } else if ((len > 6) && (mmt_strncasecmp(user_agent, "Opera/", 6) == 0)) {
        if ((len > 19) && (mmt_strncasecmp(&user_agent[11], "(Windows", 8) == 0)) {
            retval.os_id = OS_WIN;
            retval.dev_id = DEV_PC;
        } else if ((len > 22) && (mmt_strncasecmp(&user_agent[11], "(Macintosh;", 11) == 0)) {
            retval.os_id = OS_MAC;
            retval.dev_id = DEV_MAC;
        } else if ((len > 16) && (mmt_strncasecmp(&user_agent[11], "(X11;", 5) == 0)) {
            retval.os_id = OS_NUX;
            retval.dev_id = DEV_PC;
        }
    }
    return retval;
}