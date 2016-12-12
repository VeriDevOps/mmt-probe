/*
 * File:   main.c

gcc -gdwarf-2 -o probe src/smp_main.c  src/processing.c src/web_session_report.c src/thredis.c \
src/send_msg_to_file.c src/send_msg_to_redis.c src/ip_statics.c src/rtp_session_report.c src/ftp_session_report.c \
src/event_based_reporting.c src/protocols_report.c src/ssl_session_report.c src/default_app_session_report.c \
src/microflows_session_report.c src/radius_reporting.c src/security_analysis.c src/parseoptions.c src/license.c \
-lmmt_core -lmmt_tcpip -lmmt_security -lxml2 -ldl -lpcap -lconfuse -lhiredis -lpthread


 * Author: montimage
 *
 * Created on 31 mai 2011, 14:09
 */


//TODO:
//Debug MMT_Security for multi-threads


#ifdef linux
#include <syscall.h>
#endif
#include <pcap.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <time.h>
#include <pthread.h>
#include <errno.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <unistd.h> //usleep, sleep
#include "mmt_core.h"
#include "processing.h"
#include "lib/packet_hash.h"
#include "lib/data_spsc_ring.h"
#include "lib/optimization.h"
#include "lib/system_info.h"
#include <netinet/tcp.h>

#define __STDC_FORMAT_MACROS
#include <inttypes.h>


#include <sys/types.h>
//#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#define DLT_EN10MB 1    /* Ethernet (10Mb) */
#define READ_PRIO	-15	/* niceness value for Reader thread */
//#define SNAP_LEN 65535	/* apparently what tcpdump uses for -s 0 */
#define READER_CPU	0	/* assign Reader thread to this CPU */
#define MAX_FILE_NAME 500
//static int okcode  = EXIT_SUCCESS;
static int errcode = EXIT_FAILURE;

pcap_t *handle = 0; /* packet capture handle */
struct pcap_stat pcs; /* packet capture filter stats */
int got_stats = 0; /* capture stats have been obtained */
int push, stop; /* flags for inter-thread communication */
//int d_snap_len = SNAP_LEN; /* actual limit on packet capture size */
//int snap_len = SNAP_LEN; /* requested limit on packet capture size */
int volatile reader_ready = 0; /* reader thread no longer needs root */
char *filter_exp = ""; /* decapsulation filter expression */
int captured = 0; /* number of packets captured for stats */
int ignored = 0; /* number of packets !decapsulated for stats */

uint64_t nb_packets_dropped_by_mmt = 0;
uint64_t nb_packets_processed_by_mmt = 0;

static void terminate_probe_processing(int wait_thread_terminate);


uint32_t get_2_power(uint32_t nb) {
	uint32_t ret = -1;
	while (nb != 0) {
		nb >>= 1;
		ret++;
	}
	return ret;
}

void error(const char *msg)
{
	perror(msg);
	exit(1);
}

static void *smp_thread_routine(void *arg) {
	struct timeval tv;
	struct smp_thread *th = (struct smp_thread *) arg;
	char mmt_errbuf[1024];
	char lg_msg[256];
	int  i = 0;
	struct packet_element *pkt;
	uint32_t tail;
	long avail_processors;
	int size;


	sprintf(lg_msg, "Starting thread %i", th->thread_number);

	mmt_log(mmt_probe.mmt_conf, MMT_L_INFO, MMT_T_INIT, lg_msg);

	//move this thread to a specific processor
	avail_processors = get_number_of_online_processors();

	if( avail_processors > 1 ){
		avail_processors -= 1;//avoid zero that is using by Reader
		(void) move_the_current_thread_to_a_core( th->thread_number % avail_processors + 1, -10 );
	}
	//printf ("core =%ld, th_id =%u\n",th->thread_number % avail_processors + 1,th->thread_number);

	//Initialize an MMT handler
	pthread_spin_lock(&spin_lock);
	//pthread_mutex_lock(&mutex_lock);
	th->mmt_handler = mmt_init_handler(DLT_EN10MB, 0, mmt_errbuf);
	//pthread_mutex_unlock(&mutex_lock);
	pthread_spin_unlock(&spin_lock);

	if (!th->mmt_handler) { /* pcap error ? */
		sprintf(lg_msg, "Error while starting thread number %i", th->thread_number);
		mmt_log(mmt_probe.mmt_conf, MMT_L_INFO, MMT_T_INIT, lg_msg);
		return &errcode;
	}

	//th->iprobe.data_out = NULL;
	//th->iprobe.radius_out = NULL;
	for (i = 0; i < PROTO_MAX_IDENTIFIER; i++) {
		reset_microflows_stats(&th->iprobe.mf_stats[i]);
		th->iprobe.mf_stats[i].application = get_protocol_name_by_id(i);
		th->iprobe.mf_stats[i].application_id = i;
	}
	th->iprobe.instance_id = th->thread_number;
	// customized packet and session handling functions are then registered

	if(mmt_probe.mmt_conf->enable_flow_stats) {
		if (mmt_probe.mmt_conf->enable_session_report == 1)register_session_timer_handler(th->mmt_handler, print_ip_session_report, th);
		if (mmt_probe.mmt_conf->enable_session_report == 1)register_session_timeout_handler(th->mmt_handler, classification_expiry_session, th);
		if (mmt_probe.mmt_conf->enable_session_report == 1)flowstruct_init(th); // initialize our event handler
		if (mmt_probe.mmt_conf->event_based_reporting_enable == 1)event_reports_init(th); // initialize our event reports
		if (mmt_probe.mmt_conf->condition_based_reporting_enable == 1)conditional_reports_init(th);// initialize our condition reports
		if (mmt_probe.mmt_conf->radius_enable == 1)radius_ext_init(th); // initialize radius extraction and attribute event handler
		set_default_session_timed_out(th->mmt_handler, mmt_probe.mmt_conf->default_session_timeout);
		set_long_session_timed_out(th->mmt_handler, mmt_probe.mmt_conf->long_session_timeout);
		set_short_session_timed_out(th->mmt_handler, mmt_probe.mmt_conf->short_session_timeout);
		set_live_session_timed_out(th->mmt_handler, mmt_probe.mmt_conf->live_session_timeout);
	}

	if (mmt_probe.mmt_conf->enable_security_report == 0)proto_stats_init(th);//initialise this before security_reports_init
	if (mmt_probe.mmt_conf->enable_security_report == 1)security_reports_init(th);


	th->nb_packets = 0;
	mmt_probe_context_t * probe_context = get_probe_context_config();
	char message[MAX_MESS + 1];
	FILE * register_attributes;

	data_spsc_ring_t *fifo     = &th->fifo;
	mmt_handler_t *mmt_handler = th->mmt_handler;

	while ( 1 ) {

		if(time(NULL)- th->last_stat_report_time >= mmt_probe.mmt_conf->stats_reporting_period ||
				th->pcap_current_packet_time - th->pcap_last_stat_report_time >= mmt_probe.mmt_conf->stats_reporting_period){
			th->report_counter++;
			th->last_stat_report_time = time(NULL);
			th->pcap_last_stat_report_time = th->pcap_current_packet_time;
			if (probe_context->enable_session_report == 1)process_session_timer_handler(th->mmt_handler);
			if (probe_context->enable_proto_without_session_stats == 1)iterate_through_protocols(protocols_stats_iterator, th);

			//Each thread need to call these function one by one to register the attributes
			//Need a handler, problem when flushing after all handlers are closed
			if (th->file_read_flag == 1){
				new_conditional_reports_init(arg);
				new_event_reports_init(arg);
				printf("Added new attributes_th_id= %u\n", th->thread_number);
				th->file_read_flag = 0;
			}

		}

			//get number of packets being available
			size = data_spsc_ring_pop_bulk( fifo, &tail );
			/* if no packet has arrived sleep 1 milli-second */
			if ( size <= 0 ) {
				tv.tv_sec = 0;
				tv.tv_usec = 1000;
				//fprintf(stdout, "No more packets for thread %i --- waiting\n", th->thread_number);
				select(0, NULL, NULL, NULL, &tv);
				//nanosleep( (const struct timespec[]){{0, 1000000L}}, NULL );
			} else { /* else remove number of packets from list and process it */

				//the last packet will be verified after (not in for)
				size --;
			for( i=0; i<size; i++ ){
				pkt = (struct packet_element *) data_spsc_ring_get_data( fifo, i + tail);
				packet_process( mmt_handler, &pkt->header, pkt->data );
			}
			th->nb_packets += size;

			//only the last packet in the queue may has NULL data
			pkt = (struct packet_element *) data_spsc_ring_get_data( fifo, size + tail);

			/* is it a dummy packet ? => means thread must exit */
			if( unlikely( pkt->data == NULL ))
				break;
			else{
				packet_process( mmt_handler, &pkt->header, pkt->data );
				th->nb_packets ++;
			}

			//update new position of ring's tail
			data_spsc_ring_update_tail( fifo, tail, size + 1); //+1 as size-- above
		}
	}

	printf("thread %d : %"PRIu64" \n", th->thread_number, th->nb_packets );

	if(th->mmt_handler != NULL){
		radius_ext_cleanup(th->mmt_handler); // cleanup our event handler for RADIUS initializations
		flowstruct_cleanup(th->mmt_handler); // cleanup our event handler
		th->report_counter++;
		if (mmt_probe.mmt_conf->enable_proto_without_session_stats == 1)iterate_through_protocols(protocols_stats_iterator, th);
		//process_session_timer_handler(th->mmt_handler);
		mmt_close_handler(th->mmt_handler);
		th->mmt_handler = NULL;
	}

	sprintf(lg_msg, "Thread %i ended (%"PRIu64" packets)", th->thread_number, th->nb_packets);
	//printf("Thread %i ended (%"PRIu64" packets)\n", th->thread_number, nb_pkts);
	mmt_log(mmt_probe.mmt_conf, MMT_L_INFO, MMT_T_END, lg_msg);
	//fprintf(stdout, "Thread %i ended (%u packets)\n", th->thread_number, nb_pkts);
	return NULL;
}

