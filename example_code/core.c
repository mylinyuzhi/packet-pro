#include <unistd.h>
#include <sys/time.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <signal.h>
#include <assert.h>

#include <rte_config.h>
#include <rte_common.h>
#include <rte_common_vect.h>
#include <rte_byteorder.h>
#include <rte_log.h>
#include <rte_memory.h>
#include <rte_memcpy.h>
#include <rte_memzone.h>
#include <rte_malloc.h>
#include <rte_tailq.h>
#include <rte_eal.h>
#include <rte_per_lcore.h>
#include <rte_launch.h>
#include <rte_atomic.h>
#include <rte_cycles.h>
#include <rte_prefetch.h>
#include <rte_lcore.h>
#include <rte_per_lcore.h>
#include <rte_branch_prediction.h>
#include <rte_interrupts.h>
#include <rte_pci.h>
#include <rte_random.h>
#include <rte_debug.h>
#include <rte_ether.h>
#include <rte_ethdev.h>
#include <rte_ring.h>
#include <rte_mempool.h>
#include <rte_mbuf.h>
#include <rte_tcp.h>
#include <rte_udp.h>
#include <rte_string_fns.h>

#include "cpu.h"
#include "dpdk.h"
#include "eth_in.h"
#include "fhash.h"
#include "tcp_send_buffer.h"
#include "tcp_ring_buffer.h"
#include "socket.h"
#include "eth_out.h"
#include "tcp_in.h"
#include "tcp_out.h"
#include "mtcp_api.h"
#include "eventpoll.h"
#include "logger.h"
#include "config.h"
#include "arp.h"
#include "ip_out.h"
#include "timer.h"
#include "debug.h"

#define ROUND_STAT FALSE
#define TIME_STAT  FALSE
#define EVENT_STAT FALSE
#define TESTING    FALSE

#define LOG_FILE_NAME "log"
#define MAX_FILE_NAME 1024

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

#define PER_STREAM_SLICE 0.1		
#define PER_STREAM_TCHECK 1		
#define PS_SELECT_TIMEOUT 100	    

#define GBPS(bytes) (bytes * 8.0 / (1000 * 1000 * 1000))

/*----------------------------------------------------------------------------*/
/* handlers for threads */
struct mtcp_thread_context *g_pctx[MAX_CPUS];
struct log_thread_context *g_logctx[MAX_CPUS];
/*----------------------------------------------------------------------------*/
static pthread_t g_thread[MAX_CPUS];
static pthread_t log_thread[MAX_CPUS];
/*----------------------------------------------------------------------------*/
static sem_t g_init_sem[MAX_CPUS];
static int running[MAX_CPUS];
/*----------------------------------------------------------------------------*/
mtcp_sighandler_t app_signal_handler;
static int sigint_cnt[MAX_CPUS];
static struct timeval sigint_ts[MAX_CPUS];
/*----------------------------------------------------------------------------*/
#ifdef NETSTAT
#if NETSTAT_TOTAL
static int printer = -1;
#if ROUND_STAT
#endif /* ROUND_STAT */
#endif /* NETSTAT_TOTAL */
#endif /* NETSTAT */

/*----------------------------------------------------------------------------*/
/* DPDK interface configurations */

#define NB_RX_QUEUE 1

#define NB_TX_QUEUE 1

#define	NB_RX_DESC 128

#define	NB_TX_DESC 128

#define POOL_SIZE (8 * 1024)
#define CACHE_SIZE  64

static struct rte_eth_conf eth_conf = {
  .rxmode = {
    .mq_mode = ETH_MQ_RX_RSS,
    .split_hdr_size = 0,
    .header_split = 0,
    .hw_ip_checksum = 1,
    .hw_vlan_filter = 0,
    .jumbo_frame = 0,
    .hw_strip_crc = 0,
  },
  .rx_adv_conf = {
    .rss_conf = {
      .rss_key = NULL,
      .rss_hf = ETH_RSS_IP,
    },
  },
  .txmode = {
    .mq_mode = ETH_MQ_TX_NONE,
  },
};

static struct rte_eth_txconf tx_conf = {
  .tx_thresh = {
    .pthresh = 36,
    .hthresh = 0,
    .wthresh = 0,
  },
  .tx_free_thresh = 0,
  .tx_rs_thresh = 0,
  /*  .txq_flags = (ETH_TXQ_FLAGS_NOMULTSEGS |
		ETH_TXQ_FLAGS_NOVLANOFFL |
		ETH_TXQ_FLAGS_NOXSUMSCTP |
		ETH_TXQ_FLAGS_NOXSUMUDP  |
		ETH_TXQ_FLAGS_NOXSUMTCP), */
};

static struct rte_eth_rxconf rx_conf = {
  .rx_thresh = {
    .pthresh = 8,
    .hthresh = 8,
    .wthresh = 4,
  },
  .rx_free_thresh = 64,
  .rx_drop_en = 0,
};

