/*
 * TCP net.GoGo (ngg) congestion control
 *
 */

#include <linux/mm.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/inet_diag.h>

#include <net/tcp.h>

#define NGG_MAX_CWND 6553500U

static int alpha = 2;
static int beta  = 10;
static int gamma = 1;

module_param(alpha, int, 0644);
MODULE_PARM_DESC(alpha, "increase/decrease rate parameter");
module_param(beta, int, 0644);
MODULE_PARM_DESC(beta, "number of ack to make a decision");
module_param(gamma, int, 0644);
MODULE_PARM_DESC(gamma, "limit on increase (scale by 2)");


/* ngg variables */
struct ngg {
	u8 doing_ngg_now;	/* if true, do ngg for this rtt */
	u32 current_rate; 	/* Bps */ 
	u8 ack_cnt;
	u32 srate;			/* smoothed rate according to ack_cnt */
};

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


}

static void tcp_ngg_cong_avoid(struct sock *sk, u32 ack, u32 acked)
{
	struct tcp_sock *tp = tcp_sk(sk);
	struct ngg *ngg = inet_csk_ca(sk);
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
	if (tp->snd_cwnd < tp->snd_cwnd_clamp)
		tp->snd_cwnd = tp->snd_cwnd << 1U;
#endif
	
	// if (tp->snd_cwnd < NGG_MAX_CWND) {
	// 	tp->snd_cwnd = tp->snd_cwnd << 1U;
	// } else {
	// 	tcp_cong_avoid_ai(tp, tp->snd_cwnd, acked);
	// }

	cnt_log++;
	if (cnt_log % 500 == 0) {
		printk("cong_avoid, cwnd:%u, srate:%uKB\n", tp->snd_cwnd, ngg->srate);
	}


}

/* ngg MD phase */
static u32 tcp_ngg_ssthresh(struct sock *sk)
{
	u32 cwnd;
	struct tcp_sock *tp = tcp_sk(sk);
	tp->snd_cwnd -= 1024;
	cwnd = max(tp->snd_cwnd, 2U);

	printk("ssthresh:%u\n", cwnd);
	return cwnd;
}

static void tcp_ngg_set_state(struct sock *sk, u8 new_state)
{
	struct tcp_sock *tp = tcp_sk(sk);
	switch (new_state) {
		case TCP_CA_Open:
			printk("TCP_CA_Open, cwnd:%u\n", tp->snd_cwnd);
			break;
		case TCPF_CA_Disorder:
			printk("TCPF_CA_Disorder, cwnd:%u\n", tp->snd_cwnd);
			break;
		case TCPF_CA_CWR:
			printk("TCPF_CA_CWR, cwnd:%u\n", tp->snd_cwnd);
			break;
		case TCPF_CA_Recovery:
			printk("TCPF_CA_Recovery, cwnd:%u\n", tp->snd_cwnd);
			break;
		case TCPF_CA_Loss:
			printk("TCPF_CA_Loss, cwnd:%u\n", tp->snd_cwnd);
			break;
	}
}

static void tcp_ngg_cwnd_event(struct sock *sk, enum tcp_ca_event ev) 
{
	struct tcp_sock *tp = tcp_sk(sk);
	switch (ev) {
		case CA_EVENT_TX_START:
			printk("CA_EVENT_TX_START, cwnd:%u\n", tp->snd_cwnd);
			break;
		case CA_EVENT_CWND_RESTART:
			printk("CA_EVENT_CWND_RESTART, cwnd:%u\n", tp->snd_cwnd);
			break;
		case CA_EVENT_COMPLETE_CWR:
			printk("CA_EVENT_COMPLETE_CWR, cwnd:%u\n", tp->snd_cwnd);
			break;
		case CA_EVENT_LOSS:
			printk("CA_EVENT_LOSS, cwnd:%u\n", tp->snd_cwnd);
			break;
		case CA_EVENT_ECN_NO_CE:
			printk("CA_EVENT_ECN_NO_CE, cwnd:%u\n", tp->snd_cwnd);
			break;
		case CA_EVENT_ECN_IS_CE:
			printk("CA_EVENT_ECN_IS_CE, cwnd:%u\n", tp->snd_cwnd);
			break;
		case CA_EVENT_DELAYED_ACK:
			printk("CA_EVENT_DELAYED_ACK, cwnd:%u\n", tp->snd_cwnd);
			break;
		case CA_EVENT_NON_DELAYED_ACK:
			printk("CA_EVENT_NON_DELAYED_ACK, cwnd:%u\n", tp->snd_cwnd);
			break;
	}
}

static void tcp_ngg_pkts_acked(struct sock *sk, u32 num_acked, s32 rtt_us)
{
	// struct tcp_sock *tp = tcp_sk(sk);
	struct ngg *ngg = inet_csk_ca(sk);

	u32 r = num_acked * 1000000 / rtt_us;
	if (r == 0)	return;
	ngg->srate = (r + ngg->ack_cnt * ngg->srate) / (ngg->ack_cnt + 1);
	ngg->ack_cnt++;
}

static struct tcp_congestion_ops tcp_ngg __read_mostly = {
	.init 		= tcp_ngg_init,
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