struct dispatcher_struct {
	char * filename;
	pthread_t handle;
	int nb;
};

void process_trace_file(char * filename, mmt_probe_struct_t * mmt_probe) {
	int i;
	struct dispatcher_struct dispatcher[2];

	uint64_t packets_count = 0;
	pcap_t *pcap;
	const u_char *data;
	struct pkthdr header;
	struct pcap_pkthdr pkthdr;
	char errbuf[1024];
	char lg_msg[1024];
	struct smp_thread *th;
	static uint32_t p_hash = 0;
	static struct packet_element *pkt;
	static void *pdata;

	//Initialise MMT_Security
	if(mmt_probe->mmt_conf->security_enable==1)
		init_mmt_security(mmt_probe->smp_threads->mmt_handler, mmt_probe->mmt_conf->properties_file,(void *)mmt_probe->smp_threads );
	//End initialise MMT_Security

	if (mmt_probe->mmt_conf->thread_nb == 1) {

		pcap = pcap_open_offline(filename, errbuf); // open offline trace

		if (!pcap) { /* pcap error ? */
			sprintf(lg_msg, "Error while opening pcap file: %s --- error msg: %s", filename, errbuf);
			printf("Error: Verify the name and the location of the trace file to be analysed \n ");
			mmt_log(mmt_probe->mmt_conf, MMT_L_ERROR, MMT_P_TRACE_ERROR, lg_msg);
			return;
		}
		sprintf(lg_msg, "Start processing trace file: %s", filename);
		mmt_log(mmt_probe->mmt_conf, MMT_L_INFO, MMT_P_START_PROCESS_TRACE, lg_msg);
		//One thread for reading packets and processing them
		while ((data = pcap_next(pcap, &pkthdr))) {
			header.ts = pkthdr.ts;
			header.caplen = pkthdr.caplen;
			header.len = pkthdr.len;
			header.user_args = NULL;

			if(time(NULL)- mmt_probe->smp_threads->last_stat_report_time >= mmt_probe->mmt_conf->stats_reporting_period ||
					mmt_probe->smp_threads->pcap_current_packet_time - mmt_probe->smp_threads->pcap_last_stat_report_time >= mmt_probe->mmt_conf->stats_reporting_period){
				mmt_probe->smp_threads->report_counter++;
				mmt_probe->smp_threads->last_stat_report_time = time(NULL);
				mmt_probe->smp_threads->pcap_last_stat_report_time = mmt_probe->smp_threads->pcap_current_packet_time;
				if (mmt_probe->mmt_conf->enable_session_report == 1)process_session_timer_handler(mmt_probe->smp_threads->mmt_handler);
				if (mmt_probe->mmt_conf->enable_proto_without_session_stats == 1 )iterate_through_protocols(protocols_stats_iterator, (void *)mmt_probe->smp_threads);

				//Each thread need to call these function one by one to register the attributes
				//Need a handler, problem when flushing after all handlers are closed
				if (mmt_probe->smp_threads->file_read_flag == 1){
					new_conditional_reports_init((void *)mmt_probe->smp_threads);
					new_event_reports_init((void *)mmt_probe->smp_threads);
					printf("Added new attributes_th_id = %u\n", mmt_probe->smp_threads->thread_number);
					mmt_probe->smp_threads->file_read_flag = 0;
				}

			}


			//Call mmt_core function that will parse the packet and analyse it.

			if (!packet_process(mmt_probe->smp_threads->mmt_handler, &header, data)) {
				sprintf(lg_msg, "MMT Extraction failure! Error while processing packet number %"PRIu64"", packets_count);
				mmt_log(mmt_probe->mmt_conf, MMT_L_ERROR, MMT_E_PROCESS_ERROR, lg_msg);
			}
			packets_count++;
		}
		pcap_close(pcap);
		sprintf(lg_msg, "End processing trace file: %s", filename);
		mmt_log(mmt_probe->mmt_conf, MMT_L_INFO, MMT_P_END_PROCESS_TRACE, lg_msg);
	}else {//We have more than one thread for processing packets! dispatch the packet to one of them
		pcap = pcap_open_offline(filename, errbuf); // open offline trace

		if (!pcap) { /* pcap error ? */
			sprintf(lg_msg, "Error while opening pcap file: %s --- error msg: %s", filename, errbuf);
			printf("Error: Verify the name and the location of the trace file to be analysed \n ");
			mmt_log(mmt_probe->mmt_conf, MMT_L_ERROR, MMT_P_TRACE_ERROR, lg_msg);
			return;
		}

		sprintf(lg_msg, "Start processing trace file: %s", filename);
		mmt_log(mmt_probe->mmt_conf, MMT_L_INFO, MMT_P_START_PROCESS_TRACE, lg_msg);
		int is_queue_full = 0;
		while (1) {

			//if( ! is_queue_full )
			data = pcap_next(pcap, &pkthdr);
			if( ! data ){
				//printf("break read pcap");
				fflush( stdout );
				for( i=0; i<mmt_probe->mmt_conf->thread_nb; i++){
					th = &mmt_probe->smp_threads[i];
					if( data_spsc_ring_get_tmp_element( &th->fifo, &pdata ) != QUEUE_SUCCESS)
						continue;

					pkt = (struct packet_element *) pdata;
					/* fill smp_pkt fields and copy packet data from pcap buffer */
					pkt->header.len    = pkthdr.len;
					pkt->header.caplen = pkthdr.caplen;
					pkt->header.ts     = pkthdr.ts;
					pkt->data          = NULL; //put data in the same memory segment but after sizeof( pkt )
					data_spsc_ring_push_tmp_element( &th->fifo );
				}

				//printf("I lost %"PRIu64" but I processed %"PRIu64"", nb_packets_dropped_by_mmt, nb_packets_processed_by_mmt);
				break;
			}
			p_hash = get_packet_hash_number(data, pkthdr.caplen) % (mmt_probe->mmt_conf->thread_nb );
			th     = &mmt_probe->smp_threads[ p_hash ];

			data_spsc_ring_get_tmp_element( &th->fifo, &pdata );

			pkt = (struct packet_element *) pdata;
			/* fill smp_pkt fields and copy packet data from pcap buffer */
			pkt->header.len    = pkthdr.len;
			pkt->header.caplen = pkthdr.caplen;
			pkt->header.ts     = pkthdr.ts;
			pkt->data          = (u_char *)( &pkt[ 1 ]); //put data in the same memory segment but after sizeof( pkt )
			memcpy(pkt->data, data, pkthdr.caplen);
			while (  data_spsc_ring_push_tmp_element( &th->fifo ) != QUEUE_SUCCESS ){
				usleep(10);
				//nb_packets_dropped_by_mmt ++;
				//th->nb_dropped_packets ++;

			}
		}
		pcap_close(pcap);
	}
}