/*----------------------------------------------------------------------------*/
void HandleSignal(int signal)
{
  struct timeval cur_ts;
  int core, i = 0;
  
  if (signal == SIGINT) {
    core = sched_getcpu();
        
    gettimeofday(&cur_ts, NULL);
    
    if (sigint_cnt[core] > 0 && cur_ts.tv_sec > sigint_ts[core].tv_sec) {
      for (i = 0; i < num_cpus; i++) {
	if (running[i]) {
	  g_pctx[i]->exit = TRUE;
	}
      }
    } 
    else {
      for (i = 0; i < num_cpus; i++) {
	//printf("Set CPU %s thread interruptible\n", i);
	g_pctx[i]->interrupt = TRUE;
      }
      if (!app_signal_handler) {
	for (i = 0; i < num_cpus; i++) {
	  if (running[i]) {
	    g_pctx[i]->exit = TRUE;
	  }
	}
      }
    }
    sigint_cnt[core]++;
    gettimeofday(&sigint_ts[core], NULL);
  }
  
  if (signal != SIGUSR1) {
    if (app_signal_handler) {
      app_signal_handler(signal);
    }
  }
}
/*----------------------------------------------------------------------------*/
static int AttachDevice(struct mtcp_thread_context* ctx)
{
  int ret, ifidx;
  // int working = 0;
  int i;
  struct rte_eth_link link;
  // struct rte_eth_conf eth_conf;

  // attaching (device, queue)
  for (i = 0; i < num_devices_attached; i++) {
    ifidx = devices_attached[i];
    //if (devices[devices_attached[i]].num_rx_queues <= ctx->cpu)
    //  continue;  
    //ret = ps_attach_rx_device(handle, &queue);

    //memset(&eth_conf, 0, sizeof eth_conf);
    ret = rte_eth_dev_configure(ifidx, NB_RX_QUEUE, NB_TX_QUEUE, &eth_conf);
    if (ret < 0) {
      rte_exit(EXIT_FAILURE, "Cannot configure device: error=%d, port=%d\n",
	       ret, ifidx);
    }
    //printf("If %d rte_eth_dev_configure() successful\n", ifidx);
    // socketid = rte_lcore_to_socket_id(ctx->cpu);
    /* TODO: uses constatnt RX/TX ring descriptors, assumes devices use only one queue */ 
    ret = rte_eth_tx_queue_setup(ifidx, 0, NB_TX_DESC, ctx->cpu, &tx_conf);
    if (ret < 0) {
      rte_exit(EXIT_FAILURE, "rte_eth_tx_queue_setup(): error=%d, port=%d\n",
	       ret, ifidx);
    }
    //printf("If %d rte_eth_tx_queue_setup() successful\n", ifidx);
    ret = rte_eth_rx_queue_setup(ifidx, 0, NB_RX_DESC, ctx->cpu, &rx_conf, ctx->rx_pool);
    if (ret < 0) {
      rte_exit(EXIT_FAILURE, "rte_eth_rx_dev_queue_setup(): error=%d, port=%d\n",
	       ret, ifidx);
    }
    //printf("If %d rte_eth_rx_queue_setup() successful\n", ifidx);
    ret = rte_eth_dev_start(ifidx);
    if (ret < 0) {
      rte_exit(EXIT_FAILURE, "rte_eth_dev_start(): error=%d, port=%d\n",
	       ret, ifidx);
    }
    //printf("If %d rte_eth_dev_start() successful\n", ifidx);
    rte_eth_link_get(ifidx, &link);
    if (link.link_status == 0) {
      rte_exit(EXIT_FAILURE, "DPDK interface is down: %d\n", ifidx);
    }
    printf("Interface %d is UP and RUNNING\n", ifidx);
    rte_eth_promiscuous_enable(ifidx);
  }
  return 0;
}
/*----------------------------------------------------------------------------*/
#ifdef NETSTAT
static inline void InitStatCounter(struct stat_counter *counter)
{
  counter->cnt = 0;
  counter->sum = 0;
  counter->max = 0;
  counter->min = 0;
}
/*----------------------------------------------------------------------------*/
static inline void UpdateStatCounter(struct stat_counter *counter, int64_t value)
{
  counter->cnt++;
  counter->sum += value;
  if (value > counter->max)
    counter->max = value;
  if (counter->min == 0 || value < counter->min)
    counter->min = value;
}
/*----------------------------------------------------------------------------*/
static inline uint64_t GetAverageStat(struct stat_counter *counter)
{
  return counter->cnt ? (counter->sum / counter->cnt) : 0;
}
/*----------------------------------------------------------------------------*/
static inline int64_t TimeDiffUs(struct timeval *t2, struct timeval *t1)
{
  return (t2->tv_sec - t1->tv_sec) * 1000000 + (int64_t)(t2->tv_usec - t1->tv_usec);
}
/*----------------------------------------------------------------------------*/
static inline void PrintThreadNetworkStats(mtcp_manager_t mtcp, struct net_stat *ns)
{
  int i;
  
  for (i = 0; i < CONFIG.eths_num; i++) {
    ns->rx_packets[i] = mtcp->nstat.rx_packets[i] - mtcp->p_nstat.rx_packets[i];
    ns->rx_errors[i] = mtcp->nstat.rx_errors[i] - mtcp->p_nstat.rx_errors[i];
    ns->rx_bytes[i] = mtcp->nstat.rx_bytes[i] - mtcp->p_nstat.rx_bytes[i];
    ns->tx_packets[i] = mtcp->nstat.tx_packets[i] - mtcp->p_nstat.tx_packets[i];
    ns->tx_drops[i] = mtcp->nstat.tx_drops[i] - mtcp->p_nstat.tx_drops[i];
    ns->tx_bytes[i] = mtcp->nstat.tx_bytes[i] - mtcp->p_nstat.tx_bytes[i];
#if NETSTAT_PERTHREAD
    if (CONFIG.eths[i].stat_print) {
      fprintf(stderr, "[CPU%2d] xge %d flows: %6u, "
	      "RX: %7ld(pps) (err: %5ld), %5.2lf(Gbps), "
	      "TX: %7ld(pps), %5.2lf(Gbps)\n", 
	      mtcp->ctx->cpu, i, mtcp->flow_cnt, 
	      ns->rx_packets[i], ns->rx_errors[i], GBPS(ns->rx_bytes[i]), 
	      ns->tx_packets[i], GBPS(ns->tx_bytes[i]));
    }
#endif
  }
  mtcp->p_nstat = mtcp->nstat;
}
/*----------------------------------------------------------------------------*/
#if ROUND_STAT 
static inline void PrintThreadRoundStats(mtcp_manager_t mtcp, struct run_stat *rs)
{
#define ROUND_DIV (1000)
  rs->rounds = mtcp->runstat.rounds - mtcp->p_runstat.rounds;
  rs->rounds_rx = mtcp->runstat.rounds_rx - mtcp->p_runstat.rounds_rx;
  rs->rounds_rx_try = mtcp->runstat.rounds_rx_try - mtcp->p_runstat.rounds_rx_try;
  rs->rounds_tx = mtcp->runstat.rounds_tx - mtcp->p_runstat.rounds_tx;
  rs->rounds_tx_try = mtcp->runstat.rounds_tx_try - mtcp->p_runstat.rounds_tx_try;
  rs->rounds_select = mtcp->runstat.rounds_select - mtcp->p_runstat.rounds_select;
  rs->rounds_select_rx = mtcp->runstat.rounds_select_rx - mtcp->p_runstat.rounds_select_rx;
  rs->rounds_select_tx = mtcp->runstat.rounds_select_tx - mtcp->p_runstat.rounds_select_tx;
  rs->rounds_select_intr = mtcp->runstat.rounds_select_intr - mtcp->p_runstat.rounds_select_intr;
  rs->rounds_twcheck = mtcp->runstat.rounds_twcheck - mtcp->p_runstat.rounds_twcheck;
  mtcp->p_runstat = mtcp->runstat;
#if NETSTAT_PERTHREAD
  fprintf(stderr, "[CPU%2d] Rounds: %4ldK, "
	  "rx: %3ldK (try: %4ldK), tx: %3ldK (try: %4ldK), "
	  "ps_select: %4ld (rx: %4ld, tx: %4ld, intr: %3ld)\n", 
	  mtcp->ctx->cpu, rs->rounds / ROUND_DIV, 
	  rs->rounds_rx / ROUND_DIV, rs->rounds_rx_try / ROUND_DIV, 
	  rs->rounds_tx / ROUND_DIV, rs->rounds_tx_try / ROUND_DIV, 
	  rs->rounds_select, 
	  rs->rounds_select_rx, rs->rounds_select_tx, rs->rounds_select_intr);
#endif
}
#endif /* ROUND_STAT */
/*----------------------------------------------------------------------------*/
#if TIME_STAT
static inline void PrintThreadRoundTime(mtcp_manager_t mtcp)
{
  fprintf(stderr, "[CPU%2d] Time: (avg, max) "
	  "round: (%4luus, %4luus), processing: (%4luus, %4luus), "
	  "tcheck: (%4luus, %4luus), epoll: (%4luus, %4luus), "
	  "handle: (%4luus, %4luus), xmit: (%4luus, %4luus), "
	  "select: (%4luus, %4luus)\n", mtcp->ctx->cpu, 
	  GetAverageStat(&mtcp->rtstat.round), mtcp->rtstat.round.max, 
	  GetAverageStat(&mtcp->rtstat.processing), mtcp->rtstat.processing.max, 
	  GetAverageStat(&mtcp->rtstat.tcheck), mtcp->rtstat.tcheck.max, 
	  GetAverageStat(&mtcp->rtstat.epoll), mtcp->rtstat.epoll.max, 
	  GetAverageStat(&mtcp->rtstat.handle), mtcp->rtstat.handle.max, 
	  GetAverageStat(&mtcp->rtstat.xmit), mtcp->rtstat.xmit.max, 
	  GetAverageStat(&mtcp->rtstat.select), mtcp->rtstat.select.max);
	
  InitStatCounter(&mtcp->rtstat.round);
  InitStatCounter(&mtcp->rtstat.processing);
  InitStatCounter(&mtcp->rtstat.tcheck);
  InitStatCounter(&mtcp->rtstat.epoll);
  InitStatCounter(&mtcp->rtstat.handle);
  InitStatCounter(&mtcp->rtstat.xmit);
  InitStatCounter(&mtcp->rtstat.select);
}
#endif
#endif /* NETSTAT */
/*----------------------------------------------------------------------------*/
#if EVENT_STAT
static inline void PrintEventStat(int core, struct mtcp_epoll_stat *stat)
{
  fprintf(stderr, "[CPU%2d] calls: %lu, waits: %lu, wakes: %lu, "
	  "issued: %lu, registered: %lu, invalidated: %lu, handled: %lu\n", 
	  core, stat->calls, stat->waits, stat->wakes, 
	  stat->issued, stat->registered, stat->invalidated, stat->handled);
  memset(stat, 0, sizeof(struct mtcp_epoll_stat));
}
#endif /* EVENT_STAT */
/*----------------------------------------------------------------------------*/
#ifdef NETSTAT
static inline void PrintNetworkStats(mtcp_manager_t mtcp, uint32_t cur_ts)
{
#define TIMEOUT 1
  int i;
  struct net_stat ns;
#if ROUND_STAT
  struct run_stat rs;
#endif /* ROUND_STAT */
#ifdef NETSTAT_TOTAL
  int j;
  uint32_t gflow_cnt = 0;
  struct net_stat g_nstat;
#if ROUND_STAT
  struct run_stat g_runstat;
#endif /* ROUND_STAT */
#endif /* NETSTAT_TOTAL */
  
  if (TS_TO_MSEC(cur_ts - mtcp->p_nstat_ts) < SEC_TO_MSEC(TIMEOUT)) {
    return;
  }
  
  mtcp->p_nstat_ts = cur_ts;
  gflow_cnt = 0;
  memset(&g_nstat, 0, sizeof(struct net_stat));
  for (i = 0; i < num_cpus; i++) {
    if (running[i]) {
      PrintThreadNetworkStats(g_mtcp[i], &ns);
#if NETSTAT_TOTAL
      gflow_cnt += g_mtcp[i]->flow_cnt;
      for (j = 0; j < CONFIG.eths_num; j++) {
	g_nstat.rx_packets[j] += ns.rx_packets[j];
	g_nstat.rx_errors[j] += ns.rx_errors[j];
	g_nstat.rx_bytes[j] += ns.rx_bytes[j];
	g_nstat.tx_packets[j] += ns.tx_packets[j];
	g_nstat.tx_drops[j] += ns.tx_drops[j];
	g_nstat.tx_bytes[j] += ns.tx_bytes[j];
      }
#endif
    }
  }
#if NETSTAT_TOTAL
  for (i = 0; i < CONFIG.eths_num; i++) {
    if (CONFIG.eths[i].stat_print) {
      fprintf(stderr, "[ ALL ] xge %d flows: %6u, "
	      "RX: %7ld(pps) (err: %5ld), %5.2lf(Gbps), "
	      "TX: %7ld(pps), %5.2lf(Gbps)\n", i, gflow_cnt, 
	      g_nstat.rx_packets[i], g_nstat.rx_errors[i], 
	      GBPS(g_nstat.rx_bytes[i]), g_nstat.tx_packets[i], 
	      GBPS(g_nstat.tx_bytes[i]));
    }
  }
#endif
  
#if ROUND_STAT
  memset(&g_runstat, 0, sizeof(struct run_stat));
  for (i = 0; i < num_cpus; i++) {
    if (running[i]) {
      PrintThreadRoundStats(g_mtcp[i], &rs);
#if 0
      g_runstat.rounds += rs.rounds;
      g_runstat.rounds_rx += rs.rounds_rx;
      g_runstat.rounds_rx_try += rs.rounds_rx_try;
      g_runstat.rounds_tx += rs.rounds_tx;
      g_runstat.rounds_tx_try += rs.rounds_tx_try;
      g_runstat.rounds_select += rs.rounds_select;
      g_runstat.rounds_select_rx += rs.rounds_select_rx;
      g_runstat.rounds_select_tx += rs.rounds_select_tx;
#endif
    }
  }
#if 0
  fprintf(stderr, "[ ALL ] Rounds: %4ldK, "
	  "rx: %3ldK (try: %4ldK), tx: %3ldK (try: %4ldK), "
	  "ps_select: %4ld (rx: %4ld, tx: %4ld)\n", 
	  g_runstat.rounds / 1000, g_runstat.rounds_rx / 1000, 
	  g_runstat.rounds_rx_try / 1000, g_runstat.rounds_tx / 1000, 
	  g_runstat.rounds_tx_try / 1000, g_runstat.rounds_select, 
	  g_runstat.rounds_select_rx, g_runstat.rounds_select_tx);
#endif
#endif /* ROUND_STAT */
  
#if TIME_STAT
  for (i = 0; i < num_cpus; i++) {
    if (running[i]) {
      PrintThreadRoundTime(g_mtcp[i]);
    }
  }
#endif

#if EVENT_STAT
  for (i = 0; i < num_cpus; i++) {
    if (running[i] && g_mtcp[i]->ep) {
      PrintEventStat(i, &g_mtcp[i]->ep->stat);
    }
  }
#endif 
  fflush(stderr);
}
#endif /* NETSTAT */
/*----------------------------------------------------------------------------*/
#if BLOCKING_SUPPORT
static inline void FlushAcceptEvents(mtcp_manager_t mtcp)
{
  STAT_COUNT(mtcp->runstat.rounds_accept);
  
  pthread_mutex_lock(&mtcp->listener->accept_lock);
  if (!StreamQueueIsEmpty(mtcp->listener->acceptq)) {
    pthread_cond_signal(&mtcp->listener->accept_cond);
  }
  pthread_mutex_unlock(&mtcp->listener->accept_lock);
}
/*----------------------------------------------------------------------------*/
static inline void FlushWriteEvents(mtcp_manager_t mtcp, int thresh)
{
  tcp_stream *walk;
  tcp_stream *next, *last;
  int cnt;
  
  STAT_COUNT(mtcp->runstat.rounds_write);
  
  /* Notify available sending buffer (recovered peer window) */
  cnt = 0;
  walk = TAILQ_FIRST(&mtcp->snd_br_list);
  last = TAILQ_LAST(&mtcp->snd_br_list, snd_br_head);
  while (walk) {
    if (++cnt > thresh) break;    
    next = TAILQ_NEXT(walk, sndvar->snd_br_link);
    TRACE_LOOP("Inside send broadcasting list. cnt: %u\n", cnt);
    TAILQ_REMOVE(&mtcp->snd_br_list, walk, sndvar->snd_br_link);
    mtcp->snd_br_list_cnt--;
    if (walk->on_snd_br_list) {
      TRACE_SNDBUF("Broadcasting available sending buffer!\n");
      if (!(walk->epoll & MTCP_EPOLLOUT)) {
	pthread_cond_signal(&walk->write_cond);
	walk->on_snd_br_list = FALSE;
      }
    }    
    if (walk == last) break;
    walk = next;
  }
}
/*----------------------------------------------------------------------------*/
static inline void FlushReadEvents(mtcp_manager_t mtcp, int thresh)
{	
  tcp_stream *walk;
  tcp_stream *next, *last;
  int cnt;  
  STAT_COUNT(mtcp->runstat.rounds_read);  
  /* Notify receiving event */
  cnt = 0;
  walk = TAILQ_FIRST(&mtcp->rcv_br_list);
  last = TAILQ_LAST(&mtcp->rcv_br_list, rcv_br_head);
  while (walk) {
    if (++cnt > thresh) break;
    next = TAILQ_NEXT(walk, rcvvar->rcv_br_link);
    TRACE_LOOP("Inside recv broadcasting list. cnt: %u\n", cnt);
    TAILQ_REMOVE(&mtcp->rcv_br_list, walk, rcvvar->rcv_br_link);
    mtcp->rcv_br_list_cnt--;
    if (walk->on_rcv_br_list) {
      if (!(walk->epoll & MTCP_EPOLLIN)) {
	TRACE_TEMP("Broadcasting read contition\n");
	pthread_cond_signal(&walk->read_cond);
	walk->on_rcv_br_list = FALSE;
      }
    }
    
    if (walk == last) break;
    walk = next;
  }
}
#endif
/*----------------------------------------------------------------------------*/
static inline void FlushEpollEvents(mtcp_manager_t mtcp, uint32_t cur_ts)
{
  struct mtcp_epoll *ep = mtcp->ep;
  struct event_queue *usrq = ep->usr_queue;
  struct event_queue *mtcpq = ep->mtcp_queue;
  
  pthread_mutex_lock(&ep->epoll_lock);
  if (ep->mtcp_queue->num_events > 0) {
    /* while mtcp_queue have events */
    /* and usr_queue is not full */
    while (mtcpq->num_events > 0 && usrq->num_events < usrq->size) {
      /* copy the event from mtcp_queue to usr_queue */
      usrq->events[usrq->end++] = mtcpq->events[mtcpq->start++];
      
      if (usrq->end >= usrq->size)
	usrq->end = 0;
      usrq->num_events++;
      
      if (mtcpq->start >= mtcpq->size)
	mtcpq->start = 0;
      mtcpq->num_events--;
    }
  }  
  /* if there are pending events, wake up user */
  if (ep->waiting && (ep->usr_queue->num_events > 0 || 
		      ep->usr_shadow_queue->num_events > 0)) {
    STAT_COUNT(mtcp->runstat.rounds_epoll);
    TRACE_EPOLL("Broadcasting events. num: %d, cur_ts: %u, prev_ts: %u\n", 
		ep->usr_queue->num_events, cur_ts, mtcp->ts_last_event);
    mtcp->ts_last_event = cur_ts;
    ep->stat.wakes++;
    pthread_cond_signal(&ep->epoll_cond);
  }
  pthread_mutex_unlock(&ep->epoll_lock);
}
/*----------------------------------------------------------------------------*/
static inline void HandleApplicationCalls(mtcp_manager_t mtcp, uint32_t cur_ts)
{
  tcp_stream *stream;
  int cnt, max_cnt;
  int handled, delayed;
  int control, send, ack;  
  /* connect handling */
  while ((stream = StreamDequeue(mtcp->connectq))) {
    AddtoControlList(mtcp, stream, cur_ts);
  }
  /* send queue handling */
  while ((stream = StreamDequeue(mtcp->sendq))) {
    stream->sndvar->on_sendq = FALSE;
    AddtoSendList(mtcp, stream);
  }
  
  /* ack queue handling */
  while ((stream = StreamDequeue(mtcp->ackq))) {
    stream->sndvar->on_ackq = FALSE;
    EnqueueACK(mtcp, stream, cur_ts, ACK_OPT_AGGREGATE);
  }  
  /* close handling */
  handled = delayed = 0;
  control = send = ack = 0;
  while ((stream = StreamDequeue(mtcp->closeq))) {
    struct tcp_send_vars *sndvar = stream->sndvar;
    sndvar->on_closeq = FALSE;
    
    if (sndvar->sndbuf) {
      sndvar->fss = sndvar->sndbuf->head_seq + sndvar->sndbuf->len;
    } 
    else {
      sndvar->fss = stream->snd_nxt;
    }
    
    if (CONFIG.tcp_timeout > 0)
      RemoveFromTimeoutList(mtcp, stream);
    
    if (stream->have_reset) {
      handled++;
      if (stream->state != TCP_ST_CLOSED) {
	stream->close_reason = TCP_RESET;
	stream->state = TCP_ST_CLOSED;
	TRACE_STATE("Stream %d: TCP_ST_CLOSED\n", stream->id);
	DestroyTCPStream(mtcp, stream);
      } 
      else {
	TRACE_ERROR("Stream already closed.\n");
      }
      
    } 
    else if (sndvar->on_control_list) {
      sndvar->on_closeq_int = TRUE;
      StreamInternalEnqueue(mtcp->closeq_int, stream);
      delayed++;
      if (sndvar->on_control_list)
	control++;
      if (sndvar->on_send_list)
	send++;
      if (sndvar->on_ack_list)
	ack++;
      
    } 
    else if (sndvar->on_send_list || sndvar->on_ack_list) {
      handled++;
      if (stream->state == TCP_ST_ESTABLISHED) {
	stream->state = TCP_ST_FIN_WAIT_1;
	TRACE_STATE("Stream %d: TCP_ST_FIN_WAIT_1\n", stream->id);	
      } else if (stream->state == TCP_ST_CLOSE_WAIT) {
	stream->state = TCP_ST_LAST_ACK;
	TRACE_STATE("Stream %d: TCP_ST_LAST_ACK\n", stream->id);
      }
      stream->control_list_waiting = TRUE;
      
    } 
    else if (stream->state != TCP_ST_CLOSED) {
      handled++;
      if (stream->state == TCP_ST_ESTABLISHED) {
	stream->state = TCP_ST_FIN_WAIT_1;
	TRACE_STATE("Stream %d: TCP_ST_FIN_WAIT_1\n", stream->id);
	
      } 
      else if (stream->state == TCP_ST_CLOSE_WAIT) {
	stream->state = TCP_ST_LAST_ACK;
	TRACE_STATE("Stream %d: TCP_ST_LAST_ACK\n", stream->id);
      }
      //sndvar->rto = TCP_FIN_RTO;
      //UpdateRetransmissionTimer(mtcp, stream, mtcp->cur_ts);
      AddtoControlList(mtcp, stream, cur_ts);
    } 
    else {
      TRACE_ERROR("Already closed connection!\n");
    }
  }
  TRACE_ROUND("Handling close connections. cnt: %d\n", cnt);
  
  cnt = 0;
  max_cnt = mtcp->closeq_int->count;
  while (cnt++ < max_cnt) {
    stream = StreamInternalDequeue(mtcp->closeq_int);
    
    if (stream->sndvar->on_control_list) {
      StreamInternalEnqueue(mtcp->closeq_int, stream);
      
    } 
    else if (stream->state != TCP_ST_CLOSED) {
      handled++;
      stream->sndvar->on_closeq_int = FALSE;
      if (stream->state == TCP_ST_ESTABLISHED) {
	stream->state = TCP_ST_FIN_WAIT_1;
	TRACE_STATE("Stream %d: TCP_ST_FIN_WAIT_1\n", stream->id);
	
      } 
      else if (stream->state == TCP_ST_CLOSE_WAIT) {
	stream->state = TCP_ST_LAST_ACK;
	TRACE_STATE("Stream %d: TCP_ST_LAST_ACK\n", stream->id);
      }
      AddtoControlList(mtcp, stream, cur_ts);
    } 
    else {
      stream->sndvar->on_closeq_int = FALSE;
      TRACE_ERROR("Already closed connection!\n");
    }
  }
  
  /* reset handling */
  while ((stream = StreamDequeue(mtcp->resetq))) {
    stream->sndvar->on_resetq = FALSE;
    
    if (CONFIG.tcp_timeout > 0)
      RemoveFromTimeoutList(mtcp, stream);
    
    if (stream->have_reset) {
      if (stream->state != TCP_ST_CLOSED) {
	stream->close_reason = TCP_RESET;
	stream->state = TCP_ST_CLOSED;
	TRACE_STATE("Stream %d: TCP_ST_CLOSED\n", stream->id);
	DestroyTCPStream(mtcp, stream);
      } 
      else {
	TRACE_ERROR("Stream already closed.\n");
      }      
    } 
    else if (stream->sndvar->on_control_list || 
	     stream->sndvar->on_send_list || stream->sndvar->on_ack_list) {
      /* wait until all the queues are flushed */
      stream->sndvar->on_resetq_int = TRUE;
      StreamInternalEnqueue(mtcp->resetq_int, stream);
      
    } 
    else {
      if (stream->state != TCP_ST_CLOSED) {
	stream->close_reason = TCP_ACTIVE_CLOSE;
	stream->state = TCP_ST_CLOSED;
	TRACE_STATE("Stream %d: TCP_ST_CLOSED\n", stream->id);
	AddtoControlList(mtcp, stream, cur_ts);
      } 
      else {
	TRACE_ERROR("Stream already closed.\n");
      }
    }
  }
  TRACE_ROUND("Handling reset connections. cnt: %d\n", cnt);
  
  cnt = 0;
  max_cnt = mtcp->resetq_int->count;
  while (cnt++ < max_cnt) {
    stream = StreamInternalDequeue(mtcp->resetq_int);
    
    if (stream->sndvar->on_control_list || 
	stream->sndvar->on_send_list || stream->sndvar->on_ack_list) {
      /* wait until all the queues are flushed */
      StreamInternalEnqueue(mtcp->resetq_int, stream);
      
    } 
    else {
      stream->sndvar->on_resetq_int = FALSE;

      if (stream->state != TCP_ST_CLOSED) {
	stream->close_reason = TCP_ACTIVE_CLOSE;
	stream->state = TCP_ST_CLOSED;
	TRACE_STATE("Stream %d: TCP_ST_CLOSED\n", stream->id);
	AddtoControlList(mtcp, stream, cur_ts);
      } 
      else {
	TRACE_ERROR("Stream already closed.\n");
      }
    }
  }
  
  /* destroy streams in destroyq */
  while ((stream = StreamDequeue(mtcp->destroyq))) {
    DestroyTCPStream(mtcp, stream);
  }
  
  mtcp->wakeup_flag = FALSE;
}
/*----------------------------------------------------------------------------*/
static inline void WritePacketsToChunks(mtcp_manager_t mtcp, uint32_t cur_ts)
{
  int thresh = CONFIG.max_concurrency;
  int i;

  //  printf("Write packets to chunks\n");
  /* Set the threshold to CONFIG.max_concurrency to send ACK immediately */
  /* Otherwise, set to appropriate value (e.g. thresh) */
  assert(mtcp->g_sender != NULL);
  if (mtcp->g_sender->control_list_cnt)
    WriteTCPControlList(mtcp, mtcp->g_sender, cur_ts, thresh);
  if (mtcp->g_sender->ack_list_cnt)
    WriteTCPACKList(mtcp, mtcp->g_sender, cur_ts, thresh);
  if (mtcp->g_sender->send_list_cnt)
    WriteTCPDataList(mtcp, mtcp->g_sender, cur_ts, thresh);
  
  for (i = 0; i < CONFIG.eths_num; i++) {
    assert(mtcp->n_sender[i] != NULL);
    if (mtcp->n_sender[i]->control_list_cnt)
      WriteTCPControlList(mtcp, mtcp->n_sender[i], cur_ts, thresh);
    if (mtcp->n_sender[i]->ack_list_cnt)
      WriteTCPACKList(mtcp, mtcp->n_sender[i], cur_ts, thresh);
    if (mtcp->n_sender[i]->send_list_cnt)
      WriteTCPDataList(mtcp, mtcp->n_sender[i], cur_ts, thresh);
  }
}
/*----------------------------------------------------------------------------*/
#if TESTING
static int DestroyRemainingFlows(mtcp_manager_t mtcp)
{
  struct hashtable *ht = mtcp->tcp_flow_table;
  tcp_stream *walk;
  int cnt, i;
  
  cnt = 0;
#if 0
  thread_printf(mtcp, mtcp->log_fp, 
		"CPU %d: Flushing remaining flows.\n", mtcp->ctx->cpu);
#endif
  for (i = 0; i < NUM_BINS; i++) {
    TAILQ_FOREACH(walk, &ht->ht_table[i], rcvvar->he_link) {
#if 0
      thread_printf(mtcp, mtcp->log_fp, 
		    "CPU %d: Destroying stream %d\n", mtcp->ctx->cpu, walk->id);
      DumpStream(mtcp, walk);
#endif
      DestroyTCPStream(mtcp, walk);
      cnt++;
    }
  }
  
  return cnt;
}
#endif
/*----------------------------------------------------------------------------*/
static void InterruptApplication(mtcp_manager_t mtcp)
{
  //printf("interrupt application\n");
  /* interrupt if the mtcp_epoll_wait() is waiting */
  if (mtcp->ep) {
    pthread_mutex_lock(&mtcp->ep->epoll_lock);
    if (mtcp->ep->waiting) {
      pthread_cond_signal(&mtcp->ep->epoll_cond);
    }
    pthread_mutex_unlock(&mtcp->ep->epoll_lock);
  }
  /* interrupt if the accept() is waiting */
  if (mtcp->listener) {
    //printf("Notify accept\n");
    if (mtcp->listener->socket) {
      pthread_mutex_lock(&mtcp->listener->accept_lock);
      //printf("Notify accept\n");
      if (!(mtcp->listener->socket->opts & MTCP_NONBLOCK)) {
	pthread_cond_signal(&mtcp->listener->accept_cond);
      }
      pthread_mutex_unlock(&mtcp->listener->accept_lock);
    }
  }
}
/*----------------------------------------------------------------------------*/
static void RunMainLoop(struct mtcp_thread_context *ctx)
{
  int ret, i, idx;
  mtcp_manager_t mtcp = ctx->mtcp_manager;
  int recv_cnt;
  struct rte_mbuf *rx_mbufs[MAX_PKT_BURST];
  struct timeval cur_ts = {0};
  uint32_t ts, ts_prev;
  time_t t;
  struct tm *now;
  char tbuf[64];

#if TIME_STAT
  struct timeval prev_ts, processing_ts, tcheck_ts, 
    epoll_ts, handle_ts, xmit_ts, select_ts;
#endif
  int thresh;
  // chunk.recv_blocking = 0;
  gettimeofday(&cur_ts, NULL);  
  t = cur_ts.tv_sec;
  now = localtime(&t);
  strftime(tbuf, sizeof tbuf, "%Y-%m-%d %H:%M%S", now);
  printf("CPU %d: TCPMainLoop: starts at %s\n", ctx->cpu, tbuf);
  /* create packet write chunk */
  InitWriteChunks(ctx->tx_pool, ctx->w_chunk);
  for (i = 0; i < num_devices_attached; i++) {
    idx = devices_attached[i];
    ctx->w_chunk[idx].cnt = 0;
    // ctx->w_off[idx] = 0;
    ctx->w_cur_idx[idx] = 0;
  }  
  //printf("CPU %d: mtcp thread running.\n", ctx->cpu);
  
#if TIME_STAT
  prev_ts = cur_ts;
  InitStatCounter(&mtcp->rtstat.round);
  InitStatCounter(&mtcp->rtstat.processing);
  InitStatCounter(&mtcp->rtstat.tcheck);
  InitStatCounter(&mtcp->rtstat.epoll);
  InitStatCounter(&mtcp->rtstat.handle);
  InitStatCounter(&mtcp->rtstat.xmit);
  InitStatCounter(&mtcp->rtstat.select);
#endif

  ts = ts_prev = 0;
  while ((!ctx->done || mtcp->flow_cnt) && !ctx->exit) {    
    STAT_COUNT(mtcp->runstat.rounds);
    recv_cnt = 0;
    gettimeofday(&cur_ts, NULL);
    ts = TIMEVAL_TO_TS(&cur_ts);
    mtcp->cur_ts = ts;
    /* Read packets into rx_mbufs from NIC */    
    //printf("Read packets into rx_mbufs from NIC\n");
    STAT_COUNT(mtcp->runstat.rounds_rx_try);
    // TODO: receive packets from multiple queues
    for (i = 0; i < num_devices_attached; i++) {
      idx = devices_attached[i];      
      recv_cnt = rte_eth_rx_burst(idx, 0 /* queue_id*/, 
				  rx_mbufs, MAX_PKT_BURST);
      if (recv_cnt < 0) {
	if (errno != EAGAIN && errno != EINTR) {
	  perror("rte_eth_rx_burst()");
	  assert(0);
	}
      }
      //if ( recv_cnt > 0)
      //  printf("TCP received %d packets from NIC %d\n", recv_cnt, idx);

      for (i = 0; i < recv_cnt; i++) {
	//printf("Process packet %d\n", i);
	ProcessPacket(mtcp, idx, ts, 
		      (u_char *)rte_pktmbuf_mtod(rx_mbufs[i], u_char *), 
		      rte_pktmbuf_data_len(rx_mbufs[i]));
	rte_pktmbuf_free(rx_mbufs[i]);
      }
      if (recv_cnt > 0) STAT_COUNT(mtcp->runstat.rounds_rx);
    
#if TIME_STAT
      gettimeofday(&processing_ts, NULL);
      UpdateStatCounter(&mtcp->rtstat.processing, 
			TimeDiffUs(&processing_ts, &cur_ts));
#endif /* TIME_STAT */

      /* interaction with application */
      if (mtcp->flow_cnt > 0) {
	/* check retransmission timeout and timewait expire */
#if 0
	thresh = (int)mtcp->flow_cnt / (TS_TO_USEC(PER_STREAM_TCHECK));
	assert(thresh >= 0);
	if (thresh == 0) thresh = 1;
	if (recv_cnt > 0 && thresh > recv_cnt) thresh = recv_cnt;
#endif      
	thresh = CONFIG.max_concurrency;
      
	/* Eunyoung, you may fix this later 
	 * if there is no rcv packet, we will send as much as possible
	 */
	if (thresh == -1) thresh = CONFIG.max_concurrency;
      
	CheckRtmTimeout(mtcp, ts, thresh);
	CheckTimewaitExpire(mtcp, ts, CONFIG.max_concurrency);
      
	if (CONFIG.tcp_timeout > 0 && ts != ts_prev) {
	  CheckConnectionTimeout(mtcp, ts, thresh);
	}
#if TIME_STAT
      }
    
      gettimeofday(&tcheck_ts, NULL);
      UpdateStatCounter(&mtcp->rtstat.tcheck, 
			TimeDiffUs(&tcheck_ts, &processing_ts));
      
      if (mtcp->flow_cnt > 0) {
#endif /* TIME_STAT */
      
#if BLOCKING_SUPPORT
	/* notify accept events if there is incoming connection */
	if (mtcp->listener) {
	  FlushAcceptEvents(mtcp);
	}      
	thresh = (int)mtcp->flow_cnt / (TS_TO_USEC(PER_STREAM_SLICE));
	if (thresh == 0) thresh = 1;
	if (recv_cnt > 0 && thresh > recv_cnt) thresh = recv_cnt;
	/* notify read/write events to application */
	FlushWriteEvents(mtcp, thresh);
	FlushReadEvents(mtcp, thresh);
#endif
      }    
      /* if epoll is in use, flush all the queued events */
      if (mtcp->ep) {
	//printf("Flush EPOLL events\n");
	FlushEpollEvents(mtcp, ts);
      }
#if TIME_STAT
      gettimeofday(&epoll_ts, NULL);
      UpdateStatCounter(&mtcp->rtstat.epoll, 
			TimeDiffUs(&epoll_ts, &tcheck_ts));
#endif /* TIME_STAT */    
      /* hadnle stream queues  */
      if (mtcp->flow_cnt > 0) HandleApplicationCalls(mtcp, ts);
        
#if TIME_STAT
      gettimeofday(&handle_ts, NULL);
      UpdateStatCounter(&mtcp->rtstat.handle, 
			TimeDiffUs(&handle_ts, &epoll_ts));
#endif /* TIME_STAT */
    } /* for (num_devices_attached) */    
    WritePacketsToChunks(mtcp, ts);    
    /* send packets from write buffer */
    /* try send chunks immediately */
    for (i = 0; i < CONFIG.eths_num; i++) {
      ret = FlushWriteBuffer(ctx, i);
      if (ret < 0) {
	TRACE_ERROR("Failed to send chunks.\n");
      } 
      else if (ret > 0) {
	STAT_COUNT(mtcp->runstat.rounds_tx);
      }
    }
    
#if TIME_STAT
    gettimeofday(&xmit_ts, NULL);
    UpdateStatCounter(&mtcp->rtstat.xmit, 
		      TimeDiffUs(&xmit_ts, &handle_ts));
#endif /* TIME_STAT */
    
    if (ts != ts_prev) {
      ts_prev = ts;
#ifdef NETSTAT
      if (ctx->cpu == printer) {
	PrintNetworkStats(mtcp, ts);
      }
#endif /* NETSTAT */
    }
    if (ctx->interrupt) {
      //printf("Notify application\n");
      InterruptApplication(mtcp);
    }
  }
  
#if TESTING
  DestroyRemainingFlows(mtcp);
#endif

  TRACE_DBG("MTCP thread %d out of main loop.\n", ctx->cpu);
  /* flush logs */
  flush_log_data(mtcp);
  TRACE_DBG("MTCP thread %d flushed logs.\n", ctx->cpu);
  InterruptApplication(mtcp);
  TRACE_INFO("MTCP thread %d finished.\n", ctx->cpu);
}
/*----------------------------------------------------------------------------*/
struct mtcp_sender *CreateMTCPSender(int ifidx)
{
  struct mtcp_sender *sender;
  
