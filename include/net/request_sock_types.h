/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * NET		Generic infrastructure for Network protocols.
 *
 *		Definitions for request_sock
 *
 * Authors:	Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 *
 * 		From code originally in include/net/tcp.h
 */
#ifndef _REQUEST_SOCK_TYPES_H
#define _REQUEST_SOCK_TYPES_H

#include <net/sock_types.h>

struct request_sock;
struct sk_buff;
struct dst_entry;
struct proto;

struct request_sock_ops {
	int		family;
	unsigned int	obj_size;
	struct kmem_cache	*slab;
	char		*slab_name;
	int		(*rtx_syn_ack)(const struct sock *sk,
				       struct request_sock *req);
	void		(*send_ack)(const struct sock *sk, struct sk_buff *skb,
				    struct request_sock *req);
	void		(*send_reset)(const struct sock *sk,
				      struct sk_buff *skb);
	void		(*destructor)(struct request_sock *req);
	void		(*syn_ack_timeout)(const struct request_sock *req);
};

int inet_rtx_syn_ack(const struct sock *parent, struct request_sock *req);

struct saved_syn {
	u32 mac_hdrlen;
	u32 network_hdrlen;
	u32 tcp_hdrlen;
	u8 data[];
};

/* struct request_sock - mini sock to represent a connection request
 */
struct request_sock {
	struct sock_common		__req_common;
#define rsk_refcnt			__req_common.skc_refcnt
#define rsk_hash			__req_common.skc_hash
#define rsk_listener			__req_common.skc_listener
#define rsk_window_clamp		__req_common.skc_window_clamp
#define rsk_rcv_wnd			__req_common.skc_rcv_wnd

	struct request_sock		*dl_next;
	u16				mss;
	u8				num_retrans; /* number of retransmits */
	u8				syncookie:1; /* syncookie: encode tcpopts in timestamp */
	u8				num_timeout:7; /* number of timeouts */
	u32				ts_recent;
	struct timer_list		rsk_timer;
	const struct request_sock_ops	*rsk_ops;
	struct sock			*sk;
	struct saved_syn		*saved_syn;
	u32				secid;
	u32				peer_secid;
};

/*
 * For a TCP Fast Open listener -
 *	lock - protects the access to all the reqsk, which is co-owned by
 *		the listener and the child socket.
 *	qlen - pending TFO requests (still in TCP_SYN_RECV).
 *	max_qlen - max TFO reqs allowed before TFO is disabled.
 *
 *	XXX (TFO) - ideally these fields can be made as part of "listen_sock"
 *	structure above. But there is some implementation difficulty due to
 *	listen_sock being part of request_sock_queue hence will be freed when
 *	a listener is stopped. But TFO related fields may continue to be
 *	accessed even after a listener is closed, until its sk_refcnt drops
 *	to 0 implying no more outstanding TFO reqs. One solution is to keep
 *	listen_opt around until	sk_refcnt drops to 0. But there is some other
 *	complexity that needs to be resolved. E.g., a listener can be disabled
 *	temporarily through shutdown()->tcp_disconnect(), and re-enabled later.
 */
struct fastopen_queue {
	struct request_sock	*rskq_rst_head; /* Keep track of past TFO */
	struct request_sock	*rskq_rst_tail; /* requests that caused RST.
						 * This is part of the defense
						 * against spoofing attack.
						 */
	spinlock_t	lock;
	int		qlen;		/* # of pending (TCP_SYN_RECV) reqs */
	int		max_qlen;	/* != 0 iff TFO is currently enabled */

	struct tcp_fastopen_context __rcu *ctx; /* cipher context for cookie */
};

/** struct request_sock_queue - queue of request_socks
 *
 * @rskq_accept_head - FIFO head of established children
 * @rskq_accept_tail - FIFO tail of established children
 * @rskq_defer_accept - User waits for some data after accept()
 *
 */
struct request_sock_queue {
	spinlock_t		rskq_lock;
	u8			rskq_defer_accept;

	u32			synflood_warned;
	atomic_t		qlen;
	atomic_t		young;

	struct request_sock	*rskq_accept_head;
	struct request_sock	*rskq_accept_tail;
	struct fastopen_queue	fastopenq;  /* Check max_qlen != 0 to determine
					     * if TFO is enabled.
					     */
};

#endif /* _REQUEST_SOCK_TYPES_H */