//BW: TODO: add the pcap handler to the mmt_probe (or internal structure accessible from it) in order to be able to close it here
void cleanup(int signo) {
	mmt_probe_context_t * mmt_conf = mmt_probe.mmt_conf;
	int i;

	pcap_breakloop(handle);
	stop = 1;
	if (got_stats) return;

	if (pcap_stats(handle, &pcs) < 0) {
		(void) fprintf(stderr, "pcap_stats: %s\n", pcap_geterr(handle));
	} else got_stats = 1;

	if (ignored > 0) {
		fprintf(stderr, "%d packets ignored (too small to decapsulate)\n", ignored);
	}
	if (got_stats) {
		(void) fprintf(stderr, "\n%12d packets received by filter\n", pcs.ps_recv);
		(void) fprintf(stderr, "%12d packets dropped by NIC (%3.2f%%)\n", pcs.ps_ifdrop, pcs.ps_ifdrop * 100.0 / pcs.ps_recv);
		(void) fprintf(stderr, "%12d packets dropped by kernel (%3.2f%%)\n", pcs.ps_drop, pcs.ps_drop * 100.0 / pcs.ps_recv);
		(void) fprintf(stderr, "%12"PRIu64" packets dropped by MMT (%3.2f%%) \n", nb_packets_dropped_by_mmt, nb_packets_dropped_by_mmt * 100.0 /  pcs.ps_recv );
		fflush(stderr);
	}
	if( mmt_conf->thread_nb == 1)
		(void) fprintf(stderr, "%12"PRIu64" packets processed by MMT (%3.2f%%) \n", nb_packets_processed_by_mmt, nb_packets_processed_by_mmt * 100.0 /  pcs.ps_recv );
	else
		for (i = 0; i < mmt_conf->thread_nb; i++)
			(void) fprintf( stderr, "- thread %2d processed %12"PRIu64" packets, dropped %12"PRIu64"\n",
					mmt_probe.smp_threads[i].thread_number, mmt_probe.smp_threads[i].nb_packets, mmt_probe.smp_threads[i].nb_dropped_packets );
	fflush(stderr);
#ifdef RHEL3
	pcap_close(handle);
#endif /* RHEL3 */
}

//online-single thread
void got_packet_single_thread(u_char *args, const struct pcap_pkthdr *pkthdr, const u_char *data) {
	struct smp_thread *th;
	struct pkthdr header;
	static uint32_t p_hash = 0;
	static void *pdata;
	static struct packet_element *pkt;


	header.ts = pkthdr->ts;
	header.caplen = pkthdr->caplen;
	header.len = pkthdr->len;
	header.user_args = NULL;

	if(time(NULL)- mmt_probe.smp_threads->last_stat_report_time >= mmt_probe.mmt_conf->stats_reporting_period){
		mmt_probe.smp_threads->report_counter++;
		mmt_probe.smp_threads->last_stat_report_time = time(NULL);
		if (mmt_probe.mmt_conf->enable_session_report == 1)process_session_timer_handler(mmt_probe.smp_threads->mmt_handler);
		if (mmt_probe.mmt_conf->enable_proto_without_session_stats == 1)iterate_through_protocols(protocols_stats_iterator, (void *)mmt_probe.smp_threads);

		if (mmt_probe.smp_threads->file_read_flag == 1){
			new_conditional_reports_init((void *)mmt_probe.smp_threads);
			new_event_reports_init((void *)mmt_probe.smp_threads);
			printf("Added new attributes_th_id = %u\n",mmt_probe.smp_threads->thread_number);
			mmt_probe.smp_threads->file_read_flag = 0;
		}

	}

	if (!packet_process(mmt_probe.smp_threads->mmt_handler, &header, data)) {
		fprintf(stderr, "MMT Extraction failure! Error while processing packet number %"PRIu64"", nb_packets_processed_by_mmt);
		nb_packets_dropped_by_mmt ++;
	}
	nb_packets_processed_by_mmt ++;
}

//static inline void __attribute__((always_inline))
//mmt_memcpy(void* dest, const void* src, const size_t size){
//	char *d = (char *)dest, *s = (char *)src;
//	size_t i=0;
//	do{
//		d = s;
//		d++;
//		s++;
//		i++;
//	}while( i< size );
//}