  sender = (struct mtcp_sender *)rte_calloc("mtcp_sender", 1, sizeof(struct mtcp_sender), 0);
  if (!sender) return NULL;
    
  sender->ifidx = ifidx;
  
  TAILQ_INIT(&sender->control_list);
  TAILQ_INIT(&sender->send_list);
  TAILQ_INIT(&sender->ack_list);
  
  sender->control_list_cnt = 0;
  sender->send_list_cnt = 0;
  sender->ack_list_cnt = 0;
  
  return sender;
}
/*----------------------------------------------------------------------------*/
void DestroyMTCPSender(struct mtcp_sender *sender)
{
  rte_free(sender);
}
/*----------------------------------------------------------------------------*/
static mtcp_manager_t InitializeMTCPManager(struct mtcp_thread_context* ctx)
{
  mtcp_manager_t mtcp;
  char log_name[MAX_FILE_NAME];
  int i;
  
  mtcp = (mtcp_manager_t)rte_calloc("mtcp_manager", 1, sizeof(struct mtcp_manager), 0);
  if (!mtcp) {
    perror("malloc");
    CTRACE_ERROR("Failed to allocate mtcp_manager.\n");
    return NULL;
  }
  g_mtcp[ctx->cpu] = mtcp;
  
  mtcp->tcp_flow_table = CreateHashtable(HashFlow, EqualFlow);
  if (!mtcp->tcp_flow_table) {
    CTRACE_ERROR("Falied to allocate tcp flow table.\n");
    return NULL;
  }

#ifdef HUGEPAGE
#define	IS_HUGEPAGE 1
#else
#define	IS_HUGEPAGE 0
#endif

  mtcp->flow_pool = MPCreate(sizeof(tcp_stream),
			     sizeof(tcp_stream) * CONFIG.max_concurrency, IS_HUGEPAGE);
  if (!mtcp->flow_pool) {
    CTRACE_ERROR("Failed to allocate tcp flow pool.\n");
    return NULL;
  }
  mtcp->rv_pool = MPCreate(sizeof(struct tcp_recv_vars), 
			   sizeof(struct tcp_recv_vars) * CONFIG.max_concurrency, IS_HUGEPAGE);
  if (!mtcp->rv_pool) {
    CTRACE_ERROR("Failed to allocate tcp recv variable pool.\n");
    return NULL;
  }
  mtcp->sv_pool = MPCreate(sizeof(struct tcp_send_vars), 
			   sizeof(struct tcp_send_vars) * CONFIG.max_concurrency, IS_HUGEPAGE);
  if (!mtcp->sv_pool) {
    CTRACE_ERROR("Failed to allocate tcp send variable pool.\n");
    return NULL;
  }
  
  mtcp->rbm_snd = SBManagerCreate(CONFIG.sndbuf_size, CONFIG.max_num_buffers);
  if (!mtcp->rbm_snd) {
    CTRACE_ERROR("Failed to create send ring buffer.\n");
    return NULL;
  }
  mtcp->rbm_rcv = RBManagerCreate(CONFIG.rcvbuf_size, CONFIG.max_num_buffers);
  if (!mtcp->rbm_rcv) {
    CTRACE_ERROR("Failed to create recv ring buffer.\n");
    return NULL;
  }
  
  mtcp->smap = (socket_map_t)rte_calloc("socket_map", CONFIG.max_concurrency, sizeof(struct socket_map), 0);
  if (!mtcp->smap) {
    perror("calloc");
    CTRACE_ERROR("Failed to allocate memory for stream map.\n");
    return NULL;
  }
  TAILQ_INIT(&mtcp->free_smap);
  for (i = 0; i < CONFIG.max_concurrency; i++) {
    mtcp->smap[i].id = i;
    mtcp->smap[i].socktype = MTCP_SOCK_UNUSED;
    memset(&mtcp->smap[i].saddr, 0, sizeof(struct sockaddr_in));
    mtcp->smap[i].stream = NULL;
    TAILQ_INSERT_TAIL(&mtcp->free_smap, &mtcp->smap[i], free_smap_link);
  }
  
  mtcp->ctx = ctx;
  mtcp->ep = NULL;
  
  snprintf(log_name, MAX_FILE_NAME, LOG_FILE_NAME"_%d", ctx->cpu);
  mtcp->log_fp = fopen(log_name, "w");
  if (!mtcp->log_fp) {
    perror("fopen");
    CTRACE_ERROR("Failed to create file for logging.\n");
    return NULL;
  }
  mtcp->sp_fd = g_logctx[ctx->cpu]->pair_sp_fd;
  mtcp->logger = g_logctx[ctx->cpu];
  
  mtcp->connectq = CreateStreamQueue(BACKLOG_SIZE);
  if (!mtcp->connectq) {
    CTRACE_ERROR("Failed to create connect queue.\n");
    return NULL;
  }
  mtcp->sendq = CreateStreamQueue(CONFIG.max_concurrency);
  if (!mtcp->sendq) {
    CTRACE_ERROR("Failed to create send queue.\n");
    return NULL;
  }
  mtcp->ackq = CreateStreamQueue(CONFIG.max_concurrency);
  if (!mtcp->ackq) {
    CTRACE_ERROR("Failed to create ack queue.\n");
    return NULL;
  }
  mtcp->closeq = CreateStreamQueue(CONFIG.max_concurrency);
  if (!mtcp->closeq) {
    CTRACE_ERROR("Failed to create close queue.\n");
    return NULL;
  }
  mtcp->closeq_int = CreateInternalStreamQueue(CONFIG.max_concurrency);
  if (!mtcp->closeq_int) {
    CTRACE_ERROR("Failed to create close queue.\n");
    return NULL;
  }
  mtcp->resetq = CreateStreamQueue(CONFIG.max_concurrency);
  if (!mtcp->resetq) {
    CTRACE_ERROR("Failed to create reset queue.\n");
    return NULL;
  }
  mtcp->resetq_int = CreateInternalStreamQueue(CONFIG.max_concurrency);
  if (!mtcp->resetq_int) {
    CTRACE_ERROR("Failed to create reset queue.\n");
    return NULL;
  }
  mtcp->destroyq = CreateStreamQueue(CONFIG.max_concurrency);
  if (!mtcp->destroyq) {
    CTRACE_ERROR("Failed to create destroy queue.\n");
    return NULL;
  }
  
  mtcp->g_sender = CreateMTCPSender(-1);
  if (!mtcp->g_sender) {
    CTRACE_ERROR("Failed to create global sender structure.\n");
    return NULL;
  }
  for (i = 0; i < CONFIG.eths_num; i++) {
    mtcp->n_sender[i] = CreateMTCPSender(i);
    if (!mtcp->n_sender[i]) {
      CTRACE_ERROR("Failed to create per-nic sender structure.\n");
      return NULL;
    }
  }
  
  mtcp->rto_store = InitRTOHashstore();
  TAILQ_INIT(&mtcp->timewait_list);
  TAILQ_INIT(&mtcp->timeout_list);
  
#if BLOCKING_SUPPORT
  TAILQ_INIT(&mtcp->rcv_br_list);
  TAILQ_INIT(&mtcp->snd_br_list);
#endif
  
  return mtcp;
}
/*----------------------------------------------------------------------------*/
static void *MTCPRunThread(void *arg)
{
  mctx_t mctx = (mctx_t)arg;
  int cpu = mctx->cpu;
  int working;
  struct mtcp_manager *mtcp;
  struct mtcp_thread_context *ctx;
  
  /* affinitize the thread to this core first */
  //mtcp_core_affinitize(cpu);
  
  /* memory alloc after core affinitization would use local memory
     most time */
  ctx = rte_calloc("mtcp_thread_context", 1, sizeof(*ctx), 0);
  if (!ctx) {
    perror("calloc");
    TRACE_ERROR("Failed to calloc mtcp context.\n");
    exit(-1);
  }
  ctx->thread = pthread_self();  
  ctx->cpu = cpu;

  /* Initialize TX and RX pools */
  ctx->rx_pool = rte_mempool_create("rx_pool", 
				    POOL_SIZE * num_devices_attached, 
				    MAX_PKT_SIZE, CACHE_SIZE,
				    sizeof (struct rte_pktmbuf_pool_private),
				    rte_pktmbuf_pool_init, NULL,
				    rte_pktmbuf_init, NULL, cpu, 0);
  if (ctx->rx_pool == NULL) {
    rte_exit(EXIT_FAILURE, "rte_mempool_create(): error\n");
  }

  ctx->tx_pool = rte_mempool_create("tx_pool", POOL_SIZE * num_devices_attached, 
				    MAX_PKT_SIZE, CACHE_SIZE,
			   sizeof (struct rte_pktmbuf_pool_private),
				    rte_pktmbuf_pool_init, NULL,
				    rte_pktmbuf_init, NULL, cpu, 0);
  if (ctx->tx_pool == NULL) { 
    rte_exit(EXIT_FAILURE, "rte_mempool_create(): error\n");
  }

  mtcp = ctx->mtcp_manager = InitializeMTCPManager(ctx);
  if (!mtcp) {
    TRACE_ERROR("Failed to initialize mtcp manager.\n");
    exit(-1);
  }
  
  if (pthread_mutex_init(&ctx->smap_lock, NULL)) {
    perror("pthread_mutex_init ctx->smap_lock");
    exit(-1);
  }
  
  if (pthread_mutex_init(&ctx->flow_pool_lock, NULL)) {
    perror("pthread_mutex_init of ctx->flow_pool_lock\n");
    exit(-1);
  }
  
  if (pthread_mutex_init(&ctx->socket_pool_lock, NULL)) {
    perror("pthread_mutex_init of ctx->socket_pool_lock\n");
    exit(-1);
  }
  
  SQ_LOCK_INIT(&ctx->connect_lock, "ctx->connect_lock", exit(-1));
  SQ_LOCK_INIT(&ctx->close_lock, "ctx->close_lock", exit(-1));
  SQ_LOCK_INIT(&ctx->reset_lock, "ctx->reset_lock", exit(-1));
  SQ_LOCK_INIT(&ctx->sendq_lock, "ctx->sendq_lock", exit(-1));
  SQ_LOCK_INIT(&ctx->ackq_lock, "ctx->ackq_lock", exit(-1));
  SQ_LOCK_INIT(&ctx->destroyq_lock, "ctx->destroyq_lock", exit(-1));
  
  /* remember this context pointer for signal processing */
  //printf("Attach devices\n");
  g_pctx[cpu] = ctx;
  mlockall(MCL_CURRENT);  
  // attach (nic device, queue)
  working = AttachDevice(ctx);
  if (working != 0) {
    perror("attach");
    return NULL;
  }  
  TRACE_DBG("CPU %d: initialization finished.\n", cpu);  
  sem_post(&g_init_sem[ctx->cpu]);  
  /* start the main loop */
  RunMainLoop(ctx);
  
  TRACE_DBG("MTCP thread %d finished.\n", ctx->cpu);
  
  return NULL;
}
/*----------------------------------------------------------------------------*/
mctx_t mtcp_create_context(int cpu)
{
  mctx_t mctx;
  int ret;
  
  ret = sem_init(&g_init_sem[cpu], 0, 0);
  if (ret) {
    TRACE_ERROR("Failed initialize init_sem.\n");
    return NULL;
  }
  
  mctx = (mctx_t)rte_calloc("mctx", 1, sizeof(struct mtcp_context), 0);
  if (!mctx) {
    TRACE_ERROR("Failed to allocate memory for mtcp_context.\n");
    return NULL;
  }
  mctx->cpu = cpu;
  //printf("Create logger thread\n");
  /* initialize logger */
  g_logctx[cpu] = (struct log_thread_context *)
    rte_calloc("log_thread_context", 1, sizeof(struct log_thread_context), 0);
  if (!g_logctx[cpu]) {
    perror("malloc");
    TRACE_ERROR("Failed to allocate memory for log thread context.\n");
    return NULL;
  }
  
  InitLogThreadContext(g_logctx[cpu], cpu);
  if (pthread_create(&log_thread[cpu], NULL, ThreadLogMain, (void *)g_logctx[cpu])) {
    perror("pthread_create");
    TRACE_ERROR("Failed to create log thread\n");
    return NULL;
  }
  

  //printf("Create TCP thread\n");
  if (pthread_create(&g_thread[cpu], NULL, MTCPRunThread, (void *)mctx) != 0) {
    TRACE_ERROR("pthread_create of mtcp thread failed!\n");
    return NULL;
  }
  
  sem_wait(&g_init_sem[cpu]);
  sem_destroy(&g_init_sem[cpu]);
  
  running[cpu] = TRUE;
  
#ifdef NETSTAT
#if NETSTAT_TOTAL
  if (printer < 0) {
    printer = cpu;
    TRACE_DBG("CPU %d is in charge of printing stats.\n", printer);
  }
#endif
#endif
		
  return mctx;
}
/*----------------------------------------------------------------------------*/
void mtcp_destroy_context(mctx_t mctx)
{
  struct mtcp_thread_context *ctx = g_pctx[mctx->cpu];
  struct mtcp_manager *mtcp = ctx->mtcp_manager;
  struct log_thread_context *log_ctx = mtcp->logger;
  int i; //ret, i;
  
  TRACE_DBG("CPU %d: mtcp_destroy_context()\n", mctx->cpu);
  
  /* close all stream sockets that are still open */
  if (!ctx->exit) {
    for (i = 0; i < CONFIG.max_concurrency; i++) {
      if (mtcp->smap[i].socktype == MTCP_SOCK_STREAM) {
	TRACE_DBG("Closing remaining socket %d (%s)\n", 
		  i, TCPStateToString(mtcp->smap[i].stream));
	DumpStream(mtcp, mtcp->smap[i].stream);
	mtcp_close(mctx, i);
      }
    }
  }
  
  ctx->done = 1;
  
  //pthread_kill(g_thread[mctx->cpu], SIGINT);
  pthread_join(g_thread[mctx->cpu], NULL);
  
  TRACE_INFO("MTCP thread %d joined.\n", mctx->cpu);
  running[mctx->cpu] = FALSE;
  
#ifdef NETSTAT
#if NETSTAT_TOTAL
  if (printer == mctx->cpu) {
    for (i = 0; i < num_cpus; i++) {
      if (i != mctx->cpu && running[i]) {
	printer = i;
	break;
      }
    }
  }
#endif
#endif
  
  log_ctx->done = 1;
  assert(write(log_ctx->pair_sp_fd, "F", 1) == 1);
  //  assert(ret == 1);
  pthread_join(log_thread[ctx->cpu], NULL);
  fclose(mtcp->log_fp);
  TRACE_LOG("Log thread %d joined.\n", mctx->cpu);
  
  if (mtcp->connectq) {
    DestroyStreamQueue(mtcp->connectq);
    mtcp->connectq = NULL;
  }
  if (mtcp->sendq) {
    DestroyStreamQueue(mtcp->sendq);
    mtcp->sendq = NULL;
  }
  if (mtcp->ackq) {
    DestroyStreamQueue(mtcp->ackq);
    mtcp->ackq = NULL;
  }
  if (mtcp->closeq) {
    DestroyStreamQueue(mtcp->closeq);
    mtcp->closeq = NULL;
  }
  if (mtcp->closeq_int) {
    DestroyInternalStreamQueue(mtcp->closeq_int);
    mtcp->closeq_int = NULL;
  }
  if (mtcp->resetq) {
    DestroyStreamQueue(mtcp->resetq);
    mtcp->resetq = NULL;
  }
  if (mtcp->resetq_int) {
    DestroyInternalStreamQueue(mtcp->resetq_int);
    mtcp->resetq_int = NULL;
  }
  if (mtcp->destroyq) {
    DestroyStreamQueue(mtcp->destroyq);
    mtcp->destroyq = NULL;
  }
  
  DestroyMTCPSender(mtcp->g_sender);
  for (i = 0; i < CONFIG.eths_num; i++) {
    DestroyMTCPSender(mtcp->n_sender[i]);
  }
  
  MPDestroy(mtcp->rv_pool);
  MPDestroy(mtcp->sv_pool);
  MPDestroy(mtcp->flow_pool);
  
  if (mtcp->ap) {
    DestroyAddressPool(mtcp->ap);
  }
  
  SQ_LOCK_DESTROY(&ctx->connect_lock);
  SQ_LOCK_DESTROY(&ctx->close_lock);
  SQ_LOCK_DESTROY(&ctx->reset_lock);
  SQ_LOCK_DESTROY(&ctx->sendq_lock);
  SQ_LOCK_DESTROY(&ctx->ackq_lock);
  SQ_LOCK_DESTROY(&ctx->destroyq_lock);
  
  TRACE_INFO("MTCP thread %d destroyed.\n", mctx->cpu);
  //free(ctx->handle);
  rte_free(ctx);
  rte_free(mctx);
}
/*----------------------------------------------------------------------------*/
mtcp_sighandler_t mtcp_register_signal(int signum, mtcp_sighandler_t handler)
{
  mtcp_sighandler_t prev;
  
  if (signum == SIGINT) {
    prev = app_signal_handler;
    app_signal_handler = handler;
  } 
else {
  if ((prev = signal(signum, handler)) == SIG_ERR) {
    perror("signal");
    return SIG_ERR;
  }
 }
  
  return prev;
}
/*----------------------------------------------------------------------------*/
int mtcp_getconf(struct mtcp_conf *conf)
{
  if (!conf)
    return -1;
  
  conf->num_cores = CONFIG.num_cores;
  conf->max_concurrency = CONFIG.max_concurrency;
  
  conf->max_num_buffers = CONFIG.max_num_buffers;
  conf->rcvbuf_size = CONFIG.rcvbuf_size;
  conf->sndbuf_size = CONFIG.sndbuf_size;
  
  conf->tcp_timewait = CONFIG.tcp_timewait;
  conf->tcp_timeout = CONFIG.tcp_timeout;
  
  return 0;
}
/*----------------------------------------------------------------------------*/
int mtcp_setconf(const struct mtcp_conf *conf)
{
  if (!conf) return -1;
  
  CONFIG.num_cores = conf->num_cores;
  CONFIG.max_concurrency = conf->max_concurrency;
  
  CONFIG.max_num_buffers = conf->max_num_buffers;
  CONFIG.rcvbuf_size = conf->rcvbuf_size;
  CONFIG.sndbuf_size = conf->sndbuf_size;
  
  CONFIG.tcp_timewait = conf->tcp_timewait;
  CONFIG.tcp_timeout = conf->tcp_timeout;
  
  TRACE_CONFIG("Configuration updated by mtcp_setconf().\n");
  PrintConfiguration();
  
  return 0;
}
/*----------------------------------------------------------------------------*/
int mtcp_init(const char *config_file)
{
  int i;
  int ret;
  
  if (geteuid()) {
    TRACE_CONFIG("[CAUTION] Run as root if mlock is necessary.\n");
  }
  
  /* getting cpu and NIC */
  /* FIXME: num_cpus = GetNumCPUs(); */
  num_cpus = 1;
  assert(num_cpus >= 1);
  for (i = 0; i < num_cpus; i++) {
    g_mtcp[i] = NULL;
    running[i] = FALSE;
    sigint_cnt[i] = 0;
  }
  //printf("mtcp_init(): get # of dpdk enabled devices\n");
  //  num_devices = ps_list_devices(devices);
  num_devices = rte_eth_dev_count();
  if (num_devices == 0) {
    rte_exit(EXIT_FAILURE, "rte_eth_dev_count(): error\n");
  }
  //printf("mtcp_init(): found %d dpdk enabled devices\nLoad mTCP configuration.", num_devices);
  ret = LoadConfiguration(config_file);
  if (ret != 0) {
    TRACE_CONFIG("Error occured while loading configuration.\n");
    return -1;
  }
  PrintConfiguration();
  /* SetInterfaceInfo requires IP address info, which comes from configuration */
  ret = SetInterfaceInfo();
  if (ret != 0) {
    TRACE_CONFIG("Error occured while setting interface configuration.\n");
    return -1;
  }
  PrintInterfaceInfo();    
  /* TODO: this should be fixed */
  ap = CreateAddressPool(CONFIG.eths[0].ip_addr, 1);
  if (!ap) {
    TRACE_CONFIG("Error occured while creating address pool.\n");
    return -1;
  }
  ret = SetRoutingTable();
  if (ret) {
    TRACE_CONFIG("Error occured while loading routing table.\n");
    return -1;
  }
  PrintRoutingTable();
  
  LoadARPTable();
  PrintARPTable();

  //printf("Signal TCP thread SIGUSR1\n");
  if (signal(SIGUSR1, HandleSignal) == SIG_ERR) {
    perror("signal, SIGUSR1");
    return -1;
  }
  //printf("Signal TCP thread SIGINT\n");
  if (signal(SIGINT, HandleSignal) == SIG_ERR) {
    perror("signal, SIGINT");
    return -1;
  }
  app_signal_handler = NULL;
  
  return 0;
}
/*----------------------------------------------------------------------------*/
void mtcp_destroy()
{
  int i;
  
  /* wait until all threads are closed */
  for (i = 0; i < num_cpus; i++) {
    if (running[i]) {
      pthread_join(g_thread[i], NULL);
    }
  }
  
  DestroyAddressPool(ap);
  
  TRACE_INFO("All MTCP threads are joined.\n");
}
/*----------------------------------------------------------------------------*/
