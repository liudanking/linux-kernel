/*
 * TCP net.GoGo (ngg) congestion control
 *
 */

#include <linux/mm.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/inet_diag.h>

#include <net/tcp.h>

/* ngg variables */
struct ngg {
	u8 doing_ngg_now;	/* if true, do ngg for this rtt */
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
	struct ngg *ngg = inet_csk_ca(sk);

	ngg->doing_ngg_now = 1;
}

static void tcp_ngg_cong_avoid(struct sock *sk, u32 ack, u32 acked)
{
	struct tcp_sock *tp = tcp_sk(sk);
	tp->snd_cwnd = min(tp->snd_cwnd << 2, 0xafffffff);
}

/* ngg MD phase */
static u32 tcp_ngg_ssthresh(struct sock *sk)
{
	struct tcp_sock *tp = tcp_sk(sk);
	return max(tp->snd_cwnd--, 2U);
}

static struct tcp_congestion_ops tcp_ngg __read_mostly = {
	.init 		= tcp_ngg_init,
	.cong_avoid	= tcp_ngg_cong_avoid,
	.ssthresh	= tcp_ngg_ssthresh,

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