//We have more than one thread for processing packets! dispatch the packet to one of them
void got_packet_multi_thread(u_char *args, const struct pcap_pkthdr *pkthdr, const u_char *data) {
	struct smp_thread *th;
	uint32_t p_hash = 0;
	void *pdata;
	struct packet_element *pkt;

	p_hash = get_packet_hash_number(data, pkthdr->caplen) % ( mmt_probe.mmt_conf->thread_nb );

	//p_hash = rand() %  (mmt_probe.mmt_conf->thread_nb );
	th     = &mmt_probe.smp_threads[ p_hash ];

	data_spsc_ring_get_tmp_element( &th->fifo, &pdata );

	pkt = (struct packet_element *) pdata;
	/* fill smp_pkt fields and copy packet data from pcap buffer */
	pkt->header.len    = pkthdr->len;
	pkt->header.caplen = pkthdr->caplen;
	pkt->header.ts     = pkthdr->ts;
	pkt->data          = (u_char *)( &pkt[ 1 ]); //put data in the same memory segment but after sizeof( pkt )
	memcpy(pkt->data, data, pkthdr->caplen);
//	mmt_memcpy( pkt->data, data, pkthdr->caplen );
//	pkt->data = data;

	if(  unlikely( data_spsc_ring_push_tmp_element( &th->fifo ) != QUEUE_SUCCESS ))
	{
		//queue is full
		nb_packets_dropped_by_mmt ++;
		th->nb_dropped_packets ++;
	}
}

/*
 * This thread reads stdin or the network and appends to the ring buffer
 */
void *Reader(void *arg) {
	struct mmt_probe_struct * mmt_probe = (struct mmt_probe_struct *) arg;

	char errbuf[PCAP_ERRBUF_SIZE]; /* error buffer */
	struct bpf_program fp; /* compiled filter program */
	bpf_u_int32 mask; /* subnet mask */
	bpf_u_int32 net; /* ip */
	int num_packets = -1; /* number of packets to capture */
	//int num_packets = 1000000; /* number of packets to capture */

	(void) move_the_current_thread_to_a_core(0, -15);

	/*
	 * get network number and mask associated with capture device
	 * (needed to compile a bpf expression).
	 */
	if (pcap_lookupnet(mmt_probe->mmt_conf->input_source, &net, &mask, errbuf) == -1) {
		fprintf(stderr, "Couldn't get netmask for dev %s: %s\n", mmt_probe->mmt_conf->input_source, errbuf);
		net = 0;
		mask = 0;
	}

	//HUU TODO: need to fix MMT_Security using multithreads
	//Initialise MMT_Security
	//init_sec_lib (mmt_probe->mmt_handler, mmt_probe->mmt_conf->properties_file, OPTION_SATISFIED, OPTION_NOT_SATISFIED, todo_when_property_is_satisfied_or_not,
	//              db_todo_at_start, db_todo_when_property_is_satisfied_or_not);
	//End initialise MMT_Security

	/* open capture device */
	handle = pcap_create(mmt_probe->mmt_conf->input_source, errbuf);
	if (handle == NULL) {
		fprintf(stderr, "Couldn't open device %s\n", errbuf);
		exit(0);
	}
	pcap_set_snaplen(handle, mmt_probe->mmt_conf->requested_snap_len);
	pcap_set_promisc(handle, 1);
	pcap_set_timeout(handle, 0);
	pcap_set_buffer_size(handle, 500*1000*1000);
	pcap_activate(handle);

	reader_ready = 1;

	/* make sure we're capturing on an Ethernet device */
	if (pcap_datalink(handle) != DLT_EN10MB) {
		fprintf(stderr, "%s is not an Ethernet\n", mmt_probe->mmt_conf->input_source);
		exit(0);
	}

	/* compile the filter expression */
	//if (pcap_compile(handle, &fp, filter_exp, 0, net) == -1) {
	//	fprintf(stderr, "Couldn't parse filter %s: %s\n", filter_exp, pcap_geterr(handle));
	//	exit(EXIT_FAILURE);
	//}

	/* apply the compiled filter */
	//if (pcap_setfilter(handle, &fp) == -1) {
	//	fprintf(stderr, "Couldn't install filter %s: %s\n", filter_exp, pcap_geterr(handle));
	//	exit(EXIT_FAILURE);
	//}
	//Initialise MMT_Security
	if(mmt_probe->mmt_conf->security_enable==1)
		init_mmt_security( mmt_probe->smp_threads->mmt_handler, mmt_probe->mmt_conf->properties_file, (void *)mmt_probe->smp_threads );
	//End initialise MMT_Security

	/* now we can set our callback function */
	if (mmt_probe->mmt_conf->thread_nb > 1){
		pcap_loop(handle, num_packets, got_packet_multi_thread, NULL);
	}else {
		while (1){
			pcap_dispatch(handle, num_packets, got_packet_single_thread, NULL);

			if(time(NULL)- mmt_probe->smp_threads->last_stat_report_time  >= mmt_probe->mmt_conf->stats_reporting_period){
				mmt_probe->smp_threads->report_counter ++;
				mmt_probe->smp_threads->last_stat_report_time = time(NULL);
				if(mmt_probe->mmt_conf->enable_session_report == 1)process_session_timer_handler(mmt_probe->smp_threads->mmt_handler);
				if (mmt_probe->mmt_conf->enable_proto_without_session_stats == 1)iterate_through_protocols(protocols_stats_iterator, (void *)mmt_probe->smp_threads);
				if (mmt_probe->smp_threads->file_read_flag == 1){
					new_conditional_reports_init((void *)mmt_probe->smp_threads);
					new_event_reports_init((void *)mmt_probe->smp_threads);
					printf("Added new attributes_th_id = %u\n", mmt_probe->smp_threads->thread_number);
					mmt_probe->smp_threads->file_read_flag = 0;
				}

			}

		}
	}

	//fprintf(stderr, "\n%d packets captured\n", captured);

	/* cleanup */
	pcap_freecode(&fp);
#ifndef RHEL3
	pcap_close(handle);
#endif /* RHEL3 */

	stop = 1;
	fflush(stderr);
	pthread_exit(NULL);
}

void process_interface(char * ifname, struct mmt_probe_struct * mmt_probe) {
	Reader((void*) mmt_probe );
	return;

}

