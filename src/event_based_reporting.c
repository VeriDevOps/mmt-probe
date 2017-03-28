#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <errno.h>

#define __STDC_FORMAT_MACROS
#include <inttypes.h>

#include "mmt_core.h"
#include "processing.h"
#include <pthread.h>
#include "tcpip/mmt_tcpip.h"

/* This function is for reporting event report * */
void event_report_handle(const ipacket_t * ipacket, attribute_t * attribute, void * user_args) {
	int j;
	attribute_t * attr_extract;
	int offset = 0, valid;
	char message[MAX_MESS + 1];
	struct user_data *p   = (struct user_data *) user_args;
	struct smp_thread *th = (struct smp_thread *) p->smp_thread;
	mmt_probe_context_t * probe_context = get_probe_context_config();
	mmt_event_report_t * event_report   = p->event_reports; //(mmt_event_report_t *) user_args;

	valid= snprintf(message, MAX_MESS,
			"%u,%u,\"%s\",%lu.%lu",
			event_report->id, probe_context->probe_id_number, probe_context->input_source, ipacket->p_hdr->ts.tv_sec,ipacket->p_hdr->ts.tv_usec);
	if(valid > 0) {
		offset += valid;
	}else {
		return;
	}
	message[offset] = ',';

	valid = mmt_attr_sprintf(&message[offset+1], MAX_MESS - offset+1, attribute);

	if(valid > 0) {
		offset += valid+1;
	}else {
		return;
	}
	for(j = 0; j < event_report->attributes_nb; j++) {
		mmt_event_attribute_t * event_attribute = &event_report->attributes[j];
		attr_extract = get_extracted_attribute_by_name(ipacket,event_attribute->proto, event_attribute->attribute);
		message[offset] = ',';
		if(attr_extract != NULL) {
			valid = mmt_attr_sprintf(&message[offset + 1], MAX_MESS - offset+1, attr_extract);
			if(valid > 0) {
				offset += valid + 1;
			}else {

				return;
			}
		}else {

			offset += 1;
		}
	}
	message[ offset ] = '\0';
	//send_message_to_file ("event.report", message);
	if (probe_context->output_to_file_enable == 1) send_message_to_file_thread (message, th);
	if (probe_context->redis_enable == 1) send_message_to_redis ("event.report", message);

}
/* This function registers attributes and handlers for event report.
 * Returns 0 if unsuccessful
 * */
int register_event_report_handle(void * args) {
	int i = 1, j;
	mmt_probe_context_t * probe_context = get_probe_context_config();

	struct smp_thread *th ;
	struct user_data *p = ( struct user_data *) args;
	th = p->smp_thread;
	mmt_event_report_t * event_report = p->event_reports;

	i = event_report->event.proto_id = get_protocol_id_by_name (event_report->event.proto);
	if (i == 0) return i;

	i = event_report->event.attribute_id = get_attribute_id_by_protocol_and_attribute_names(event_report->event.proto, event_report->event.attribute);
	if (i == 0) return i;

	if (is_registered_attribute_handler(th->mmt_handler, event_report->event.proto_id, event_report->event.attribute_id, event_report_handle) == 0){
		i = register_attribute_handler(th->mmt_handler, event_report->event.proto_id, event_report->event.attribute_id, event_report_handle, NULL, (void *) p);
		if (i == 0) return i;
	}

	for(j = 0; j < event_report->attributes_nb; j++) {
		mmt_event_attribute_t * event_attribute = &event_report->attributes[j];
		i = event_attribute->proto_id = get_protocol_id_by_name (event_attribute->proto);
		if (i == 0) return i;


		i = event_attribute->attribute_id = get_attribute_id_by_protocol_and_attribute_names(event_attribute->proto, event_attribute->attribute);
		if (i == 0) return i;

		if (is_registered_attribute(th->mmt_handler, event_attribute->proto_id, event_attribute->attribute_id) == 0){
			i = register_extraction_attribute(th->mmt_handler, event_attribute->proto_id, event_attribute->attribute_id);
			if (i == 0) return i;
		}
	}
	return i;
}

/* This function registers extraction attribute for multisession reports.
 * Returns 0 if unsuccessful
 * */
