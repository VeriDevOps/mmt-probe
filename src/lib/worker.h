/*
 * worker.h
 *
 *  Created on: Dec 18, 2017
 *          by: Huu Nghia
 */

#ifndef SRC_LIB_WORKER_H_
#define SRC_LIB_WORKER_H_



#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h> //for uint64_t PRIu64
#include <stdbool.h>
#include <signal.h>
#include <mmt_core.h>

#include "limit.h"
#include "context.h"
#include "alloc.h"
#include "configure.h"

#include "../modules/output/output.h"
#include "../modules/dpi/dpi.h"

struct security_context_struct;

//for each thread
struct worker_context_struct{
	mmt_handler_t *dpi_handler;
	//statistics
	struct {
		uint64_t pkt_processed; //number of packets being processed by MMT
		uint64_t pkt_dropped;   //number of packets be dropped by MMT
		IF_ENABLE_SECURITY_MODULE(
				uint64_t alert_generated //number of alerts being generated by security
		);
	}stat;

	//point to its root
	probe_context_t *probe_context;

	output_t *output;

	dpi_context_t *dpi_context;

	//input
	IF_ENABLE_PCAP_MODULE(
			struct pcap_worker_context_struct *pcap );

	IF_ENABLE_DPDK_MODULE(
			struct dpdk_worker_context_struct *dpdk );

	IF_ENABLE_SECURITY_MODULE(
			struct security_context_struct *security );

	uint16_t index;    //thread index
	uint16_t lcore_id; //id of logical core on which the thread is running
	pid_t    pid;
};

static inline void worker_process_a_packet( worker_context_t *worker_context, struct pkthdr *pkt_header, const u_char *pkt_data ){
	//printf("%d %5d %5d\n", worker_context->index, header->caplen, header->len );
	//fflush( stdout );
	packet_process(worker_context->dpi_handler, pkt_header, pkt_data);
	worker_context->stat.pkt_processed ++;
}

worker_context_t * worker_alloc_init();

void worker_release( worker_context_t *worker_context );

/**
 * This callback is called after starting a worker
 * @param worker_context
 */
void worker_on_start( worker_context_t *worker_context );

/**
 * This callback is called before stopping a worker
 * @param worker_context
 */
void worker_on_stop( worker_context_t *worker_context );

/**
 * This must be called periodically each x seconds depending on config.stats_period
 * @param worker_context
 */
void worker_on_timer_stat_period( worker_context_t *worker_context );

/**
 * This must be called periodically each x seconds (= file-output.output-period) if
 * - file output is enable, and,
 * - file output is sampled
 * @param worker_context
 */
void worker_on_timer_sample_file_period( worker_context_t *worker_context );


void worker_print_common_statistics( const probe_context_t *context );

#endif /* SRC_LIB_WORKER_H_ */