void terminate_probe_processing(int wait_thread_terminate) {
	char lg_msg[1024];
	mmt_probe_context_t * mmt_conf = mmt_probe.mmt_conf;
	int i, j=0, l=0, k=0;

	//For MMT_Security
	//To finish results file (e.g. write summary in the XML file)
	todo_at_end();
	//End for MMT_Security

	//Cleanup
	if (mmt_conf->thread_nb == 1) {
		//One thread for processing packets
		//Cleanup the MMT handler
		flowstruct_cleanup(mmt_probe.smp_threads->mmt_handler); // cleanup our event handler
		radius_ext_cleanup(mmt_probe.smp_threads->mmt_handler); // cleanup our event handler for RADIUS initializations
		//process_session_timer_handler(mmt_probe.->mmt_handler);
		if (mmt_probe.smp_threads->report_counter == 0)mmt_probe.smp_threads->report_counter++;
		if (mmt_conf->enable_proto_without_session_stats == 1)iterate_through_protocols(protocols_stats_iterator, (void *) mmt_probe.smp_threads);
		mmt_close_handler(mmt_probe.smp_threads->mmt_handler);
		report_all_protocols_microflows_stats((void *)mmt_probe.smp_threads);
		if (mmt_conf->output_to_file_enable == 1)flush_messages_to_file_thread((void *)mmt_probe.smp_threads);
		free (mmt_probe.smp_threads->cache_message_list);
		mmt_probe.smp_threads->cache_message_list = NULL;
		exit_timers();

	} else {
		if (wait_thread_terminate) {
			/* Add a dummy packet at each thread packet list tail */

			for (i = 0; i < mmt_conf->thread_nb; i++) {

				pthread_spin_lock(&mmt_probe.smp_threads[i].lock);
				list_add_tail((struct list_entry *) &mmt_probe.smp_threads[i].null_pkt,
						(struct list_entry *) &mmt_probe.smp_threads[i].pkt_head);
				pthread_spin_unlock(&mmt_probe.smp_threads[i].lock);
			}
		}

		/* wait for all threads to complete */
		if (wait_thread_terminate) {
			for (i = 0; i < mmt_conf->thread_nb; i++) {
				pthread_join(mmt_probe.smp_threads[i].handle, NULL);
				report_all_protocols_microflows_stats(&mmt_probe.smp_threads[i]);
				//if (mmt_probe.smp_threads->report_counter == 0)mmt_probe.smp_threads->report_counter++;
				//if (mmt_conf->enable_proto_without_session_stats == 1)iterate_through_protocols(protocols_stats_iterator, &mmt_probe.smp_threads[i]);
				if (mmt_conf->output_to_file_enable == 1)flush_messages_to_file_thread(&mmt_probe.smp_threads[i]);
				free(mmt_probe.smp_threads[i].cache_message_list);
				mmt_probe.smp_threads[i].cache_message_list =NULL;
			}
			exit_timers();
		} else {
			//We might have catched a SEGV or ABORT signal.
			//We have seen the threads in deadlock situations.
			//Wait 30 seconds then cancel the threads
			//Once cancelled, join should give "THREAD_CANCELLED" retval
			sleep(30);
			for (i = 0; i < mmt_conf->thread_nb; i++) {
				int s;
				s = pthread_cancel(mmt_probe.smp_threads[i].handle);
				if (s != 0) {
					exit(1);
				}
			}
			for (i = 0; i < mmt_conf->thread_nb; i++) {
				//pthread_join(mmt_probe.smp_threads[i].handle, NULL);
				if (mmt_probe.smp_threads[i].mmt_handler != NULL) {
					flowstruct_cleanup(mmt_probe.smp_threads[i].mmt_handler); // cleanup our event handler
					radius_ext_cleanup(mmt_probe.smp_threads[i].mmt_handler); // cleanup our event handler for RADIUS initializations
					//process_session_timer_handler(mmt_probe.smp_threads[i].mmt_handler);
					if (mmt_probe.smp_threads->report_counter == 0)mmt_probe.smp_threads->report_counter++;
					if (mmt_conf->enable_proto_without_session_stats == 1)iterate_through_protocols(protocols_stats_iterator, &mmt_probe.smp_threads[i]);
					mmt_close_handler(mmt_probe.smp_threads[i].mmt_handler);
					mmt_probe.smp_threads[i].mmt_handler = NULL;
					free(mmt_probe.smp_threads[i].cache_message_list);
					mmt_probe.smp_threads[i].cache_message_list = NULL;
				}
				report_all_protocols_microflows_stats(&mmt_probe.smp_threads[i].iprobe);
				exit_timers();
			}
		}
	}
	//sleep (2);	// flight time required between a msg send from sockets to destination socket


	//Now close the reporting files.
	//Offline or Online processing
	if (mmt_conf->input_mode == OFFLINE_ANALYSIS||mmt_conf->input_mode == ONLINE_ANALYSIS) {
		if (mmt_conf->data_out_file) fclose(mmt_conf->data_out_file);
		sprintf(lg_msg, "Closing output results file");
		mmt_log(mmt_probe.mmt_conf, MMT_L_INFO, MMT_P_CLOSE_OUTPUT, lg_msg);

	}
	if (mmt_conf->register_new_condition_reports != NULL && mmt_conf->register_new_event_reports != NULL){
		for (i=0; i < mmt_conf->new_condition_reports_nb; i++){
			free (mmt_conf->register_new_condition_reports[i].attributes);
			mmt_conf->register_new_condition_reports[i].attributes = NULL;
			free (mmt_conf->register_new_condition_reports[i].handlers);
			mmt_conf->register_new_condition_reports[i].handlers = NULL;
		}

		for (i=0; i < mmt_conf->new_event_reports_nb; i++){
			free (mmt_conf->register_new_event_reports[i].attributes);
			mmt_conf->register_new_event_reports[i].attributes = NULL;
		}

		free (mmt_conf->register_new_condition_reports);
		mmt_conf->register_new_condition_reports = NULL;

		free (mmt_conf->register_new_event_reports);
		mmt_conf->register_new_event_reports = NULL;

	}

	for (i = 0; i < mmt_conf->condition_reports_nb; i++){
		free (mmt_conf->condition_reports[i].attributes);
		mmt_conf->condition_reports[i].attributes = NULL;
		free (mmt_conf->condition_reports[i].handlers);
		mmt_conf->condition_reports[i].handlers = NULL;
	}
	for (i = 0; i < mmt_conf->event_reports_nb; i++){
		free (mmt_conf->event_reports[i].attributes);
		mmt_conf->event_reports[i].attributes = NULL;
	}
	free (mmt_conf->condition_reports);
	mmt_conf->condition_reports = NULL;
	free (mmt_conf->event_reports);
	mmt_conf->event_reports = NULL;
	for (i=0; i < mmt_conf->security_reports_nb; i++){
		free (mmt_conf->security_reports[i].attributes);
		mmt_conf->security_reports[i].attributes = NULL;
		for (l = 0; l < mmt_conf->security_reports[i].event_name_nb; l++ ){
			free (mmt_conf->security_reports[i].event_name[l]);
			mmt_conf->security_reports[i].event_name[l] = NULL;
		}
		free (mmt_conf->security_reports[i].event_name);
		mmt_conf->security_reports[i].event_name = NULL;
		free (mmt_conf->security_reports[i].event_id);
		mmt_conf->security_reports[i].event_id = NULL;
	}
	free (mmt_conf->security_reports);
	int retval = 0;
	uint32_t count = 0;
	if (mmt_conf->thread_nb > 1){
		for(i=0; i<mmt_conf->thread_nb; i++){
			printf ("th_nb =%u, packet_send = %u \n", i, mmt_probe.smp_threads[i].packet_send);
			count += mmt_probe.smp_threads[i].packet_send;
			data_spsc_ring_free( &mmt_probe.smp_threads[i].fifo );
			//if(mmt_probe.smp_threads[i].sockfd_unix > 0)close(mmt_probe.smp_threads[i].sockfd_unix);
			if (mmt_probe.smp_threads[i].report != NULL){
				for(j = 0; j < mmt_conf->security_reports_nb; j++) {
					if (mmt_probe.smp_threads[i].report[j].data != NULL){
						retval = sendmmsg(mmt_probe.smp_threads[i].sockfd_internet[j], &mmt_probe.smp_threads[i].report[j].grouped_msg, 1, 0);
						if (retval == -1)
							perror("sendmmsg()");
						if (mmt_probe.smp_threads[i].report[j].msg != NULL){
							free (mmt_probe.smp_threads[i].report[j].msg);
						}
						for (l = 0; l < mmt_conf->nb_of_report_per_msg; l++)free (mmt_probe.smp_threads[i].report[j].data[l]);
					}
					free (mmt_probe.smp_threads[i].report[j].data);
				}

				free (mmt_probe.smp_threads[i].report);

			}
			//sleep (1);	// flight time required between a msg send from sockets to destination socket


			if (mmt_probe.smp_threads[i].sockfd_internet != NULL){
				for (j = 0; j < mmt_conf->server_ip_nb; j++){
					if(mmt_probe.smp_threads[i].sockfd_internet[j] > 0)close(mmt_probe.smp_threads[i].sockfd_internet[j]);
				}
				free (mmt_probe.smp_threads[i].sockfd_internet);
			}




			if (mmt_probe.smp_threads[i].security_attributes != NULL){
				free (mmt_probe.smp_threads[i].security_attributes);
			}

			/*			if (mmt_probe.smp_threads[i].event_reports != NULL){
				free (mmt_probe.smp_threads[i].event_reports);
				mmt_probe.smp_threads[i].event_reports = NULL;
			}*/
		}
		printf ("total_packets_send= %u \n",count);

		free( mmt_probe.smp_threads);
		mmt_probe.smp_threads = NULL;
	}else {
		/*		if (mmt_probe.smp_threads->event_reports != NULL){
			free(mmt_probe.smp_threads->event_reports);
			mmt_probe.smp_threads->event_reports = NULL;
		}*/
		if (mmt_probe.smp_threads->report != NULL){
			for(j = 0; j < mmt_conf->security_reports_nb; j++) {
				if (mmt_probe.smp_threads->report[j].data != NULL){
					retval = sendmmsg(mmt_probe.smp_threads->sockfd_internet[j], &mmt_probe.smp_threads->report[j].grouped_msg, 1, 0);
					// flight time required between a msg send from sockets to destination socket
					if (retval == -1)
						perror("sendmmsg()");

					if (mmt_probe.smp_threads->report[j].msg != NULL){
						free (mmt_probe.smp_threads->report[j].msg);
					}
					for (l = 0; l < mmt_conf->nb_of_report_per_msg; l++) free (mmt_probe.smp_threads->report[j].data[l]);
				}
				free (mmt_probe.smp_threads->report[j].data);

			}
			free (mmt_probe.smp_threads->report);
		}
		//sleep (1);	// flight time required between a msg send from sockets to destination socket

		if (mmt_probe.smp_threads->sockfd_internet != NULL){
			for (j = 0; j < mmt_conf->server_ip_nb; j++){
				if(mmt_probe.smp_threads->sockfd_internet[j] > 0)close(mmt_probe.smp_threads->sockfd_internet[j]);
			}
			free (mmt_probe.smp_threads->sockfd_internet);
		}


		if (mmt_probe.smp_threads->security_attributes != NULL){
			free (mmt_probe.smp_threads->security_attributes);
		}
		printf ("3.. packet_send = %u \n", mmt_probe.smp_threads->packet_send);

		free (mmt_probe.smp_threads);
		mmt_probe.smp_threads = NULL;
	}

	//printf("HERE_close_extraction1\n");
	close_extraction();
	//printf("HERE_close_extraction2\n");

	mmt_log(mmt_conf, MMT_L_INFO, MMT_E_END, "Closing MMT Extraction engine!");
	mmt_log(mmt_conf, MMT_L_INFO, MMT_P_END, "Closing MMT Probe!");
	if (mmt_conf->log_output) fclose(mmt_conf->log_output);
}

