/*
 * TCP net.GoGo (ngg) congestion control
 *
 */


#include <linux/mm.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/inet_diag.h>

#include <net/tcp.h>



#define NGG_DBG 1
#define NGG_MAX_CWND 6553500U

static int alpha = 2;
static int beta  = 10;
static int gamma = 8;

module_param(alpha, int, 0644);
MODULE_PARM_DESC(alpha, "increase/decrease rate parameter");
module_param(beta, int, 0644);
MODULE_PARM_DESC(beta, "number of ack to make a change rate decision");
module_param(gamma, int, 0644);
MODULE_PARM_DESC(gamma, "number of loss packet to make a MD decision");


/* ngg variables */
struct ngg {
	u8 doing_ngg_now;	/* if true, do ngg for this rtt */
	u32 current_rate; 	/* in Bps */ 
	u8 ack_cnt;			/* ack count */
	u32 srate;			/* smoothed rate according to ack_cnt */
	u8 loss_cnt;		/* loss packet count */
	// for statistics
	u32 loss_total;
	u32 cwr_cnt;
	// copy from vegas
	u32	beg_snd_nxt;	/* right edge during last RTT */
	u32	beg_snd_una;	/* left edge  during last RTT */
	u32	beg_snd_cwnd;	/* saves the size of the cwnd */
	u8	doing_vegas_now;/* if true, do vegas for this RTT */
	u16	cntRTT;		/* # of RTTs measured within last RTT */
	u32	minRTT;		/* min of RTTs measured within last RTT (in usec) */
	u32	baseRTT;	/* the min of all Vegas RTT measurements seen (in usec) */
};


static int ngg_printk(char *fmt, ...)
{
#if NGG_DBG
	va_list args;
	int n;
    va_start(args, fmt);
    n = vprintk(fmt, args);
    va_end(args);
    return n;
#endif
    return 0;
}

/* There are several situations when we must "re-start" ngg:
 *
 *  o when a connection is established
 *  o after an RTO
 *  o after fast recovery
 *  o when we send a packet and there is no outstanding
 *    unacknowledged data (restarting an idle connection)
 *
 */

static void tcp_ngg_init(struct sock *sk)
{
	struct tcp_sock *tp = tcp_sk(sk);
	struct ngg *ngg = inet_csk_ca(sk);

	tp->snd_cwnd = 65535U;
	tp->snd_ssthresh = NGG_MAX_CWND;
	tp->snd_cwnd_clamp = 2 * NGG_MAX_CWND;
	
	ngg->doing_ngg_now = 1;
	ngg->current_rate = 0;
	ngg->ack_cnt = 0;
	ngg->srate = 0;
	ngg->loss_cnt = 0;

	ngg->loss_total = 0;
	ngg->cwr_cnt = 0;

	ngg_printk("\n===== connection start =====\n");
}


static void tcp_ngg_release(struct sock *sk)
{
	struct ngg *ngg = inet_csk_ca(sk);
	ngg_printk("===== connection complete!=====\n");
	ngg_printk("loss_total:%u, cwr_cnt:%u\n\n", ngg->loss_total, ngg->cwr_cnt);

}

static void tcp_ngg_cong_avoid(struct sock *sk, u32 ack, u32 acked)
{
	struct tcp_sock *tp = tcp_sk(sk);
	struct ngg *ngg = inet_csk_ca(sk);
	u64 rate;
	static u32 cnt_log = 0;

#if 0
	if (ngg->ack_cnt > beta && ngg->srate > ngg->current_rate) {
		tp->snd_cwnd = max(tp->snd_cwnd, tp->snd_cwnd + tp->snd_cwnd/alpha);
		tp->snd_cwnd = min(tp->snd_cwnd, tp->snd_cwnd_clamp);
		ngg->current_rate = ngg->srate;
		ngg->ack_cnt = 0;
		ngg->srate = 0;
	}
#else
	if (tp->snd_cwnd < tp->snd_cwnd_clamp) {
		if (ngg->loss_cnt <= gamma/2 && ngg->cwr_cnt <= gamma) {
			tp->snd_cwnd = tp->snd_cwnd << 1U;
		} else {
			tp->snd_cwnd += acked;
			if (ngg->loss_cnt > 0)
				ngg->loss_cnt--;
			if (ngg->cwr_cnt > 0)
				ngg->cwr_cnt--;
		}
	}
#endif

	cnt_log++;
	if (cnt_log % 500 == 0) {
		if (ngg->baseRTT > 0) {
			 rate = (u64)((ngg->beg_snd_nxt - ngg->beg_snd_una) * 1000000) / ngg->baseRTT;
			 ngg_printk("cong_avoid, beg_snd_una: %u, beg_snd_una: %lu, rate: %uKB\n", ngg->beg_snd_una, ngg->beg_snd_nxt, rate/1024);
		}
		ngg_printk("cong_avoid, cwnd:%u, srate:%uKB\n", tp->snd_cwnd, ngg->srate);
	}

	// save current state
	ngg->beg_snd_nxt = tp->snd_nxt;
	ngg->beg_snd_una = tp->snd_una;
	ngg->beg_snd_cwnd = tp->snd_cwnd;


}