int register_security_report_multisession_handle(void * args) {
	int i = 0, j = 0, k = 1;
	mmt_probe_context_t * probe_context = get_probe_context_config();
	struct smp_thread *th = (struct smp_thread *) args;

	for(i = 0; i < probe_context->security_reports_multisession_nb; i++) {
		if (probe_context->security_reports_multisession[i].enable == 1){

			for(j = 0; j < probe_context->security_reports_multisession[i].attributes_nb; j++) {
				mmt_security_attribute_t * security_attribute_multisession = &probe_context->security_reports_multisession[i].attributes[j];

				k = security_attribute_multisession->proto_id = get_protocol_id_by_name (security_attribute_multisession->proto);
				if (k == 0) return k;

				k = security_attribute_multisession->attribute_id = get_attribute_id_by_protocol_and_attribute_names(security_attribute_multisession->proto, security_attribute_multisession->attribute);
				if (k == 0) return k;

				if (is_registered_attribute(th->mmt_handler, security_attribute_multisession->proto_id, security_attribute_multisession->attribute_id) == 0){
					k = register_extraction_attribute(th->mmt_handler, security_attribute_multisession->proto_id, security_attribute_multisession->attribute_id);
					if (k == 0) return k;
					//printf("proto_id = %u, attribute_id = %u \n",security_attribute_multisession->proto_id, security_attribute_multisession->attribute_id);
				}
			}
		}
	}
	return k;
}
/* This function initialize multisession reports.  * */
void security_reports_multisession_init(void * args) {

	mmt_probe_context_t * probe_context = get_probe_context_config();

	struct smp_thread *th = (struct smp_thread *) args;


	if(register_security_report_multisession_handle((void *) th) == 0) {
		fprintf(stderr, "Error while initializing security report !\n");
	}
/*
	if (probe_context->socket_enable == 1 && th->socket_active == 0){
		create_socket(probe_context, args);
		th->socket_active = 1;
	}
*/
	if (is_registered_packet_handler(th->mmt_handler,6) == 1)unregister_packet_handler(th->mmt_handler, 6);
	register_packet_handler(th->mmt_handler, 6, packet_handler, (void *) th);
}

/* This function registers extraction attribute for security report.
 * Returns 0 if unsuccessful
 * */
int register_security_report_handle(void * args) {
	int i = 0, j = 0, k = 1, l = 0;
	mmt_probe_context_t * probe_context = get_probe_context_config();
	struct smp_thread *th = (struct smp_thread *) args;

	for(i = 0; i < probe_context->security_reports_nb; i++) {
		if (probe_context->security_reports[i].enable == 1){
			th->report[i].data = malloc (sizeof (unsigned char *) * probe_context->nb_of_report_per_msg +1);

			if (th->report[i].data == NULL){
				printf ("Error: Memory allocation of data in register_security_report_handle \n");
				exit(1);
			}
			th->report[i].msg = malloc (sizeof (struct iovec) * probe_context->nb_of_report_per_msg +1);

			if (th->report[i].msg == NULL){
				printf ("Error: Memory allocation of msg in register_security_report_handle \n");
				exit(1);
			}
			for (l=0; l < probe_context->nb_of_report_per_msg; l++)th->report[i].data[l]= malloc(10000);

			for(j = 0; j < probe_context->security_reports[i].attributes_nb; j++) {
				mmt_security_attribute_t * security_attribute = &probe_context->security_reports[i].attributes[j];

				k = security_attribute->proto_id = get_protocol_id_by_name (security_attribute->proto);
				if (k == 0) return k;

				k = security_attribute->attribute_id = get_attribute_id_by_protocol_and_attribute_names(security_attribute->proto, security_attribute->attribute);
				if (k == 0) return k;

				if (is_registered_attribute(th->mmt_handler, security_attribute->proto_id, security_attribute->attribute_id) == 0){
					k = register_extraction_attribute(th->mmt_handler, security_attribute->proto_id, security_attribute->attribute_id);
					if (k == 0) return k;
				}
			}
		}
	}
	return k;
}

/* This function initialize security report.
 * */
void security_reports_init(void * args) {

	mmt_probe_context_t * probe_context = get_probe_context_config();

	struct smp_thread *th = (struct smp_thread *) args;
	th->report = calloc(sizeof(security_report_buffer_t), probe_context->security_reports_nb);
	if (th->report == NULL){
		printf ("Error: Memory allocation of report in security_reports_init \n");
		exit(1);
	}

	if(register_security_report_handle((void *) th) == 0) {
		fprintf(stderr, "Error while initializing security report !\n");
		exit(1);
	}
	if (probe_context->socket_enable == 1 && th->socket_active == 0){
		create_socket(probe_context, args);
		th->socket_active = 1;
	}
	if (is_registered_packet_handler(th->mmt_handler,6) == 1)unregister_packet_handler(th->mmt_handler, 6);
	register_packet_handler(th->mmt_handler, 6, packet_handler, (void *) th);
}

/* This function initialize event report.
 * */
void event_reports_init(void * args) {
	int i;
	mmt_probe_context_t * probe_context = get_probe_context_config();
	struct smp_thread *th = (struct smp_thread *) args;
	struct user_data *p;
	for(i = 0; i < probe_context->event_reports_nb; i++) {
		th->event_reports = &probe_context->event_reports[i];
		if(th->event_reports->enable == 1){
			p = malloc( sizeof( struct user_data ));
			if (p == NULL){
				printf ("Error: Memory allocation of user_data in event_reports_init \n");
				exit(1);
			}
			p->smp_thread    = th;
			p->event_reports = th->event_reports;
			if(register_event_report_handle((void *) p) == 0) {
				fprintf(stderr, "Error while initializing event report number %i!\n", th->event_reports->id);
				exit(1);
			}

		}
	}
}