/* This signal handler ensures clean exits */
void signal_handler(int type) {
	static int i = 0;
	i++;
	char lg_msg[1024];
	fprintf(stderr, "\n reception of signal %d\n", type);
	fflush( stderr );
	cleanup( 0 );

	if (i == 1) {
		terminate_probe_processing(0);
		/*
                    if(strlen(mmt_probe.mmt_conf->input_f_name) > 1) {
                        if (remove(mmt_probe.mmt_conf->input_f_name) != 0) {
                            //fprintf(stdout, "Trace Error deleting file\n");
                            sprintf(lg_msg, "Error while deleting trace file: %s! File will remain on the system. Manual delete required!", mmt_probe.mmt_conf->input_f_name);
                            mmt_log(mmt_probe.mmt_conf, MMT_L_ERROR, MMT_P_TRACE_DELETE, lg_msg);
                        } else {
                            sprintf(lg_msg, "Trace file %s deleted following the reception of error signal", mmt_probe.mmt_conf->input_f_name);
                            mmt_log(mmt_probe.mmt_conf, MMT_L_INFO, MMT_P_TRACE_DELETE, lg_msg);
                            //fprintf(stdout, "Trace File %s successfully deleted\n", trace_file_name);
                        }
                    }
		 */
	} else {

		signal(SIGINT, signal_handler);
		sprintf(lg_msg, "reception of signal %i while processing a signal exiting!", type);
		/*
                mmt_log(mmt_probe.mmt_conf, MMT_L_EMERGENCY, MMT_P_TERMINATION, "Multi signal received! cleaning up!");
                    if(strlen(mmt_probe.mmt_conf->input_f_name) > 1) {
                        if (remove(mmt_probe.mmt_conf->input_f_name) != 0) {
                            //fprintf(stdout, "Trace Error deleting file\n");
                            sprintf(lg_msg, "Error while deleting trace file: %s! File will remain on the system. Manual delete required!", mmt_probe.mmt_conf->input_f_name);
                            mmt_log(mmt_probe.mmt_conf, MMT_L_ERROR, MMT_P_TRACE_DELETE, lg_msg);
                        } else {
                            sprintf(lg_msg, "Trace file %s deleted following the reception of error signal", mmt_probe.mmt_conf->input_f_name);
                            mmt_log(mmt_probe.mmt_conf, MMT_L_INFO, MMT_P_TRACE_DELETE, lg_msg);
                            //fprintf(stdout, "Trace File %s successfully deleted\n", trace_file_name);
                        }
                    }
		 */
		exit(0);
	}

	switch (type) {
	case SIGSEGV:
		mmt_log(mmt_probe.mmt_conf, MMT_L_EMERGENCY, MMT_P_SEGV_ERROR, "Segv signal received! cleaning up!");
		//terminate_probe_processing();
		//fprintf(stdout, "SEGMENTATION FAULT!!!! Exiting!!! \n");
		//Now delete the last input file if it is available. This is to avoid blocking situation in continuous trace file processing
		/*
                        if(strlen(mmt_probe.mmt_conf->input_f_name) > 1) {
                            if (remove(mmt_probe.mmt_conf->input_f_name) != 0) {
                                //fprintf(stdout, "Trace Error deleting file\n");
                                sprintf(lg_msg, "Error while deleting trace file: %s! File will remain on the system. Manual delete required!", mmt_probe.mmt_conf->input_f_name);
                                mmt_log(mmt_probe.mmt_conf, MMT_L_ERROR, MMT_P_TRACE_DELETE, lg_msg);
                            } else {
                                sprintf(lg_msg, "Trace file %s deleted following the reception of error signal", mmt_probe.mmt_conf->input_f_name);
                                mmt_log(mmt_probe.mmt_conf, MMT_L_INFO, MMT_P_TRACE_DELETE, lg_msg);
                                //fprintf(stdout, "Trace File %s successfully deleted\n", trace_file_name);
                            }
                        }
		 */
		exit(1);
	case SIGTERM:
		mmt_log(mmt_probe.mmt_conf, MMT_L_WARNING, MMT_P_TERMINATION, "Termination signal received! Cleaning up before exiting!");
		//terminate_probe_processing();
		//fprintf(stdout, "Terminating\n");
		exit(1);
	case SIGABRT:
		mmt_log(mmt_probe.mmt_conf, MMT_L_WARNING, MMT_P_TERMINATION, "Abort signal received! Cleaning up before exiting!");
		//terminate_probe_processing();
		//fprintf(stdout, "Terminating\n");
		exit(1);
	case SIGINT:
		mmt_log(mmt_probe.mmt_conf, MMT_L_WARNING, MMT_P_TERMINATION, "Interruption Termination signal received! Cleaning up before exiting!");
		//terminate_probe_processing();
		//fprintf(stdout, "Terminating\n");
		exit(0);
#ifndef _WIN32
	case SIGKILL:
		mmt_log(mmt_probe.mmt_conf, MMT_L_WARNING, MMT_P_TERMINATION, "Kill signal received! Cleaning up before exiting!");
		//terminate_probe_processing();
		//fprintf(stdout, "Terminating\n");
		exit(1);
#endif
	default:
		mmt_log(mmt_probe.mmt_conf, MMT_L_WARNING, MMT_P_TERMINATION, "Received an unexpected signal!");
		exit(1);
	}
}