/* ngg MD phase */
static u32 tcp_ngg_ssthresh(struct sock *sk)
{
	struct tcp_sock *tp = tcp_sk(sk);
	struct ngg *ngg = inet_csk_ca(sk);
	do {
		if (ngg->loss_cnt > gamma) {
			ngg_printk("cwnd MD by loss_cnt\n");
			ngg->loss_cnt = 0;
			tp->snd_cwnd = max(tp->snd_cwnd / 2, 8192U);
			break;
		}
		if (ngg->cwr_cnt > 2 * gamma) {
			ngg_printk("cwnd MD by cwr_cnt\n");
			ngg->cwr_cnt = 0;
			tp->snd_cwnd = max(tp->snd_cwnd / 2, 8192U);
			break;	
		}
		
		tp->snd_cwnd -= 1024;
		tp->snd_cwnd = max(tp->snd_cwnd, 8192U);

		break;
	} while(1);

	ngg_printk("ssthresh cwnd:%u, loss_cnt:%u, cwr_cnt:%u\n", tp->snd_cwnd, ngg->loss_cnt, ngg->cwr_cnt);
	return tp->snd_cwnd;
}

static void tcp_ngg_set_state(struct sock *sk, u8 new_state)
{
	struct tcp_sock *tp = tcp_sk(sk);
	switch (new_state) {
		case TCP_CA_Open:
			ngg_printk("TCP_CA_Open, cwnd:%u\n", tp->snd_cwnd);
			break;
		case TCPF_CA_Disorder:
			ngg_printk("TCPF_CA_Disorder, cwnd:%u\n", tp->snd_cwnd);
			break;
		case TCPF_CA_CWR:
			ngg_printk("TCPF_CA_CWR, cwnd:%u\n", tp->snd_cwnd);
			tp->snd_cwnd = NGG_MAX_CWND/2;
			ngg_printk("TCPF_CA_CWR, cwnd-->:%u\n", tp->snd_cwnd);
			break;
		case TCPF_CA_Recovery:
			ngg_printk("TCPF_CA_Recovery, cwnd:%u\n", tp->snd_cwnd);
			break;
		case TCPF_CA_Loss:
			ngg_printk("TCPF_CA_Loss, cwnd:%u\n", tp->snd_cwnd);
			break;
	}
}

static void tcp_ngg_cwnd_event(struct sock *sk, enum tcp_ca_event ev) 
{
	struct tcp_sock *tp = tcp_sk(sk);
	struct ngg *ngg = inet_csk_ca(sk);
	switch (ev) {
		case CA_EVENT_TX_START:
			ngg_printk("CA_EVENT_TX_START, cwnd:%u\n", tp->snd_cwnd);
			break;
		case CA_EVENT_CWND_RESTART:
			ngg_printk("CA_EVENT_CWND_RESTART, cwnd:%u\n", tp->snd_cwnd);
			break;
		case CA_EVENT_COMPLETE_CWR:
			/*
			 * This event means local congestion window may by overflow, then cut window by half.
			 */
			ngg->cwr_cnt++;
			ngg_printk("CA_EVENT_COMPLETE_CWR, cwnd:%u\n", tp->snd_cwnd);
			// tp->snd_cwnd = max(tp->snd_cwnd >> 1U, 65535U);
			// ngg_printk("CA_EVENT_COMPLETE_CWR, cwnd-->:%u\n", tp->snd_cwnd);
			break;
		case CA_EVENT_LOSS:
			ngg->loss_cnt++;
			ngg_printk("CA_EVENT_LOSS, cwnd:%u\n", tp->snd_cwnd);
			break;
		case CA_EVENT_ECN_NO_CE:
			ngg_printk("CA_EVENT_ECN_NO_CE, cwnd:%u\n", tp->snd_cwnd);
			break;
		case CA_EVENT_ECN_IS_CE:
			ngg_printk("CA_EVENT_ECN_IS_CE, cwnd:%u\n", tp->snd_cwnd);
			break;
		case CA_EVENT_DELAYED_ACK:
			ngg_printk("CA_EVENT_DELAYED_ACK, cwnd:%u\n", tp->snd_cwnd);
			break;
		case CA_EVENT_NON_DELAYED_ACK:
			ngg_printk("CA_EVENT_NON_DELAYED_ACK, cwnd:%u\n", tp->snd_cwnd);
			break;
	}
}

static void tcp_ngg_pkts_acked(struct sock *sk, u32 num_acked, s32 rtt_us)
{
	// struct tcp_sock *tp = tcp_sk(sk);
	struct ngg *ngg = inet_csk_ca(sk);
	u32 r;
	static u32 cnt_log = 0;

	if (rtt_us <= 0 ) {
		return;
	} else {
		ngg->baseRTT = rtt_us;
		r = num_acked * 1000000 / rtt_us;
		if (r == 0)	return;
	}

	ngg->srate = (r + ngg->ack_cnt * ngg->srate) / (ngg->ack_cnt + 1);
	ngg->ack_cnt++;

	cnt_log++;
	if (cnt_log % 500  == 0) {
		ngg_printk("pkts_acked, num_acked:%u, rtt_us:%d, rate:%uKB\n", num_acked, rtt_us, r/1024);
	}
}

static struct tcp_congestion_ops tcp_ngg __read_mostly = {
	.init 		= tcp_ngg_init,
	.release 	= tcp_ngg_release,
	.cong_avoid	= tcp_ngg_cong_avoid,
	.ssthresh	= tcp_ngg_ssthresh,
	.set_state 	= tcp_ngg_set_state,
	.cwnd_event = tcp_ngg_cwnd_event,
	.pkts_acked	= tcp_ngg_pkts_acked,

	.owner		= THIS_MODULE,
	.name		= "ngg",
};

static int __init tcp_ngg_register(void)
{
	BUILD_BUG_ON(sizeof(struct ngg) > ICSK_CA_PRIV_SIZE);
	tcp_register_congestion_control(&tcp_ngg);
	return 0;
}

static void __exit tcp_ngg_unregister(void)
{
	tcp_unregister_congestion_control(&tcp_ngg);
}

module_init(tcp_ngg_register);
module_exit(tcp_ngg_unregister);

MODULE_AUTHOR("Daniel");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("TCP ngg");