void create_socket(mmt_probe_context_t * mmt_conf, void *args){
	/*.....socket */
	struct sockaddr_in in_serv_addr;
	struct hostent *server;
	struct smp_thread *th = (struct smp_thread *) args;
	int i = 0, on;
	on = 1;

	/*Internet socket*/
	//if (mmt_conf->socket_domain == 1|| mmt_conf->socket_domain == 2){
	th->sockfd_internet = calloc(sizeof(uint32_t), mmt_conf->server_ip_nb);

	for (i = 0; i < mmt_conf->server_ip_nb; i++){
		th->sockfd_internet[i] = socket(AF_INET, SOCK_STREAM, 0);
		if (th->sockfd_internet[i] < 0)
			error("ERROR opening socket");
		if (setsockopt(th->sockfd_internet[i] , SOL_SOCKET, SO_REUSEADDR, (char *) &on, sizeof(on)) == -1) {
			perror("setsockopt(SO_REUSEADDR)");
			exit(1);
		}
		//setsockopt( th->sockfd_internet[i], IPPROTO_TCP, TCP_NODELAY, (void *)&on, sizeof(on)); // need to experiment
		server = gethostbyname(mmt_conf->server_adresses[i].server_ip_address);
		if (server == NULL) {
			fprintf(stderr,"ERROR, no such host\n");
			//exit(0);
		}
		bzero((char *) &in_serv_addr, sizeof(in_serv_addr));
		in_serv_addr.sin_family = AF_INET;
		bcopy((char *)server->h_addr,
				(char *)&in_serv_addr.sin_addr.s_addr,
				server->h_length);

		if (mmt_conf->one_socket_server == 1){

			in_serv_addr.sin_port = htons(mmt_conf->server_adresses[i].server_portnb[0]);
			//printf("th_nb=%u,ip = %s,port = %u \n",th->thread_number,mmt_conf->server_adresses[i].server_ip_address,mmt_conf->server_adresses[i].server_portnb[0]);

		}else{

			in_serv_addr.sin_port = htons(mmt_conf->server_adresses[i].server_portnb[th->thread_number]);
			//printf("th_nb=%u,ip = %s,port = %u \n",th->thread_number,mmt_conf->server_adresses[i].server_ip_address,mmt_conf->server_adresses[i].server_portnb[th->thread_number]);
		}

		if (connect(th->sockfd_internet[i], (struct sockaddr *) &in_serv_addr, sizeof(in_serv_addr)) < 0)
			fprintf(stderr,"ERROR cannot connect to a socket(check availability of server):%s\n", strerror(errno));
		//error("ERROR connecting");
	}
}

int main(int argc, char **argv) {
	char mmt_errbuf[1024];
	int i, j, l = 0;
	char lg_msg[1024];
	sigset_t signal_set;
	char single_file [MAX_FILE_NAME+1] = {0};
	pthread_mutex_init(&mutex_lock, NULL);
	pthread_spin_init(&spin_lock, 0);

	mmt_probe.smp_threads = NULL;
	mmt_probe.mmt_conf = NULL;

	mmt_probe_context_t * mmt_conf = get_probe_context_config();
	mmt_probe.mmt_conf = mmt_conf;

	parseOptions(argc, argv, mmt_conf);

	mmt_conf->log_output = fopen(mmt_conf->log_file, "a");


	sigfillset(&signal_set);
	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);
	signal(SIGSEGV, signal_handler);
	signal(SIGABRT, signal_handler);

	if (mmt_conf->sampled_report == 0) {
		int len = 0;
		len = snprintf(single_file,MAX_FILE_NAME,"%s%s", mmt_conf->output_location, mmt_conf->data_out);
		single_file[len] = '\0';
		update_reporting_time = time(0);

		mmt_conf->data_out_file = fopen(single_file, "w");

		if (mmt_conf->data_out_file == NULL){
			fprintf ( stderr , "\n[e] Error: %d creation of \"%s\" failed: %s\n" , errno ,single_file, strerror( errno ) );
			exit(0);
		}

		sprintf(lg_msg, "Open output results file: %s", single_file);
		mmt_log(mmt_conf, MMT_L_INFO, MMT_P_OPEN_OUTPUT, lg_msg);
	}
	is_stop_timer = 0;

	if (license_expiry_check(0) == 1){
		//exit(0);
	}

	mmt_log(mmt_conf, MMT_L_INFO, MMT_P_INIT, "MMT Probe started!");

	if (!init_extraction()) { // general ixE initialization
		fprintf(stderr, "MMT extract init error\n");
		mmt_log(mmt_conf, MMT_L_ERROR, MMT_E_INIT_ERROR, "MMT Extraction engine initialization error! Exiting!");
		return EXIT_FAILURE;
	}

	//For MMT_Security
	if (mmt_conf->security_enable == 1)
		todo_at_start(mmt_conf->dir_out);
	//End for MMT_Security
	mmt_conf->file_modified_time = time (0);
	//Initialization

	for(i = 0; i < mmt_conf->security_reports_nb; i++) {
		if (mmt_conf->security_reports[i].enable == 1){
			mmt_conf->security_reports[i].event_id = malloc (mmt_conf->security_reports[i].event_name_nb * sizeof (uint32_t *));
			if (strcmp(mmt_conf->security_reports[i].event_name[0],"null") != 0){
				if (mmt_conf->security_reports[i].event_name_nb > 0){
					for (l = 0; l < mmt_conf->security_reports[i].event_name_nb; l++){
						mmt_conf->security_reports[i].event_id[l] = get_protocol_id_by_name (mmt_conf->security_reports[i].event_name[l]);
						//printf("name=%s\n",mmt_conf->security_reports[i].event_name[l]);

					}
				}
			}else{
				mmt_conf->security_reports[i].event_id[0] = 0;//incase when the event_name is NULL;
			}
		}
	}

	if (mmt_conf->thread_nb == 1) {

		mmt_log(mmt_conf, MMT_L_INFO, MMT_E_INIT, "Initializating MMT Extraction engine! Single threaded operation.");
		mmt_probe.smp_threads = (struct smp_thread *) calloc(mmt_conf->thread_nb,sizeof (struct smp_thread));
		mmt_probe.smp_threads->last_stat_report_time = time(0);
		mmt_probe.smp_threads->pcap_last_stat_report_time = 0;
		mmt_probe.smp_threads->pcap_current_packet_time = 0;
		//One thread for reading packets and processing them
		//Initialize an MMT handler
		mmt_probe.smp_threads->mmt_handler = mmt_init_handler(DLT_EN10MB, 0, mmt_errbuf);
		if (!mmt_probe.smp_threads->mmt_handler) { /* pcap error ? */
			fprintf(stderr, "MMT handler init failed for the following reason: %s\n", mmt_errbuf);
			mmt_log(mmt_conf, MMT_L_ERROR, MMT_E_INIT_ERROR, "MMT Extraction handler initialization error! Exiting!");
			return EXIT_FAILURE;
		}

		//mmt_probe.iprobe.instance_id = 0;
		mmt_probe.smp_threads->thread_number = 0;
		for (i = 0; i < PROTO_MAX_IDENTIFIER; i++) {
			reset_microflows_stats(&mmt_probe.smp_threads->iprobe.mf_stats[i]);
			mmt_probe.smp_threads->iprobe.mf_stats[i].application = get_protocol_name_by_id(i);
			mmt_probe.smp_threads->iprobe.mf_stats[i].application_id = i;
		}
		mmt_probe.smp_threads->iprobe.instance_id = mmt_probe.smp_threads->thread_number;
		mmt_probe.smp_threads->thread_number = 0;

		pthread_spin_init(&mmt_probe.smp_threads->lock, 0);

		// customized packet and session handling functions are then registered
		if(mmt_probe.mmt_conf->enable_flow_stats) {
			if (mmt_probe.mmt_conf->enable_session_report == 1)register_session_timer_handler(mmt_probe.smp_threads->mmt_handler, print_ip_session_report, (void *) mmt_probe.smp_threads);
			if (mmt_probe.mmt_conf->enable_session_report == 1)register_session_timeout_handler(mmt_probe.smp_threads->mmt_handler, classification_expiry_session, (void *) mmt_probe.smp_threads);
			if (mmt_probe.mmt_conf->enable_session_report == 1)flowstruct_init((void *)mmt_probe.smp_threads); // initialize our event handler
			if(mmt_conf->event_based_reporting_enable == 1)event_reports_init((void *)mmt_probe.smp_threads); // initialize our event reports
			if(mmt_conf->condition_based_reporting_enable == 1)conditional_reports_init((void *)mmt_probe.smp_threads);// initialize our conditional reports
			if(mmt_conf->radius_enable == 1)radius_ext_init((void *)mmt_probe.smp_threads); // initialize radius extraction and attribute event handler
			set_default_session_timed_out(mmt_probe.smp_threads->mmt_handler, mmt_conf->default_session_timeout);
			set_long_session_timed_out(mmt_probe.smp_threads->mmt_handler, mmt_conf->long_session_timeout);
			set_short_session_timed_out(mmt_probe.smp_threads->mmt_handler, mmt_conf->short_session_timeout);
			set_live_session_timed_out(mmt_probe.smp_threads->mmt_handler, mmt_conf->live_session_timeout);
		}
		if (mmt_conf->enable_security_report == 1)security_reports_init((void *)mmt_probe.smp_threads);// should be defined before proto_stats_init
		if (mmt_conf->enable_security_report == 0)proto_stats_init(mmt_probe.smp_threads);


		mmt_log(mmt_conf, MMT_L_INFO, MMT_E_STARTED, "MMT Extraction engine! successfully initialized in a single threaded operation.");
	} else {
		//Multiple threads for processing the packets
		/*
		 * Array of list of packets for all threads
		 */
		sprintf(lg_msg, "Initializating MMT Extraction engine! Multi threaded operation (%i threads)", mmt_conf->thread_nb);
		mmt_log(mmt_conf, MMT_L_INFO, MMT_E_INIT, lg_msg);
		mmt_probe.smp_threads = (struct smp_thread *) calloc(mmt_conf->thread_nb, sizeof (struct smp_thread));
		/* run threads */
		for (i = 0; i < mmt_conf->thread_nb; i++) {
			init_list_head((struct list_entry *) &mmt_probe.smp_threads[i].pkt_head);
			pthread_spin_init(&mmt_probe.smp_threads[i].lock, 0);
			mmt_probe.smp_threads[i].thread_number = i;
			mmt_probe.smp_threads[i].last_stat_report_time = time(0);
			mmt_probe.smp_threads[i].pcap_last_stat_report_time = 0;
			mmt_probe.smp_threads[i].pcap_current_packet_time = 0;
			//mmt_probe.smp_threads[i].last_msg_report_time = time(0);
			mmt_probe.smp_threads[i].null_pkt.pkt.data = NULL;

			mmt_probe.smp_threads[i].nb_dropped_packets = 0;
			mmt_probe.smp_threads[i].nb_packets         = 0;

			if( data_spsc_ring_init( &mmt_probe.smp_threads[i].fifo, mmt_conf->thread_queue_plen, mmt_conf->requested_snap_len ) != 0 ){
				perror("Not enough memory. Please reduce thread-queue or thread-nb in .conf");
				//free memory allocated
				for(j=0; j<=i; j++)
					data_spsc_ring_free( &mmt_probe.smp_threads[j].fifo );
				exit( 0 );
			}

			pthread_create(&mmt_probe.smp_threads[i].handle, NULL,
					smp_thread_routine, &mmt_probe.smp_threads[i]);
		}
		sprintf(lg_msg, "MMT Extraction engine! successfully initialized in a multi threaded operation (%i threads)", mmt_conf->thread_nb);
		mmt_log(mmt_conf, MMT_L_INFO, MMT_E_STARTED, lg_msg);
	}
	start_timer( mmt_probe.mmt_conf->sampled_report_period, flush_messages_to_file_thread, (void *) &mmt_probe);
	//Offline or Online processing
	if (mmt_conf->input_mode == OFFLINE_ANALYSIS) {
		process_trace_file(mmt_conf->input_source, &mmt_probe); //Process single offline trace
		//We don't close the files here because they will be used when the handler is closed to report still to timeout flows
	}else if (mmt_conf->input_mode == ONLINE_ANALYSIS) {
		process_interface(mmt_conf->input_source, &mmt_probe); //Process single offline trace
		//We don't close the files here because they will be used when the handler is closed to report still to timeout flows
	}

	terminate_probe_processing(1);

	//printf("Process Terminated successfully\n");
	return EXIT_SUCCESS;
}

