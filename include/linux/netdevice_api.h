/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Definitions for the Interfaces handler.
 *
 * Version:	@(#)dev.h	1.0.10	08/12/93
 *
 * Authors:	Ross Biro
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *		Corey Minyard <wf-rch!minyard@relay.EU.net>
 *		Donald J. Becker, <becker@cesdis.gsfc.nasa.gov>
 *		Alan Cox, <alan@lxorguk.ukuu.org.uk>
 *		Bjorn Ekwall. <bj0rn@blox.se>
 *              Pekka Riikonen <priikone@poseidon.pspt.fi>
 *
 *		Moved to /usr/include/linux for NET3
 */
#ifndef _LINUX_NETDEVICE_API_H
#define _LINUX_NETDEVICE_API_H

#include <linux/rculist.h>
#include <linux/smp_types.h>
#include <linux/netdevice_types.h>

#include <linux/dynamic_queue_limits.h>
#include <linux/netdev_features.h>

#include <net/net_namespace.h>
#include <net/xdp.h>

#include <uapi/linux/netdevice.h>
#include <uapi/linux/if_bonding.h>

#include <linux/skbuff_api.h>

/* This structure contains an instance of an RX queue. */
____cacheline_aligned_in_smp struct netdev_rx_queue {
	struct xdp_rxq_info		xdp_rxq;
#ifdef CONFIG_RPS
	struct rps_map __rcu		*rps_map;
	struct rps_dev_flow_table __rcu	*rps_flow_table;
#endif
	struct kobject			kobj;
	struct net_device		*dev;
#ifdef CONFIG_XDP_SOCKETS
	struct xsk_buff_pool            *pool;
#endif
};

/*
 * Current order: NETDEV_TX_MASK > NET_XMIT_MASK >= 0 is significant;
 * hard_start_xmit() return < NET_XMIT_MASK means skb was consumed.
 */
static inline bool dev_xmit_complete(int rc)
{
	/*
	 * Positive cases with an skb consumed by a driver:
	 * - successful transmission (rc == NETDEV_TX_OK)
	 * - error while transmitting (rc < 0)
	 * - error while queueing to a different device (rc & NET_XMIT_MASK)
	 */
	if (likely(rc < NET_XMIT_MASK))
		return true;

	return false;
}

void __napi_schedule(struct napi_struct *n);
void __napi_schedule_irqoff(struct napi_struct *n);

static inline bool napi_disable_pending(struct napi_struct *n)
{
	return test_bit(NAPI_STATE_DISABLE, &n->state);
}

static inline bool napi_prefer_busy_poll(struct napi_struct *n)
{
	return test_bit(NAPI_STATE_PREFER_BUSY_POLL, &n->state);
}

bool napi_schedule_prep(struct napi_struct *n);

/**
 *	napi_schedule - schedule NAPI poll
 *	@n: NAPI context
 *
 * Schedule NAPI poll routine to be called if it is not already
 * running.
 */
static inline void napi_schedule(struct napi_struct *n)
{
	if (napi_schedule_prep(n))
		__napi_schedule(n);
}

/**
 *	napi_schedule_irqoff - schedule NAPI poll
 *	@n: NAPI context
 *
 * Variant of napi_schedule(), assuming hard irqs are masked.
 */
static inline void napi_schedule_irqoff(struct napi_struct *n)
{
	if (napi_schedule_prep(n))
		__napi_schedule_irqoff(n);
}

/* Try to reschedule poll. Called by dev->poll() after napi_complete().  */
static inline bool napi_reschedule(struct napi_struct *napi)
{
	if (napi_schedule_prep(napi)) {
		__napi_schedule(napi);
		return true;
	}
	return false;
}

bool napi_complete_done(struct napi_struct *n, int work_done);
/**
 *	napi_complete - NAPI processing complete
 *	@n: NAPI context
 *
 * Mark NAPI processing as complete.
 * Consider using napi_complete_done() instead.
 * Return false if device should avoid rearming interrupts.
 */
static inline bool napi_complete(struct napi_struct *n)
{
	return napi_complete_done(n, 0);
}

int dev_set_threaded(struct net_device *dev, bool threaded);

/**
 *	napi_disable - prevent NAPI from scheduling
 *	@n: NAPI context
 *
 * Stop NAPI from being scheduled on this context.
 * Waits till any outstanding processing completes.
 */
void napi_disable(struct napi_struct *n);

void napi_enable(struct napi_struct *n);

/**
 *	napi_synchronize - wait until NAPI is not running
 *	@n: NAPI context
 *
 * Wait until NAPI is done being scheduled on this context.
 * Waits till any outstanding processing completes but
 * does not disable future activations.
 */
void napi_synchronize(const struct napi_struct *n);

/**
 *	napi_if_scheduled_mark_missed - if napi is running, set the
 *	NAPIF_STATE_MISSED
 *	@n: NAPI context
 *
 * If napi is running, set the NAPIF_STATE_MISSED, and return true if
 * NAPI is scheduled.
 **/
static inline bool napi_if_scheduled_mark_missed(struct napi_struct *n)
{
	unsigned long val, new;

	do {
		val = READ_ONCE(n->state);
		if (val & NAPIF_STATE_DISABLE)
			return true;

		if (!(val & NAPIF_STATE_SCHED))
			return false;

		new = val | NAPIF_STATE_MISSED;
	} while (cmpxchg(&n->state, val, new) != val);

	return true;
}

extern int sysctl_fb_tunnels_only_for_init_net;
extern int sysctl_devconf_inherit_init_net;

/*
 * sysctl_fb_tunnels_only_for_init_net == 0 : For all netns
 *                                     == 1 : For initns only
 *                                     == 2 : For none.
 */
static inline bool net_has_fallback_tunnels(const struct net *net)
{
	return !IS_ENABLED(CONFIG_SYSCTL) ||
	       !sysctl_fb_tunnels_only_for_init_net ||
	       (net == &init_net && sysctl_fb_tunnels_only_for_init_net == 1);
}

static inline int netdev_queue_numa_node_read(const struct netdev_queue *q)
{
#if defined(CONFIG_XPS) && defined(CONFIG_NUMA)
	return q->numa_node;
#else
	return NUMA_NO_NODE;
#endif
}

static inline void netdev_queue_numa_node_write(struct netdev_queue *q, int node)
{
#if defined(CONFIG_XPS) && defined(CONFIG_NUMA)
	q->numa_node = node;
#endif
}

static inline bool netdev_phys_item_id_same(struct netdev_phys_item_id *a,
					    struct netdev_phys_item_id *b)
{
	return a->id_len == b->id_len &&
	       memcmp(a->id, b->id, a->id_len) == 0;
}

static inline bool netif_elide_gro(const struct net_device *dev)
{
	if (!(dev->features & NETIF_F_GRO) || dev->xdp_prog)
		return true;
	return false;
}

static inline
int netdev_get_prio_tc_map(const struct net_device *dev, u32 prio)
{
	return dev->prio_tc_map[prio & TC_BITMASK];
}

static inline
int netdev_set_prio_tc_map(struct net_device *dev, u8 prio, u8 tc)
{
	if (tc >= dev->num_tc)
		return -EINVAL;

	dev->prio_tc_map[prio & TC_BITMASK] = tc & TC_BITMASK;
	return 0;
}

int netdev_txq_to_tc(struct net_device *dev, unsigned int txq);
void netdev_reset_tc(struct net_device *dev);
int netdev_set_tc_queue(struct net_device *dev, u8 tc, u16 count, u16 offset);
int netdev_set_num_tc(struct net_device *dev, u8 num_tc);

static inline
int netdev_get_num_tc(struct net_device *dev)
{
	return dev->num_tc;
}

void netdev_unbind_sb_channel(struct net_device *dev,
			      struct net_device *sb_dev);
int netdev_bind_sb_channel_queue(struct net_device *dev,
				 struct net_device *sb_dev,
				 u8 tc, u16 count, u16 offset);
int netdev_set_sb_channel(struct net_device *dev, u16 channel);
static inline int netdev_get_sb_channel(struct net_device *dev)
{
	return max_t(int, -dev->num_tc, 0);
}

static inline
struct netdev_queue *netdev_get_tx_queue(const struct net_device *dev,
					 unsigned int index)
{
	return &dev->_tx[index];
}

static inline struct netdev_queue *skb_get_tx_queue(const struct net_device *dev,
						    const struct sk_buff *skb)
{
	return netdev_get_tx_queue(dev, skb_get_queue_mapping(skb));
}

static inline void netdev_for_each_tx_queue(struct net_device *dev,
					    void (*f)(struct net_device *,
						      struct netdev_queue *,
						      void *),
					    void *arg)
{
	unsigned int i;

	for (i = 0; i < dev->num_tx_queues; i++)
		f(dev, &dev->_tx[i], arg);
}

#define netdev_lockdep_set_classes(dev)				\
{								\
	static struct lock_class_key qdisc_tx_busylock_key;	\
	static struct lock_class_key qdisc_xmit_lock_key;	\
	static struct lock_class_key dev_addr_list_lock_key;	\
	unsigned int i;						\
								\
	(dev)->qdisc_tx_busylock = &qdisc_tx_busylock_key;	\
	lockdep_set_class(&(dev)->addr_list_lock,		\
			  &dev_addr_list_lock_key);		\
	for (i = 0; i < (dev)->num_tx_queues; i++)		\
		lockdep_set_class(&(dev)->_tx[i]._xmit_lock,	\
				  &qdisc_xmit_lock_key);	\
}

u16 netdev_pick_tx(struct net_device *dev, struct sk_buff *skb,
		     struct net_device *sb_dev);
struct netdev_queue *netdev_core_pick_tx(struct net_device *dev,
					 struct sk_buff *skb,
					 struct net_device *sb_dev);

/* returns the headroom that the master device needs to take in account
 * when forwarding to this dev
 */
static inline unsigned netdev_get_fwd_headroom(struct net_device *dev)
{
	return dev->priv_flags & IFF_PHONY_HEADROOM ? 0 : dev->needed_headroom;
}

static inline void netdev_set_rx_headroom(struct net_device *dev, int new_hr)
{
	if (dev->netdev_ops->ndo_set_rx_headroom)
		dev->netdev_ops->ndo_set_rx_headroom(dev, new_hr);
}

/* set the device rx headroom to the dev's default */
static inline void netdev_reset_rx_headroom(struct net_device *dev)
{
	netdev_set_rx_headroom(dev, -1);
}

static inline void *netdev_get_ml_priv(struct net_device *dev,
				       enum netdev_ml_priv_type type)
{
	if (dev->ml_priv_type != type)
		return NULL;

	return dev->ml_priv;
}

static inline void netdev_set_ml_priv(struct net_device *dev,
				      void *ml_priv,
				      enum netdev_ml_priv_type type)
{
	WARN(dev->ml_priv_type && dev->ml_priv_type != type,
	     "Overwriting already set ml_priv_type (%u) with different ml_priv_type (%u)!\n",
	     dev->ml_priv_type, type);
	WARN(!dev->ml_priv_type && dev->ml_priv,
	     "Overwriting already set ml_priv and ml_priv_type is ML_PRIV_NONE!\n");

	dev->ml_priv = ml_priv;
	dev->ml_priv_type = type;
}

/* Set the sysfs physical device reference for the network logical device
 * if set prior to registration will cause a symlink during initialization.
 */
#define SET_NETDEV_DEV(net, pdev)	((net)->dev.parent = (pdev))

/* Set the sysfs device type for the network logical device to allow
 * fine-grained identification of different network device types. For
 * example Ethernet, Wireless LAN, Bluetooth, WiMAX etc.
 */
#define SET_NETDEV_DEVTYPE(net, devtype)	((net)->dev.type = (devtype))

/* Default NAPI poll() weight
 * Device drivers are strongly advised to not use bigger value
 */
#define NAPI_POLL_WEIGHT 64

/**
 *	netif_napi_add - initialize a NAPI context
 *	@dev:  network device
 *	@napi: NAPI context
 *	@poll: polling function
 *	@weight: default weight
 *
 * netif_napi_add() must be used to initialize a NAPI context prior to calling
 * *any* of the other NAPI-related functions.
 */
void netif_napi_add(struct net_device *dev, struct napi_struct *napi,
		    int (*poll)(struct napi_struct *, int), int weight);

/**
 *	netif_tx_napi_add - initialize a NAPI context
 *	@dev:  network device
 *	@napi: NAPI context
 *	@poll: polling function
 *	@weight: default weight
 *
 * This variant of netif_napi_add() should be used from drivers using NAPI
 * to exclusively poll a TX queue.
 * This will avoid we add it into napi_hash[], thus polluting this hash table.
 */
static inline void netif_tx_napi_add(struct net_device *dev,
				     struct napi_struct *napi,
				     int (*poll)(struct napi_struct *, int),
				     int weight)
{
	set_bit(NAPI_STATE_NO_BUSY_POLL, &napi->state);
	netif_napi_add(dev, napi, poll, weight);
}

/**
 *  __netif_napi_del - remove a NAPI context
 *  @napi: NAPI context
 *
 * Warning: caller must observe RCU grace period before freeing memory
 * containing @napi. Drivers might want to call this helper to combine
 * all the needed RCU grace periods into a single one.
 */
void __netif_napi_del(struct napi_struct *napi);

/**
 *  netif_napi_del - remove a NAPI context
 *  @napi: NAPI context
 *
 *  netif_napi_del() removes a NAPI context from the network device NAPI list
 */
static inline void netif_napi_del(struct napi_struct *napi)
{
	__netif_napi_del(napi);
	synchronize_net();
}

struct napi_gro_cb {
	/* Virtual address of skb_shinfo(skb)->frags[0].page + offset. */
	void	*frag0;

	/* Length of frag0. */
	unsigned int frag0_len;

	/* This indicates where we are processing relative to skb->data. */
	int	data_offset;

	/* This is non-zero if the packet cannot be merged with the new skb. */
	u16	flush;

	/* Save the IP ID here and check when we get to the transport layer */
	u16	flush_id;

	/* Number of segments aggregated. */
	u16	count;

	/* Start offset for remote checksum offload */
	u16	gro_remcsum_start;

	/* jiffies when first packet was created/queued */
	unsigned long age;

	/* Used in ipv6_gro_receive() and foo-over-udp */
	u16	proto;

	/* This is non-zero if the packet may be of the same flow. */
	u8	same_flow:1;

	/* Used in tunnel GRO receive */
	u8	encap_mark:1;

	/* GRO checksum is valid */
	u8	csum_valid:1;

	/* Number of checksums via CHECKSUM_UNNECESSARY */
	u8	csum_cnt:3;

	/* Free the skb? */
	u8	free:2;
#define NAPI_GRO_FREE		  1
#define NAPI_GRO_FREE_STOLEN_HEAD 2

	/* Used in foo-over-udp, set in udp[46]_gro_receive */
	u8	is_ipv6:1;

	/* Used in GRE, set in fou/gue_gro_receive */
	u8	is_fou:1;

	/* Used to determine if flush_id can be ignored */
	u8	is_atomic:1;

	/* Number of gro_receive callbacks this packet already went through */
	u8 recursion_counter:4;

	/* GRO is done by frag_list pointer chaining. */
	u8	is_flist:1;

	/* used to support CHECKSUM_COMPLETE for tunneling protocols */
	__wsum	csum;

	/* used in skb_gro_receive() slow path */
	struct sk_buff *last;
};

#define NAPI_GRO_CB(skb) ((struct napi_gro_cb *)(skb)->cb)

#define GRO_RECURSION_LIMIT 15
static inline int gro_recursion_inc_test(struct sk_buff *skb)
{
	return ++NAPI_GRO_CB(skb)->recursion_counter == GRO_RECURSION_LIMIT;
}

typedef struct sk_buff *(*gro_receive_t)(struct list_head *, struct sk_buff *);
static inline struct sk_buff *call_gro_receive(gro_receive_t cb,
					       struct list_head *head,
					       struct sk_buff *skb)
{
	if (unlikely(gro_recursion_inc_test(skb))) {
		NAPI_GRO_CB(skb)->flush |= 1;
		return NULL;
	}

	return cb(head, skb);
}

typedef struct sk_buff *(*gro_receive_sk_t)(struct sock *, struct list_head *,
					    struct sk_buff *);
static inline struct sk_buff *call_gro_receive_sk(gro_receive_sk_t cb,
						  struct sock *sk,
						  struct list_head *head,
						  struct sk_buff *skb)
{
	if (unlikely(gro_recursion_inc_test(skb))) {
		NAPI_GRO_CB(skb)->flush |= 1;
		return NULL;
	}

	return cb(sk, head, skb);
}

void dev_lstats_read(struct net_device *dev, u64 *packets, u64 *bytes);

enum netdev_lag_tx_type {
	NETDEV_LAG_TX_TYPE_UNKNOWN,
	NETDEV_LAG_TX_TYPE_RANDOM,
	NETDEV_LAG_TX_TYPE_BROADCAST,
	NETDEV_LAG_TX_TYPE_ROUNDROBIN,
	NETDEV_LAG_TX_TYPE_ACTIVEBACKUP,
	NETDEV_LAG_TX_TYPE_HASH,
};

enum netdev_lag_hash {
	NETDEV_LAG_HASH_NONE,
	NETDEV_LAG_HASH_L2,
	NETDEV_LAG_HASH_L34,
	NETDEV_LAG_HASH_L23,
	NETDEV_LAG_HASH_E23,
	NETDEV_LAG_HASH_E34,
	NETDEV_LAG_HASH_VLAN_SRCMAC,
	NETDEV_LAG_HASH_UNKNOWN,
};

struct netdev_lag_upper_info {
	enum netdev_lag_tx_type tx_type;
	enum netdev_lag_hash hash_type;
};

struct netdev_lag_lower_state_info {
	u8 link_up : 1,
	   tx_enabled : 1;
};

/* netdevice notifier chain. Please remember to update netdev_cmd_to_name()
 * and the rtnetlink notification exclusion list in rtnetlink_event() when
 * adding new types.
 */
enum netdev_cmd {
	NETDEV_UP	= 1,	/* For now you can't veto a device up/down */
	NETDEV_DOWN,
	NETDEV_REBOOT,		/* Tell a protocol stack a network interface
				   detected a hardware crash and restarted
				   - we can use this eg to kick tcp sessions
				   once done */
	NETDEV_CHANGE,		/* Notify device state change */
	NETDEV_REGISTER,
	NETDEV_UNREGISTER,
	NETDEV_CHANGEMTU,	/* notify after mtu change happened */
	NETDEV_CHANGEADDR,	/* notify after the address change */
	NETDEV_PRE_CHANGEADDR,	/* notify before the address change */
	NETDEV_GOING_DOWN,
	NETDEV_CHANGENAME,
	NETDEV_FEAT_CHANGE,
	NETDEV_BONDING_FAILOVER,
	NETDEV_PRE_UP,
	NETDEV_PRE_TYPE_CHANGE,
	NETDEV_POST_TYPE_CHANGE,
	NETDEV_POST_INIT,
	NETDEV_RELEASE,
	NETDEV_NOTIFY_PEERS,
	NETDEV_JOIN,
	NETDEV_CHANGEUPPER,
	NETDEV_RESEND_IGMP,
	NETDEV_PRECHANGEMTU,	/* notify before mtu change happened */
	NETDEV_CHANGEINFODATA,
	NETDEV_BONDING_INFO,
	NETDEV_PRECHANGEUPPER,
	NETDEV_CHANGELOWERSTATE,
	NETDEV_UDP_TUNNEL_PUSH_INFO,
	NETDEV_UDP_TUNNEL_DROP_INFO,
	NETDEV_CHANGE_TX_QUEUE_LEN,
	NETDEV_CVLAN_FILTER_PUSH_INFO,
	NETDEV_CVLAN_FILTER_DROP_INFO,
	NETDEV_SVLAN_FILTER_PUSH_INFO,
	NETDEV_SVLAN_FILTER_DROP_INFO,
};
const char *netdev_cmd_to_name(enum netdev_cmd cmd);

int register_netdevice_notifier(struct notifier_block *nb);
int unregister_netdevice_notifier(struct notifier_block *nb);
int register_netdevice_notifier_net(struct net *net, struct notifier_block *nb);
int unregister_netdevice_notifier_net(struct net *net,
				      struct notifier_block *nb);
int register_netdevice_notifier_dev_net(struct net_device *dev,
					struct notifier_block *nb,
					struct netdev_net_notifier *nn);
int unregister_netdevice_notifier_dev_net(struct net_device *dev,
					  struct notifier_block *nb,
					  struct netdev_net_notifier *nn);

struct netdev_notifier_info {
	struct net_device	*dev;
	struct netlink_ext_ack	*extack;
};

struct netdev_notifier_info_ext {
	struct netdev_notifier_info info; /* must be first */
	union {
		u32 mtu;
	} ext;
};

struct netdev_notifier_change_info {
	struct netdev_notifier_info info; /* must be first */
	unsigned int flags_changed;
};

struct netdev_notifier_changeupper_info {
	struct netdev_notifier_info info; /* must be first */
	struct net_device *upper_dev; /* new upper dev */
	bool master; /* is upper dev master */
	bool linking; /* is the notification for link or unlink */
	void *upper_info; /* upper dev info */
};

struct netdev_notifier_changelowerstate_info {
	struct netdev_notifier_info info; /* must be first */
	void *lower_state_info; /* is lower dev state */
};

struct netdev_notifier_pre_changeaddr_info {
	struct netdev_notifier_info info; /* must be first */
	const unsigned char *dev_addr;
};

static inline void netdev_notifier_info_init(struct netdev_notifier_info *info,
					     struct net_device *dev)
{
	info->dev = dev;
	info->extack = NULL;
}

static inline struct net_device *
netdev_notifier_info_to_dev(const struct netdev_notifier_info *info)
{
	return info->dev;
}

static inline struct netlink_ext_ack *
netdev_notifier_info_to_extack(const struct netdev_notifier_info *info)
{
	return info->extack;
}

int call_netdevice_notifiers(unsigned long val, struct net_device *dev);


extern rwlock_t				dev_base_lock;		/* Device list lock */

#define for_each_netdev(net, d)		\
		list_for_each_entry(d, &(net)->dev_base_head, dev_list)
#define for_each_netdev_reverse(net, d)	\
		list_for_each_entry_reverse(d, &(net)->dev_base_head, dev_list)
#define for_each_netdev_rcu(net, d)		\
		list_for_each_entry_rcu(d, &(net)->dev_base_head, dev_list)
#define for_each_netdev_safe(net, d, n)	\
		list_for_each_entry_safe(d, n, &(net)->dev_base_head, dev_list)
#define for_each_netdev_continue(net, d)		\
		list_for_each_entry_continue(d, &(net)->dev_base_head, dev_list)
#define for_each_netdev_continue_reverse(net, d)		\
		list_for_each_entry_continue_reverse(d, &(net)->dev_base_head, \
						     dev_list)
#define for_each_netdev_continue_rcu(net, d)		\
	list_for_each_entry_continue_rcu(d, &(net)->dev_base_head, dev_list)
#define for_each_netdev_in_bond_rcu(bond, slave)	\
		for_each_netdev_rcu(&init_net, slave)	\
			if (netdev_master_upper_dev_get_rcu(slave) == (bond))
#define net_device_entry(lh)	list_entry(lh, struct net_device, dev_list)

static inline struct net_device *next_net_device(struct net_device *dev)
{
	struct list_head *lh;
	struct net *net;

	net = dev_net(dev);
	lh = dev->dev_list.next;
	return lh == &net->dev_base_head ? NULL : net_device_entry(lh);
}

static inline struct net_device *next_net_device_rcu(struct net_device *dev)
{
	struct list_head *lh;
	struct net *net;

	net = dev_net(dev);
	lh = rcu_dereference(list_next_rcu(&dev->dev_list));
	return lh == &net->dev_base_head ? NULL : net_device_entry(lh);
}

static inline struct net_device *first_net_device(struct net *net)
{
	return list_empty(&net->dev_base_head) ? NULL :
		net_device_entry(net->dev_base_head.next);
}

static inline struct net_device *first_net_device_rcu(struct net *net)
{
	struct list_head *lh = rcu_dereference(list_next_rcu(&net->dev_base_head));

	return lh == &net->dev_base_head ? NULL : net_device_entry(lh);
}

int netdev_boot_setup_check(struct net_device *dev);
struct net_device *dev_getbyhwaddr_rcu(struct net *net, unsigned short type,
				       const char *hwaddr);
struct net_device *dev_getfirstbyhwtype(struct net *net, unsigned short type);
void dev_add_pack(struct packet_type *pt);
void dev_remove_pack(struct packet_type *pt);
void __dev_remove_pack(struct packet_type *pt);
void dev_add_offload(struct packet_offload *po);
void dev_remove_offload(struct packet_offload *po);

int dev_get_iflink(const struct net_device *dev);
int dev_fill_metadata_dst(struct net_device *dev, struct sk_buff *skb);
int dev_fill_forward_path(const struct net_device *dev, const u8 *daddr,
			  struct net_device_path_stack *stack);
struct net_device *__dev_get_by_flags(struct net *net, unsigned short flags,
				      unsigned short mask);
struct net_device *dev_get_by_name(struct net *net, const char *name);
struct net_device *dev_get_by_name_rcu(struct net *net, const char *name);
struct net_device *__dev_get_by_name(struct net *net, const char *name);
bool netdev_name_in_use(struct net *net, const char *name);
int dev_alloc_name(struct net_device *dev, const char *name);
int dev_open(struct net_device *dev, struct netlink_ext_ack *extack);
void dev_close(struct net_device *dev);
void dev_close_many(struct list_head *head, bool unlink);
void dev_disable_lro(struct net_device *dev);
int dev_loopback_xmit(struct net *net, struct sock *sk, struct sk_buff *newskb);
u16 dev_pick_tx_zero(struct net_device *dev, struct sk_buff *skb,
		     struct net_device *sb_dev);
u16 dev_pick_tx_cpu_id(struct net_device *dev, struct sk_buff *skb,
		       struct net_device *sb_dev);

int dev_queue_xmit(struct sk_buff *skb);
int dev_queue_xmit_accel(struct sk_buff *skb, struct net_device *sb_dev);
int __dev_direct_xmit(struct sk_buff *skb, u16 queue_id);

static inline int dev_direct_xmit(struct sk_buff *skb, u16 queue_id)
{
	int ret;

	ret = __dev_direct_xmit(skb, queue_id);
	if (!dev_xmit_complete(ret))
		kfree_skb(skb);
	return ret;
}

int register_netdevice(struct net_device *dev);
void unregister_netdevice_queue(struct net_device *dev, struct list_head *head);
void unregister_netdevice_many(struct list_head *head);
static inline void unregister_netdevice(struct net_device *dev)
{
	unregister_netdevice_queue(dev, NULL);
}

int netdev_refcnt_read(const struct net_device *dev);
void free_netdev(struct net_device *dev);
void netdev_freemem(struct net_device *dev);
int init_dummy_netdev(struct net_device *dev);

struct net_device *netdev_get_xmit_slave(struct net_device *dev,
					 struct sk_buff *skb,
					 bool all_slaves);
struct net_device *netdev_sk_get_lowest_dev(struct net_device *dev,
					    struct sock *sk);
struct net_device *dev_get_by_index(struct net *net, int ifindex);
struct net_device *__dev_get_by_index(struct net *net, int ifindex);
struct net_device *dev_get_by_index_rcu(struct net *net, int ifindex);
struct net_device *dev_get_by_napi_id(unsigned int napi_id);
int netdev_get_name(struct net *net, char *name, int ifindex);
int dev_restart(struct net_device *dev);
int skb_gro_receive(struct sk_buff *p, struct sk_buff *skb);
int skb_gro_receive_list(struct sk_buff *p, struct sk_buff *skb);

static inline unsigned int skb_gro_offset(const struct sk_buff *skb)
{
	return NAPI_GRO_CB(skb)->data_offset;
}

static inline unsigned int skb_gro_len(const struct sk_buff *skb)
{
	return skb->len - NAPI_GRO_CB(skb)->data_offset;
}

static inline void skb_gro_pull(struct sk_buff *skb, unsigned int len)
{
	NAPI_GRO_CB(skb)->data_offset += len;
}

static inline void *skb_gro_header_fast(struct sk_buff *skb,
					unsigned int offset)
{
	return NAPI_GRO_CB(skb)->frag0 + offset;
}

static inline int skb_gro_header_hard(struct sk_buff *skb, unsigned int hlen)
{
	return NAPI_GRO_CB(skb)->frag0_len < hlen;
}

static inline void skb_gro_frag0_invalidate(struct sk_buff *skb)
{
	NAPI_GRO_CB(skb)->frag0 = NULL;
	NAPI_GRO_CB(skb)->frag0_len = 0;
}

static inline void *skb_gro_header_slow(struct sk_buff *skb, unsigned int hlen,
					unsigned int offset)
{
	if (!pskb_may_pull(skb, hlen))
		return NULL;

	skb_gro_frag0_invalidate(skb);
	return skb->data + offset;
}

static inline void *skb_gro_network_header(struct sk_buff *skb)
{
	return (NAPI_GRO_CB(skb)->frag0 ?: skb->data) +
	       skb_network_offset(skb);
}

static inline void skb_gro_postpull_rcsum(struct sk_buff *skb,
					const void *start, unsigned int len)
{
	if (NAPI_GRO_CB(skb)->csum_valid)
		NAPI_GRO_CB(skb)->csum = csum_sub(NAPI_GRO_CB(skb)->csum,
						  csum_partial(start, len, 0));
}

/* GRO checksum functions. These are logical equivalents of the normal
 * checksum functions (in skbuff.h) except that they operate on the GRO
 * offsets and fields in sk_buff.
 */

__sum16 __skb_gro_checksum_complete(struct sk_buff *skb);

static inline bool skb_at_gro_remcsum_start(struct sk_buff *skb)
{
	return (NAPI_GRO_CB(skb)->gro_remcsum_start == skb_gro_offset(skb));
}

static inline bool __skb_gro_checksum_validate_needed(struct sk_buff *skb,
						      bool zero_okay,
						      __sum16 check)
{
	return ((skb->ip_summed != CHECKSUM_PARTIAL ||
		skb_checksum_start_offset(skb) <
		 skb_gro_offset(skb)) &&
		!skb_at_gro_remcsum_start(skb) &&
		NAPI_GRO_CB(skb)->csum_cnt == 0 &&
		(!zero_okay || check));
}

static inline __sum16 __skb_gro_checksum_validate_complete(struct sk_buff *skb,
							   __wsum psum)
{
	if (NAPI_GRO_CB(skb)->csum_valid &&
	    !csum_fold(csum_add(psum, NAPI_GRO_CB(skb)->csum)))
		return 0;

	NAPI_GRO_CB(skb)->csum = psum;

	return __skb_gro_checksum_complete(skb);
}

static inline void skb_gro_incr_csum_unnecessary(struct sk_buff *skb)
{
	if (NAPI_GRO_CB(skb)->csum_cnt > 0) {
		/* Consume a checksum from CHECKSUM_UNNECESSARY */
		NAPI_GRO_CB(skb)->csum_cnt--;
	} else {
		/* Update skb for CHECKSUM_UNNECESSARY and csum_level when we
		 * verified a new top level checksum or an encapsulated one
		 * during GRO. This saves work if we fallback to normal path.
		 */
		__skb_incr_checksum_unnecessary(skb);
	}
}

#define __skb_gro_checksum_validate(skb, proto, zero_okay, check,	\
				    compute_pseudo)			\
({									\
	__sum16 __ret = 0;						\
	if (__skb_gro_checksum_validate_needed(skb, zero_okay, check))	\
		__ret = __skb_gro_checksum_validate_complete(skb,	\
				compute_pseudo(skb, proto));		\
	if (!__ret)							\
		skb_gro_incr_csum_unnecessary(skb);			\
	__ret;								\
})

#define skb_gro_checksum_validate(skb, proto, compute_pseudo)		\
	__skb_gro_checksum_validate(skb, proto, false, 0, compute_pseudo)

#define skb_gro_checksum_validate_zero_check(skb, proto, check,		\
					     compute_pseudo)		\
	__skb_gro_checksum_validate(skb, proto, true, check, compute_pseudo)

#define skb_gro_checksum_simple_validate(skb)				\
	__skb_gro_checksum_validate(skb, 0, false, 0, null_compute_pseudo)

static inline bool __skb_gro_checksum_convert_check(struct sk_buff *skb)
{
	return (NAPI_GRO_CB(skb)->csum_cnt == 0 &&
		!NAPI_GRO_CB(skb)->csum_valid);
}

static inline void __skb_gro_checksum_convert(struct sk_buff *skb,
					      __wsum pseudo)
{
	NAPI_GRO_CB(skb)->csum = ~pseudo;
	NAPI_GRO_CB(skb)->csum_valid = 1;
}

#define skb_gro_checksum_try_convert(skb, proto, compute_pseudo)	\
do {									\
	if (__skb_gro_checksum_convert_check(skb))			\
		__skb_gro_checksum_convert(skb, 			\
					   compute_pseudo(skb, proto));	\
} while (0)

struct gro_remcsum {
	int offset;
	__wsum delta;
};

static inline void skb_gro_remcsum_init(struct gro_remcsum *grc)
{
	grc->offset = 0;
	grc->delta = 0;
}

static inline void *skb_gro_remcsum_process(struct sk_buff *skb, void *ptr,
					    unsigned int off, size_t hdrlen,
					    int start, int offset,
					    struct gro_remcsum *grc,
					    bool nopartial)
{
	__wsum delta;
	size_t plen = hdrlen + max_t(size_t, offset + sizeof(u16), start);

	BUG_ON(!NAPI_GRO_CB(skb)->csum_valid);

	if (!nopartial) {
		NAPI_GRO_CB(skb)->gro_remcsum_start = off + hdrlen + start;
		return ptr;
	}

	ptr = skb_gro_header_fast(skb, off);
	if (skb_gro_header_hard(skb, off + plen)) {
		ptr = skb_gro_header_slow(skb, off + plen, off);
		if (!ptr)
			return NULL;
	}

	delta = remcsum_adjust(ptr + hdrlen, NAPI_GRO_CB(skb)->csum,
			       start, offset);

	/* Adjust skb->csum since we changed the packet */
	NAPI_GRO_CB(skb)->csum = csum_add(NAPI_GRO_CB(skb)->csum, delta);

	grc->offset = off + hdrlen + offset;
	grc->delta = delta;

	return ptr;
}

static inline void skb_gro_remcsum_cleanup(struct sk_buff *skb,
					   struct gro_remcsum *grc)
{
	void *ptr;
	size_t plen = grc->offset + sizeof(u16);

	if (!grc->delta)
		return;

	ptr = skb_gro_header_fast(skb, grc->offset);
	if (skb_gro_header_hard(skb, grc->offset + sizeof(u16))) {
		ptr = skb_gro_header_slow(skb, plen, grc->offset);
		if (!ptr)
			return;
	}

	remcsum_unadjust((__sum16 *)ptr, grc->delta);
}

#ifdef CONFIG_XFRM_OFFLOAD
static inline void skb_gro_flush_final(struct sk_buff *skb, struct sk_buff *pp, int flush)
{
	if (PTR_ERR(pp) != -EINPROGRESS)
		NAPI_GRO_CB(skb)->flush |= flush;
}
static inline void skb_gro_flush_final_remcsum(struct sk_buff *skb,
					       struct sk_buff *pp,
					       int flush,
					       struct gro_remcsum *grc)
{
	if (PTR_ERR(pp) != -EINPROGRESS) {
		NAPI_GRO_CB(skb)->flush |= flush;
		skb_gro_remcsum_cleanup(skb, grc);
		skb->remcsum_offload = 0;
	}
}
#else
static inline void skb_gro_flush_final(struct sk_buff *skb, struct sk_buff *pp, int flush)
{
	NAPI_GRO_CB(skb)->flush |= flush;
}
static inline void skb_gro_flush_final_remcsum(struct sk_buff *skb,
					       struct sk_buff *pp,
					       int flush,
					       struct gro_remcsum *grc)
{
	NAPI_GRO_CB(skb)->flush |= flush;
	skb_gro_remcsum_cleanup(skb, grc);
	skb->remcsum_offload = 0;
}
#endif

static inline int dev_hard_header(struct sk_buff *skb, struct net_device *dev,
				  unsigned short type,
				  const void *daddr, const void *saddr,
				  unsigned int len)
{
	if (!dev->header_ops || !dev->header_ops->create)
		return 0;

	return dev->header_ops->create(skb, dev, type, daddr, saddr, len);
}

static inline int dev_parse_header(const struct sk_buff *skb,
				   unsigned char *haddr)
{
	const struct net_device *dev = skb->dev;

	if (!dev->header_ops || !dev->header_ops->parse)
		return 0;
	return dev->header_ops->parse(skb, haddr);
}

static inline __be16 dev_parse_header_protocol(const struct sk_buff *skb)
{
	const struct net_device *dev = skb->dev;

	if (!dev->header_ops || !dev->header_ops->parse_protocol)
		return 0;
	return dev->header_ops->parse_protocol(skb);
}

static inline bool dev_has_header(const struct net_device *dev)
{
	return dev->header_ops && dev->header_ops->create;
}

#ifdef CONFIG_NET_FLOW_LIMIT
#define FLOW_LIMIT_HISTORY	(1 << 7)  /* must be ^2 and !overflow buckets */
struct sd_flow_limit {
	u64			count;
	unsigned int		num_buckets;
	unsigned int		history_head;
	u16			history[FLOW_LIMIT_HISTORY];
	u8			buckets[];
};

extern int netdev_flow_limit_table_len;
#endif /* CONFIG_NET_FLOW_LIMIT */

/*
 * Incoming packets are placed on per-CPU queues
 */
struct softnet_data {
	struct list_head	poll_list;
	struct sk_buff_head	process_queue;

	/* stats */
	unsigned int		processed;
	unsigned int		time_squeeze;
	unsigned int		received_rps;
#ifdef CONFIG_RPS
	struct softnet_data	*rps_ipi_list;
#endif
#ifdef CONFIG_NET_FLOW_LIMIT
	struct sd_flow_limit __rcu *flow_limit;
#endif
	struct Qdisc		*output_queue;
	struct Qdisc		**output_queue_tailp;
	struct sk_buff		*completion_queue;
#ifdef CONFIG_XFRM_OFFLOAD
	struct sk_buff_head	xfrm_backlog;
#endif
	/* written and read only by owning cpu: */
	struct {
		u16 recursion;
		u8  more;
	} xmit;
#ifdef CONFIG_RPS
	/* input_queue_head should be written by cpu owning this struct,
	 * and only read by other cpus. Worth using a cache line.
	 */
	unsigned int		input_queue_head ____cacheline_aligned_in_smp;

	/* Elements below can be accessed between CPUs for RPS/RFS */
	call_single_data_t	csd ____cacheline_aligned_in_smp;
	struct softnet_data	*rps_ipi_next;
	unsigned int		cpu;
	unsigned int		input_queue_tail;
#endif
	unsigned int		dropped;
	struct sk_buff_head	input_pkt_queue;
	struct napi_struct	backlog;

};

static inline void input_queue_head_incr(struct softnet_data *sd)
{
#ifdef CONFIG_RPS
	sd->input_queue_head++;
#endif
}

static inline void input_queue_tail_incr_save(struct softnet_data *sd,
					      unsigned int *qtail)
{
#ifdef CONFIG_RPS
	*qtail = ++sd->input_queue_tail;
#endif
}

DECLARE_PER_CPU_ALIGNED(struct softnet_data, softnet_data);

static inline int dev_recursion_level(void)
{
	return this_cpu_read(softnet_data.xmit.recursion);
}

#define XMIT_RECURSION_LIMIT	8
static inline bool dev_xmit_recursion(void)
{
	return unlikely(__this_cpu_read(softnet_data.xmit.recursion) >
			XMIT_RECURSION_LIMIT);
}

static inline void dev_xmit_recursion_inc(void)
{
	__this_cpu_inc(softnet_data.xmit.recursion);
}

static inline void dev_xmit_recursion_dec(void)
{
	__this_cpu_dec(softnet_data.xmit.recursion);
}

void __netif_schedule(struct Qdisc *q);
void netif_schedule_queue(struct netdev_queue *txq);

static inline void netif_tx_schedule_all(struct net_device *dev)
{
	unsigned int i;

	for (i = 0; i < dev->num_tx_queues; i++)
		netif_schedule_queue(netdev_get_tx_queue(dev, i));
}

static __always_inline void netif_tx_start_queue(struct netdev_queue *dev_queue)
{
	clear_bit(__QUEUE_STATE_DRV_XOFF, &dev_queue->state);
}

/**
 *	netif_start_queue - allow transmit
 *	@dev: network device
 *
 *	Allow upper layers to call the device hard_start_xmit routine.
 */
static inline void netif_start_queue(struct net_device *dev)
{
	netif_tx_start_queue(netdev_get_tx_queue(dev, 0));
}

static inline void netif_tx_start_all_queues(struct net_device *dev)
{
	unsigned int i;

	for (i = 0; i < dev->num_tx_queues; i++) {
		struct netdev_queue *txq = netdev_get_tx_queue(dev, i);
		netif_tx_start_queue(txq);
	}
}

void netif_tx_wake_queue(struct netdev_queue *dev_queue);

/**
 *	netif_wake_queue - restart transmit
 *	@dev: network device
 *
 *	Allow upper layers to call the device hard_start_xmit routine.
 *	Used for flow control when transmit resources are available.
 */
static inline void netif_wake_queue(struct net_device *dev)
{
	netif_tx_wake_queue(netdev_get_tx_queue(dev, 0));
}

static inline void netif_tx_wake_all_queues(struct net_device *dev)
{
	unsigned int i;

	for (i = 0; i < dev->num_tx_queues; i++) {
		struct netdev_queue *txq = netdev_get_tx_queue(dev, i);
		netif_tx_wake_queue(txq);
	}
}

static __always_inline void netif_tx_stop_queue(struct netdev_queue *dev_queue)
{
	set_bit(__QUEUE_STATE_DRV_XOFF, &dev_queue->state);
}

/**
 *	netif_stop_queue - stop transmitted packets
 *	@dev: network device
 *
 *	Stop upper layers calling the device hard_start_xmit routine.
 *	Used for flow control when transmit resources are unavailable.
 */
static inline void netif_stop_queue(struct net_device *dev)
{
	netif_tx_stop_queue(netdev_get_tx_queue(dev, 0));
}

void netif_tx_stop_all_queues(struct net_device *dev);

static inline bool netif_tx_queue_stopped(const struct netdev_queue *dev_queue)
{
	return test_bit(__QUEUE_STATE_DRV_XOFF, &dev_queue->state);
}

/**
 *	netif_queue_stopped - test if transmit queue is flowblocked
 *	@dev: network device
 *
 *	Test if transmit queue on device is currently unable to send.
 */
static inline bool netif_queue_stopped(const struct net_device *dev)
{
	return netif_tx_queue_stopped(netdev_get_tx_queue(dev, 0));
}

static inline bool netif_xmit_stopped(const struct netdev_queue *dev_queue)
{
	return dev_queue->state & QUEUE_STATE_ANY_XOFF;
}

static inline bool
netif_xmit_frozen_or_stopped(const struct netdev_queue *dev_queue)
{
	return dev_queue->state & QUEUE_STATE_ANY_XOFF_OR_FROZEN;
}

static inline bool
netif_xmit_frozen_or_drv_stopped(const struct netdev_queue *dev_queue)
{
	return dev_queue->state & QUEUE_STATE_DRV_XOFF_OR_FROZEN;
}

/**
 *	netdev_queue_set_dql_min_limit - set dql minimum limit
 *	@dev_queue: pointer to transmit queue
 *	@min_limit: dql minimum limit
 *
 * Forces xmit_more() to return true until the minimum threshold
 * defined by @min_limit is reached (or until the tx queue is
 * empty). Warning: to be use with care, misuse will impact the
 * latency.
 */
static inline void netdev_queue_set_dql_min_limit(struct netdev_queue *dev_queue,
						  unsigned int min_limit)
{
#ifdef CONFIG_BQL
	dev_queue->dql.min_limit = min_limit;
#endif
}

static inline void netdev_tx_sent_queue(struct netdev_queue *dev_queue,
					unsigned int bytes)
{
#ifdef CONFIG_BQL
	dql_queued(&dev_queue->dql, bytes);

	if (likely(dql_avail(&dev_queue->dql) >= 0))
		return;

	set_bit(__QUEUE_STATE_STACK_XOFF, &dev_queue->state);

	/*
	 * The XOFF flag must be set before checking the dql_avail below,
	 * because in netdev_tx_completed_queue we update the dql_completed
	 * before checking the XOFF flag.
	 */
	smp_mb();

	/* check again in case another CPU has just made room avail */
	if (unlikely(dql_avail(&dev_queue->dql) >= 0))
		clear_bit(__QUEUE_STATE_STACK_XOFF, &dev_queue->state);
#endif
}

/* Variant of netdev_tx_sent_queue() for drivers that are aware
 * that they should not test BQL status themselves.
 * We do want to change __QUEUE_STATE_STACK_XOFF only for the last
 * skb of a batch.
 * Returns true if the doorbell must be used to kick the NIC.
 */
static inline bool __netdev_tx_sent_queue(struct netdev_queue *dev_queue,
					  unsigned int bytes,
					  bool xmit_more)
{
	if (xmit_more) {
#ifdef CONFIG_BQL
		dql_queued(&dev_queue->dql, bytes);
#endif
		return netif_tx_queue_stopped(dev_queue);
	}
	netdev_tx_sent_queue(dev_queue, bytes);
	return true;
}

/**
 * 	netdev_sent_queue - report the number of bytes queued to hardware
 * 	@dev: network device
 * 	@bytes: number of bytes queued to the hardware device queue
 *
 * 	Report the number of bytes queued for sending/completion to the network
 * 	device hardware queue. @bytes should be a good approximation and should
 * 	exactly match netdev_completed_queue() @bytes
 */
static inline void netdev_sent_queue(struct net_device *dev, unsigned int bytes)
{
	netdev_tx_sent_queue(netdev_get_tx_queue(dev, 0), bytes);
}

static inline bool __netdev_sent_queue(struct net_device *dev,
				       unsigned int bytes,
				       bool xmit_more)
{
	return __netdev_tx_sent_queue(netdev_get_tx_queue(dev, 0), bytes,
				      xmit_more);
}

static inline void netdev_tx_completed_queue(struct netdev_queue *dev_queue,
					     unsigned int pkts, unsigned int bytes)
{
#ifdef CONFIG_BQL
	if (unlikely(!bytes))
		return;

	dql_completed(&dev_queue->dql, bytes);

	/*
	 * Without the memory barrier there is a small possiblity that
	 * netdev_tx_sent_queue will miss the update and cause the queue to
	 * be stopped forever
	 */
	smp_mb();

	if (unlikely(dql_avail(&dev_queue->dql) < 0))
		return;

	if (test_and_clear_bit(__QUEUE_STATE_STACK_XOFF, &dev_queue->state))
		netif_schedule_queue(dev_queue);
#endif
}

/**
 * 	netdev_completed_queue - report bytes and packets completed by device
 * 	@dev: network device
 * 	@pkts: actual number of packets sent over the medium
 * 	@bytes: actual number of bytes sent over the medium
 *
 * 	Report the number of bytes and packets transmitted by the network device
 * 	hardware queue over the physical medium, @bytes must exactly match the
 * 	@bytes amount passed to netdev_sent_queue()
 */
static inline void netdev_completed_queue(struct net_device *dev,
					  unsigned int pkts, unsigned int bytes)
{
	netdev_tx_completed_queue(netdev_get_tx_queue(dev, 0), pkts, bytes);
}

static inline void netdev_tx_reset_queue(struct netdev_queue *q)
{
#ifdef CONFIG_BQL
	clear_bit(__QUEUE_STATE_STACK_XOFF, &q->state);
	dql_reset(&q->dql);
#endif
}

/**
 * 	netdev_reset_queue - reset the packets and bytes count of a network device
 * 	@dev_queue: network device
 *
 * 	Reset the bytes and packet count of a network device and clear the
 * 	software flow control OFF bit for this network device
 */
static inline void netdev_reset_queue(struct net_device *dev_queue)
{
	netdev_tx_reset_queue(netdev_get_tx_queue(dev_queue, 0));
}

extern int net_ratelimit(void);

/**
 * 	netdev_cap_txqueue - check if selected tx queue exceeds device queues
 * 	@dev: network device
 * 	@queue_index: given tx queue index
 *
 * 	Returns 0 if given tx queue index >= number of device tx queues,
 * 	otherwise returns the originally passed tx queue index.
 */
static inline u16 netdev_cap_txqueue(struct net_device *dev, u16 queue_index)
{
	if (unlikely(queue_index >= dev->real_num_tx_queues)) {

		if (net_ratelimit())
			pr_warn("%s selects TX queue %d, but real number of TX queues is %d\n",
				     dev->name, queue_index,
				     dev->real_num_tx_queues);
		return 0;
	}

	return queue_index;
}

/**
 *	netif_running - test if up
 *	@dev: network device
 *
 *	Test if the device has been brought up.
 */
static inline bool netif_running(const struct net_device *dev)
{
	return test_bit(__LINK_STATE_START, &dev->state);
}

/*
 * Routines to manage the subqueues on a device.  We only need start,
 * stop, and a check if it's stopped.  All other device management is
 * done at the overall netdevice level.
 * Also test the device if we're multiqueue.
 */

/**
 *	netif_start_subqueue - allow sending packets on subqueue
 *	@dev: network device
 *	@queue_index: sub queue index
 *
 * Start individual transmit queue of a device with multiple transmit queues.
 */
static inline void netif_start_subqueue(struct net_device *dev, u16 queue_index)
{
	struct netdev_queue *txq = netdev_get_tx_queue(dev, queue_index);

	netif_tx_start_queue(txq);
}

/**
 *	netif_stop_subqueue - stop sending packets on subqueue
 *	@dev: network device
 *	@queue_index: sub queue index
 *
 * Stop individual transmit queue of a device with multiple transmit queues.
 */
static inline void netif_stop_subqueue(struct net_device *dev, u16 queue_index)
{
	struct netdev_queue *txq = netdev_get_tx_queue(dev, queue_index);
	netif_tx_stop_queue(txq);
}

/**
 *	__netif_subqueue_stopped - test status of subqueue
 *	@dev: network device
 *	@queue_index: sub queue index
 *
 * Check individual transmit queue of a device with multiple transmit queues.
 */
static inline bool __netif_subqueue_stopped(const struct net_device *dev,
					    u16 queue_index)
{
	struct netdev_queue *txq = netdev_get_tx_queue(dev, queue_index);

	return netif_tx_queue_stopped(txq);
}

/**
 *	netif_subqueue_stopped - test status of subqueue
 *	@dev: network device
 *	@skb: sub queue buffer pointer
 *
 * Check individual transmit queue of a device with multiple transmit queues.
 */
static inline bool netif_subqueue_stopped(const struct net_device *dev,
					  struct sk_buff *skb)
{
	return __netif_subqueue_stopped(dev, skb_get_queue_mapping(skb));
}

/**
 *	netif_wake_subqueue - allow sending packets on subqueue
 *	@dev: network device
 *	@queue_index: sub queue index
 *
 * Resume individual transmit queue of a device with multiple transmit queues.
 */
static inline void netif_wake_subqueue(struct net_device *dev, u16 queue_index)
{
	struct netdev_queue *txq = netdev_get_tx_queue(dev, queue_index);

	netif_tx_wake_queue(txq);
}

#ifdef CONFIG_XPS
int netif_set_xps_queue(struct net_device *dev, const struct cpumask *mask,
			u16 index);
int __netif_set_xps_queue(struct net_device *dev, const unsigned long *mask,
			  u16 index, enum xps_map_type type);

/**
 *	netif_attr_test_mask - Test a CPU or Rx queue set in a mask
 *	@j: CPU/Rx queue index
 *	@mask: bitmask of all cpus/rx queues
 *	@nr_bits: number of bits in the bitmask
 *
 * Test if a CPU or Rx queue index is set in a mask of all CPU/Rx queues.
 */
static inline bool netif_attr_test_mask(unsigned long j,
					const unsigned long *mask,
					unsigned int nr_bits)
{
	cpu_max_bits_warn(j, nr_bits);
	return test_bit(j, mask);
}

/**
 *	netif_attr_test_online - Test for online CPU/Rx queue
 *	@j: CPU/Rx queue index
 *	@online_mask: bitmask for CPUs/Rx queues that are online
 *	@nr_bits: number of bits in the bitmask
 *
 * Returns true if a CPU/Rx queue is online.
 */
static inline bool netif_attr_test_online(unsigned long j,
					  const unsigned long *online_mask,
					  unsigned int nr_bits)
{
	cpu_max_bits_warn(j, nr_bits);

	if (online_mask)
		return test_bit(j, online_mask);

	return (j < nr_bits);
}

/**
 *	netif_attrmask_next - get the next CPU/Rx queue in a cpu/Rx queues mask
 *	@n: CPU/Rx queue index
 *	@srcp: the cpumask/Rx queue mask pointer
 *	@nr_bits: number of bits in the bitmask
 *
 * Returns >= nr_bits if no further CPUs/Rx queues set.
 */
static inline unsigned int netif_attrmask_next(int n, const unsigned long *srcp,
					       unsigned int nr_bits)
{
	/* -1 is a legal arg here. */
	if (n != -1)
		cpu_max_bits_warn(n, nr_bits);

	if (srcp)
		return find_next_bit(srcp, nr_bits, n + 1);

	return n + 1;
}

/**
 *	netif_attrmask_next_and - get the next CPU/Rx queue in \*src1p & \*src2p
 *	@n: CPU/Rx queue index
 *	@src1p: the first CPUs/Rx queues mask pointer
 *	@src2p: the second CPUs/Rx queues mask pointer
 *	@nr_bits: number of bits in the bitmask
 *
 * Returns >= nr_bits if no further CPUs/Rx queues set in both.
 */
static inline int netif_attrmask_next_and(int n, const unsigned long *src1p,
					  const unsigned long *src2p,
					  unsigned int nr_bits)
{
	/* -1 is a legal arg here. */
	if (n != -1)
		cpu_max_bits_warn(n, nr_bits);

	if (src1p && src2p)
		return find_next_and_bit(src1p, src2p, nr_bits, n + 1);
	else if (src1p)
		return find_next_bit(src1p, nr_bits, n + 1);
	else if (src2p)
		return find_next_bit(src2p, nr_bits, n + 1);

	return n + 1;
}
#else
static inline int netif_set_xps_queue(struct net_device *dev,
				      const struct cpumask *mask,
				      u16 index)
{
	return 0;
}

static inline int __netif_set_xps_queue(struct net_device *dev,
					const unsigned long *mask,
					u16 index, enum xps_map_type type)
{
	return 0;
}
#endif

/**
 *	netif_is_multiqueue - test if device has multiple transmit queues
 *	@dev: network device
 *
 * Check if device has multiple transmit queues
 */
static inline bool netif_is_multiqueue(const struct net_device *dev)
{
	return dev->num_tx_queues > 1;
}

int netif_set_real_num_tx_queues(struct net_device *dev, unsigned int txq);

#ifdef CONFIG_SYSFS
int netif_set_real_num_rx_queues(struct net_device *dev, unsigned int rxq);
#else
static inline int netif_set_real_num_rx_queues(struct net_device *dev,
						unsigned int rxqs)
{
	dev->real_num_rx_queues = rxqs;
	return 0;
}
#endif
int netif_set_real_num_queues(struct net_device *dev,
			      unsigned int txq, unsigned int rxq);

static inline struct netdev_rx_queue *
__netif_get_rx_queue(struct net_device *dev, unsigned int rxq)
{
	return dev->_rx + rxq;
}

#ifdef CONFIG_SYSFS
static inline unsigned int get_netdev_rx_queue_index(
		struct netdev_rx_queue *queue)
{
	struct net_device *dev = queue->dev;
	int index = queue - dev->_rx;

	BUG_ON(index >= dev->num_rx_queues);
	return index;
}
#endif

#define DEFAULT_MAX_NUM_RSS_QUEUES	(8)
int netif_get_num_default_rss_queues(void);

enum skb_free_reason {
	SKB_REASON_CONSUMED,
	SKB_REASON_DROPPED,
};

void __dev_kfree_skb_irq(struct sk_buff *skb, enum skb_free_reason reason);
void __dev_kfree_skb_any(struct sk_buff *skb, enum skb_free_reason reason);

/*
 * It is not allowed to call kfree_skb() or consume_skb() from hardware
 * interrupt context or with hardware interrupts being disabled.
 * (in_hardirq() || irqs_disabled())
 *
 * We provide four helpers that can be used in following contexts :
 *
 * dev_kfree_skb_irq(skb) when caller drops a packet from irq context,
 *  replacing kfree_skb(skb)
 *
 * dev_consume_skb_irq(skb) when caller consumes a packet from irq context.
 *  Typically used in place of consume_skb(skb) in TX completion path
 *
 * dev_kfree_skb_any(skb) when caller doesn't know its current irq context,
 *  replacing kfree_skb(skb)
 *
 * dev_consume_skb_any(skb) when caller doesn't know its current irq context,
 *  and consumed a packet. Used in place of consume_skb(skb)
 */
static inline void dev_kfree_skb_irq(struct sk_buff *skb)
{
	__dev_kfree_skb_irq(skb, SKB_REASON_DROPPED);
}

static inline void dev_consume_skb_irq(struct sk_buff *skb)
{
	__dev_kfree_skb_irq(skb, SKB_REASON_CONSUMED);
}

static inline void dev_kfree_skb_any(struct sk_buff *skb)
{
	__dev_kfree_skb_any(skb, SKB_REASON_DROPPED);
}

static inline void dev_consume_skb_any(struct sk_buff *skb)
{
	__dev_kfree_skb_any(skb, SKB_REASON_CONSUMED);
}

u32 bpf_prog_run_generic_xdp(struct sk_buff *skb, struct xdp_buff *xdp,
			     struct bpf_prog *xdp_prog);
void generic_xdp_tx(struct sk_buff *skb, struct bpf_prog *xdp_prog);
int do_xdp_generic(struct bpf_prog *xdp_prog, struct sk_buff *skb);
int netif_rx(struct sk_buff *skb);
int netif_rx_ni(struct sk_buff *skb);
int netif_rx_any_context(struct sk_buff *skb);
int netif_receive_skb(struct sk_buff *skb);
int netif_receive_skb_core(struct sk_buff *skb);
void netif_receive_skb_list(struct list_head *head);
gro_result_t napi_gro_receive(struct napi_struct *napi, struct sk_buff *skb);
void napi_gro_flush(struct napi_struct *napi, bool flush_old);
struct sk_buff *napi_get_frags(struct napi_struct *napi);
gro_result_t napi_gro_frags(struct napi_struct *napi);
struct packet_offload *gro_find_receive_by_type(__be16 type);
struct packet_offload *gro_find_complete_by_type(__be16 type);

static inline void napi_free_frags(struct napi_struct *napi)
{
	kfree_skb(napi->skb);
	napi->skb = NULL;
}

bool netdev_is_rx_handler_busy(struct net_device *dev);
int netdev_rx_handler_register(struct net_device *dev,
			       rx_handler_func_t *rx_handler,
			       void *rx_handler_data);
void netdev_rx_handler_unregister(struct net_device *dev);

bool dev_valid_name(const char *name);
static inline bool is_socket_ioctl_cmd(unsigned int cmd)
{
	return _IOC_TYPE(cmd) == SOCK_IOC_TYPE;
}
int get_user_ifreq(struct ifreq *ifr, void __user **ifrdata, void __user *arg);
int put_user_ifreq(struct ifreq *ifr, void __user *arg);
int dev_ioctl(struct net *net, unsigned int cmd, struct ifreq *ifr,
		void __user *data, bool *need_copyout);
int dev_ifconf(struct net *net, struct ifconf __user *ifc);
int dev_ethtool(struct net *net, struct ifreq *ifr, void __user *userdata);
unsigned int dev_get_flags(const struct net_device *);
int __dev_change_flags(struct net_device *dev, unsigned int flags,
		       struct netlink_ext_ack *extack);
int dev_change_flags(struct net_device *dev, unsigned int flags,
		     struct netlink_ext_ack *extack);
void __dev_notify_flags(struct net_device *, unsigned int old_flags,
			unsigned int gchanges);
int dev_change_name(struct net_device *, const char *);
int dev_set_alias(struct net_device *, const char *, size_t);
int dev_get_alias(const struct net_device *, char *, size_t);
int __dev_change_net_namespace(struct net_device *dev, struct net *net,
			       const char *pat, int new_ifindex);
static inline
int dev_change_net_namespace(struct net_device *dev, struct net *net,
			     const char *pat)
{
	return __dev_change_net_namespace(dev, net, pat, 0);
}
int __dev_set_mtu(struct net_device *, int);
int dev_validate_mtu(struct net_device *dev, int mtu,
		     struct netlink_ext_ack *extack);
int dev_set_mtu_ext(struct net_device *dev, int mtu,
		    struct netlink_ext_ack *extack);
int dev_set_mtu(struct net_device *, int);
int dev_change_tx_queue_len(struct net_device *, unsigned long);
void dev_set_group(struct net_device *, int);
int dev_pre_changeaddr_notify(struct net_device *dev, const char *addr,
			      struct netlink_ext_ack *extack);
int dev_set_mac_address(struct net_device *dev, struct sockaddr *sa,
			struct netlink_ext_ack *extack);
int dev_set_mac_address_user(struct net_device *dev, struct sockaddr *sa,
			     struct netlink_ext_ack *extack);
int dev_get_mac_address(struct sockaddr *sa, struct net *net, char *dev_name);
int dev_change_carrier(struct net_device *, bool new_carrier);
int dev_get_phys_port_id(struct net_device *dev,
			 struct netdev_phys_item_id *ppid);
int dev_get_phys_port_name(struct net_device *dev,
			   char *name, size_t len);
int dev_get_port_parent_id(struct net_device *dev,
			   struct netdev_phys_item_id *ppid, bool recurse);
bool netdev_port_same_parent_id(struct net_device *a, struct net_device *b);
int dev_change_proto_down(struct net_device *dev, bool proto_down);
int dev_change_proto_down_generic(struct net_device *dev, bool proto_down);
void dev_change_proto_down_reason(struct net_device *dev, unsigned long mask,
				  u32 value);
struct sk_buff *validate_xmit_skb_list(struct sk_buff *skb, struct net_device *dev, bool *again);
struct sk_buff *dev_hard_start_xmit(struct sk_buff *skb, struct net_device *dev,
				    struct netdev_queue *txq, int *ret);

typedef int (*bpf_op_t)(struct net_device *dev, struct netdev_bpf *bpf);
int dev_change_xdp_fd(struct net_device *dev, struct netlink_ext_ack *extack,
		      int fd, int expected_fd, u32 flags);
int bpf_xdp_link_attach(const union bpf_attr *attr, struct bpf_prog *prog);
u8 dev_xdp_prog_count(struct net_device *dev);
u32 dev_xdp_prog_id(struct net_device *dev, enum bpf_xdp_mode mode);

int __dev_forward_skb(struct net_device *dev, struct sk_buff *skb);
int dev_forward_skb(struct net_device *dev, struct sk_buff *skb);
int dev_forward_skb_nomtu(struct net_device *dev, struct sk_buff *skb);
bool is_skb_forwardable(const struct net_device *dev,
			const struct sk_buff *skb);

static __always_inline bool __is_skb_forwardable(const struct net_device *dev,
						 const struct sk_buff *skb,
						 const bool check_mtu)
{
	const u32 vlan_hdr_len = 4; /* VLAN_HLEN */
	unsigned int len;

	if (!(dev->flags & IFF_UP))
		return false;

	if (!check_mtu)
		return true;

	len = dev->mtu + dev->hard_header_len + vlan_hdr_len;
	if (skb->len <= len)
		return true;

	/* if TSO is enabled, we don't care about the length as the packet
	 * could be forwarded without being segmented before
	 */
	if (skb_is_gso(skb))
		return true;

	return false;
}

static __always_inline int ____dev_forward_skb(struct net_device *dev,
					       struct sk_buff *skb,
					       const bool check_mtu)
{
	if (skb_orphan_frags(skb, GFP_ATOMIC) ||
	    unlikely(!__is_skb_forwardable(dev, skb, check_mtu))) {
		atomic_long_inc(&dev->rx_dropped);
		kfree_skb(skb);
		return NET_RX_DROP;
	}

	skb_scrub_packet(skb, !net_eq(dev_net(dev), dev_net(skb->dev)));
	skb->priority = 0;
	return 0;
}

bool dev_nit_active(struct net_device *dev);
void dev_queue_xmit_nit(struct sk_buff *skb, struct net_device *dev);

extern int		netdev_budget;
extern unsigned int	netdev_budget_usecs;

/* Called by rtnetlink.c:rtnl_unlock() */
void netdev_run_todo(void);

/**
 *	dev_put - release reference to device
 *	@dev: network device
 *
 * Release reference to device to allow it to be freed.
 */
static inline void dev_put(struct net_device *dev)
{
	if (dev) {
#ifdef CONFIG_PCPU_DEV_REFCNT
		this_cpu_dec(*dev->pcpu_refcnt);
#else
		refcount_dec(&dev->dev_refcnt);
#endif
	}
}

/**
 *	dev_hold - get reference to device
 *	@dev: network device
 *
 * Hold reference to device to keep it from being freed.
 */
static inline void dev_hold(struct net_device *dev)
{
	if (dev) {
#ifdef CONFIG_PCPU_DEV_REFCNT
		this_cpu_inc(*dev->pcpu_refcnt);
#else
		refcount_inc(&dev->dev_refcnt);
#endif
	}
}

/* Carrier loss detection, dial on demand. The functions netif_carrier_on
 * and _off may be called from IRQ context, but it is caller
 * who is responsible for serialization of these calls.
 *
 * The name carrier is inappropriate, these functions should really be
 * called netif_lowerlayer_*() because they represent the state of any
 * kind of lower layer not just hardware media.
 */

void linkwatch_init_dev(struct net_device *dev);
void linkwatch_fire_event(struct net_device *dev);
void linkwatch_forget_dev(struct net_device *dev);

/**
 *	netif_carrier_ok - test if carrier present
 *	@dev: network device
 *
 * Check if carrier is present on device
 */
static inline bool netif_carrier_ok(const struct net_device *dev)
{
	return !test_bit(__LINK_STATE_NOCARRIER, &dev->state);
}

unsigned long dev_trans_start(struct net_device *dev);

void __netdev_watchdog_up(struct net_device *dev);

void netif_carrier_on(struct net_device *dev);
void netif_carrier_off(struct net_device *dev);
void netif_carrier_event(struct net_device *dev);

/**
 *	netif_dormant_on - mark device as dormant.
 *	@dev: network device
 *
 * Mark device as dormant (as per RFC2863).
 *
 * The dormant state indicates that the relevant interface is not
 * actually in a condition to pass packets (i.e., it is not 'up') but is
 * in a "pending" state, waiting for some external event.  For "on-
 * demand" interfaces, this new state identifies the situation where the
 * interface is waiting for events to place it in the up state.
 */
static inline void netif_dormant_on(struct net_device *dev)
{
	if (!test_and_set_bit(__LINK_STATE_DORMANT, &dev->state))
		linkwatch_fire_event(dev);
}

/**
 *	netif_dormant_off - set device as not dormant.
 *	@dev: network device
 *
 * Device is not in dormant state.
 */
static inline void netif_dormant_off(struct net_device *dev)
{
	if (test_and_clear_bit(__LINK_STATE_DORMANT, &dev->state))
		linkwatch_fire_event(dev);
}

/**
 *	netif_dormant - test if device is dormant
 *	@dev: network device
 *
 * Check if device is dormant.
 */
static inline bool netif_dormant(const struct net_device *dev)
{
	return test_bit(__LINK_STATE_DORMANT, &dev->state);
}


/**
 *	netif_testing_on - mark device as under test.
 *	@dev: network device
 *
 * Mark device as under test (as per RFC2863).
 *
 * The testing state indicates that some test(s) must be performed on
 * the interface. After completion, of the test, the interface state
 * will change to up, dormant, or down, as appropriate.
 */
static inline void netif_testing_on(struct net_device *dev)
{
	if (!test_and_set_bit(__LINK_STATE_TESTING, &dev->state))
		linkwatch_fire_event(dev);
}

/**
 *	netif_testing_off - set device as not under test.
 *	@dev: network device
 *
 * Device is not in testing state.
 */
static inline void netif_testing_off(struct net_device *dev)
{
	if (test_and_clear_bit(__LINK_STATE_TESTING, &dev->state))
		linkwatch_fire_event(dev);
}

/**
 *	netif_testing - test if device is under test
 *	@dev: network device
 *
 * Check if device is under test
 */
static inline bool netif_testing(const struct net_device *dev)
{
	return test_bit(__LINK_STATE_TESTING, &dev->state);
}


/**
 *	netif_oper_up - test if device is operational
 *	@dev: network device
 *
 * Check if carrier is operational
 */
static inline bool netif_oper_up(const struct net_device *dev)
{
	return (dev->operstate == IF_OPER_UP ||
		dev->operstate == IF_OPER_UNKNOWN /* backward compat */);
}

/**
 *	netif_device_present - is device available or removed
 *	@dev: network device
 *
 * Check if device has not been removed from system.
 */
static inline bool netif_device_present(const struct net_device *dev)
{
	return test_bit(__LINK_STATE_PRESENT, &dev->state);
}

void netif_device_detach(struct net_device *dev);

void netif_device_attach(struct net_device *dev);

/*
 * Network interface message level settings
 */

enum {
	NETIF_MSG_DRV_BIT,
	NETIF_MSG_PROBE_BIT,
	NETIF_MSG_LINK_BIT,
	NETIF_MSG_TIMER_BIT,
	NETIF_MSG_IFDOWN_BIT,
	NETIF_MSG_IFUP_BIT,
	NETIF_MSG_RX_ERR_BIT,
	NETIF_MSG_TX_ERR_BIT,
	NETIF_MSG_TX_QUEUED_BIT,
	NETIF_MSG_INTR_BIT,
	NETIF_MSG_TX_DONE_BIT,
	NETIF_MSG_RX_STATUS_BIT,
	NETIF_MSG_PKTDATA_BIT,
	NETIF_MSG_HW_BIT,
	NETIF_MSG_WOL_BIT,

	/* When you add a new bit above, update netif_msg_class_names array
	 * in net/ethtool/common.c
	 */
	NETIF_MSG_CLASS_COUNT,
};
/* Both ethtool_ops interface and internal driver implementation use u32 */
static_assert(NETIF_MSG_CLASS_COUNT <= 32);

#define __NETIF_MSG_BIT(bit)	((u32)1 << (bit))
#define __NETIF_MSG(name)	__NETIF_MSG_BIT(NETIF_MSG_ ## name ## _BIT)

#define NETIF_MSG_DRV		__NETIF_MSG(DRV)
#define NETIF_MSG_PROBE		__NETIF_MSG(PROBE)
#define NETIF_MSG_LINK		__NETIF_MSG(LINK)
#define NETIF_MSG_TIMER		__NETIF_MSG(TIMER)
#define NETIF_MSG_IFDOWN	__NETIF_MSG(IFDOWN)
#define NETIF_MSG_IFUP		__NETIF_MSG(IFUP)
#define NETIF_MSG_RX_ERR	__NETIF_MSG(RX_ERR)
#define NETIF_MSG_TX_ERR	__NETIF_MSG(TX_ERR)
#define NETIF_MSG_TX_QUEUED	__NETIF_MSG(TX_QUEUED)
#define NETIF_MSG_INTR		__NETIF_MSG(INTR)
#define NETIF_MSG_TX_DONE	__NETIF_MSG(TX_DONE)
#define NETIF_MSG_RX_STATUS	__NETIF_MSG(RX_STATUS)
#define NETIF_MSG_PKTDATA	__NETIF_MSG(PKTDATA)
#define NETIF_MSG_HW		__NETIF_MSG(HW)
#define NETIF_MSG_WOL		__NETIF_MSG(WOL)

#define netif_msg_drv(p)	((p)->msg_enable & NETIF_MSG_DRV)
#define netif_msg_probe(p)	((p)->msg_enable & NETIF_MSG_PROBE)
#define netif_msg_link(p)	((p)->msg_enable & NETIF_MSG_LINK)
#define netif_msg_timer(p)	((p)->msg_enable & NETIF_MSG_TIMER)
#define netif_msg_ifdown(p)	((p)->msg_enable & NETIF_MSG_IFDOWN)
#define netif_msg_ifup(p)	((p)->msg_enable & NETIF_MSG_IFUP)
#define netif_msg_rx_err(p)	((p)->msg_enable & NETIF_MSG_RX_ERR)
#define netif_msg_tx_err(p)	((p)->msg_enable & NETIF_MSG_TX_ERR)
#define netif_msg_tx_queued(p)	((p)->msg_enable & NETIF_MSG_TX_QUEUED)
#define netif_msg_intr(p)	((p)->msg_enable & NETIF_MSG_INTR)
#define netif_msg_tx_done(p)	((p)->msg_enable & NETIF_MSG_TX_DONE)
#define netif_msg_rx_status(p)	((p)->msg_enable & NETIF_MSG_RX_STATUS)
#define netif_msg_pktdata(p)	((p)->msg_enable & NETIF_MSG_PKTDATA)
#define netif_msg_hw(p)		((p)->msg_enable & NETIF_MSG_HW)
#define netif_msg_wol(p)	((p)->msg_enable & NETIF_MSG_WOL)

static inline u32 netif_msg_init(int debug_value, int default_msg_enable_bits)
{
	/* use default */
	if (debug_value < 0 || debug_value >= (sizeof(u32) * 8))
		return default_msg_enable_bits;
	if (debug_value == 0)	/* no output */
		return 0;
	/* set low N bits */
	return (1U << debug_value) - 1;
}

/*
 * dev_addrs walker. Should be used only for read access. Call with
 * rcu_read_lock held.
 */
#define for_each_dev_addr(dev, ha) \
		list_for_each_entry_rcu(ha, &dev->dev_addrs.list, list)

/* These functions live elsewhere (drivers/net/net_init.c, but related) */

void ether_setup(struct net_device *dev);

/* Support for loadable net-drivers */
struct net_device *alloc_netdev_mqs(int sizeof_priv, const char *name,
				    unsigned char name_assign_type,
				    void (*setup)(struct net_device *),
				    unsigned int txqs, unsigned int rxqs);
#define alloc_netdev(sizeof_priv, name, name_assign_type, setup) \
	alloc_netdev_mqs(sizeof_priv, name, name_assign_type, setup, 1, 1)

#define alloc_netdev_mq(sizeof_priv, name, name_assign_type, setup, count) \
	alloc_netdev_mqs(sizeof_priv, name, name_assign_type, setup, count, \
			 count)

int register_netdev(struct net_device *dev);
void unregister_netdev(struct net_device *dev);

int devm_register_netdev(struct device *dev, struct net_device *ndev);

/* General hardware address lists handling functions */
int __hw_addr_sync(struct netdev_hw_addr_list *to_list,
		   struct netdev_hw_addr_list *from_list, int addr_len);
void __hw_addr_unsync(struct netdev_hw_addr_list *to_list,
		      struct netdev_hw_addr_list *from_list, int addr_len);
int __hw_addr_sync_dev(struct netdev_hw_addr_list *list,
		       struct net_device *dev,
		       int (*sync)(struct net_device *, const unsigned char *),
		       int (*unsync)(struct net_device *,
				     const unsigned char *));
int __hw_addr_ref_sync_dev(struct netdev_hw_addr_list *list,
			   struct net_device *dev,
			   int (*sync)(struct net_device *,
				       const unsigned char *, int),
			   int (*unsync)(struct net_device *,
					 const unsigned char *, int));
void __hw_addr_ref_unsync_dev(struct netdev_hw_addr_list *list,
			      struct net_device *dev,
			      int (*unsync)(struct net_device *,
					    const unsigned char *, int));
void __hw_addr_unsync_dev(struct netdev_hw_addr_list *list,
			  struct net_device *dev,
			  int (*unsync)(struct net_device *,
					const unsigned char *));
void __hw_addr_init(struct netdev_hw_addr_list *list);

/* Functions used for device addresses handling */
static inline void
__dev_addr_set(struct net_device *dev, const void *addr, size_t len)
{
	memcpy(dev->dev_addr, addr, len);
}

static inline void dev_addr_set(struct net_device *dev, const u8 *addr)
{
	__dev_addr_set(dev, addr, dev->addr_len);
}

static inline void
dev_addr_mod(struct net_device *dev, unsigned int offset,
	     const void *addr, size_t len)
{
	memcpy(&dev->dev_addr[offset], addr, len);
}

int dev_addr_add(struct net_device *dev, const unsigned char *addr,
		 unsigned char addr_type);
int dev_addr_del(struct net_device *dev, const unsigned char *addr,
		 unsigned char addr_type);
void dev_addr_flush(struct net_device *dev);
int dev_addr_init(struct net_device *dev);

/* Functions used for unicast addresses handling */
int dev_uc_add(struct net_device *dev, const unsigned char *addr);
int dev_uc_add_excl(struct net_device *dev, const unsigned char *addr);
int dev_uc_del(struct net_device *dev, const unsigned char *addr);
int dev_uc_sync(struct net_device *to, struct net_device *from);
int dev_uc_sync_multiple(struct net_device *to, struct net_device *from);
void dev_uc_unsync(struct net_device *to, struct net_device *from);
void dev_uc_flush(struct net_device *dev);
void dev_uc_init(struct net_device *dev);

/**
 *  __dev_uc_sync - Synchonize device's unicast list
 *  @dev:  device to sync
 *  @sync: function to call if address should be added
 *  @unsync: function to call if address should be removed
 *
 *  Add newly added addresses to the interface, and release
 *  addresses that have been deleted.
 */
static inline int __dev_uc_sync(struct net_device *dev,
				int (*sync)(struct net_device *,
					    const unsigned char *),
				int (*unsync)(struct net_device *,
					      const unsigned char *))
{
	return __hw_addr_sync_dev(&dev->uc, dev, sync, unsync);
}

/**
 *  __dev_uc_unsync - Remove synchronized addresses from device
 *  @dev:  device to sync
 *  @unsync: function to call if address should be removed
 *
 *  Remove all addresses that were added to the device by dev_uc_sync().
 */
static inline void __dev_uc_unsync(struct net_device *dev,
				   int (*unsync)(struct net_device *,
						 const unsigned char *))
{
	__hw_addr_unsync_dev(&dev->uc, dev, unsync);
}

/* Functions used for multicast addresses handling */
int dev_mc_add(struct net_device *dev, const unsigned char *addr);
int dev_mc_add_global(struct net_device *dev, const unsigned char *addr);
int dev_mc_add_excl(struct net_device *dev, const unsigned char *addr);
int dev_mc_del(struct net_device *dev, const unsigned char *addr);
int dev_mc_del_global(struct net_device *dev, const unsigned char *addr);
int dev_mc_sync(struct net_device *to, struct net_device *from);
int dev_mc_sync_multiple(struct net_device *to, struct net_device *from);
void dev_mc_unsync(struct net_device *to, struct net_device *from);
void dev_mc_flush(struct net_device *dev);
void dev_mc_init(struct net_device *dev);

/**
 *  __dev_mc_sync - Synchonize device's multicast list
 *  @dev:  device to sync
 *  @sync: function to call if address should be added
 *  @unsync: function to call if address should be removed
 *
 *  Add newly added addresses to the interface, and release
 *  addresses that have been deleted.
 */
static inline int __dev_mc_sync(struct net_device *dev,
				int (*sync)(struct net_device *,
					    const unsigned char *),
				int (*unsync)(struct net_device *,
					      const unsigned char *))
{
	return __hw_addr_sync_dev(&dev->mc, dev, sync, unsync);
}

/**
 *  __dev_mc_unsync - Remove synchronized addresses from device
 *  @dev:  device to sync
 *  @unsync: function to call if address should be removed
 *
 *  Remove all addresses that were added to the device by dev_mc_sync().
 */
static inline void __dev_mc_unsync(struct net_device *dev,
				   int (*unsync)(struct net_device *,
						 const unsigned char *))
{
	__hw_addr_unsync_dev(&dev->mc, dev, unsync);
}

/* Functions used for secondary unicast and multicast support */
void dev_set_rx_mode(struct net_device *dev);
void __dev_set_rx_mode(struct net_device *dev);
int dev_set_promiscuity(struct net_device *dev, int inc);
int dev_set_allmulti(struct net_device *dev, int inc);
void netdev_state_change(struct net_device *dev);
void __netdev_notify_peers(struct net_device *dev);
void netdev_notify_peers(struct net_device *dev);
void netdev_features_change(struct net_device *dev);
/* Load a device via the kmod */
void dev_load(struct net *net, const char *name);
struct rtnl_link_stats64 *dev_get_stats(struct net_device *dev,
					struct rtnl_link_stats64 *storage);
void netdev_stats_to_stats64(struct rtnl_link_stats64 *stats64,
			     const struct net_device_stats *netdev_stats);
void dev_fetch_sw_netstats(struct rtnl_link_stats64 *s,
			   const struct pcpu_sw_netstats __percpu *netstats);
void dev_get_tstats64(struct net_device *dev, struct rtnl_link_stats64 *s);

extern int		netdev_max_backlog;
extern int		netdev_tstamp_prequeue;
extern int		netdev_unregister_timeout_secs;
extern int		weight_p;
extern int		dev_weight_rx_bias;
extern int		dev_weight_tx_bias;
extern int		dev_rx_weight;
extern int		dev_tx_weight;
extern int		gro_normal_batch;

enum {
	NESTED_SYNC_IMM_BIT,
	NESTED_SYNC_TODO_BIT,
};

#define __NESTED_SYNC_BIT(bit)	((u32)1 << (bit))
#define __NESTED_SYNC(name)	__NESTED_SYNC_BIT(NESTED_SYNC_ ## name ## _BIT)

#define NESTED_SYNC_IMM		__NESTED_SYNC(IMM)
#define NESTED_SYNC_TODO	__NESTED_SYNC(TODO)

struct netdev_nested_priv {
	unsigned char flags;
	void *data;
};

bool netdev_has_upper_dev(struct net_device *dev, struct net_device *upper_dev);
struct net_device *netdev_upper_get_next_dev_rcu(struct net_device *dev,
						     struct list_head **iter);

#ifdef CONFIG_LOCKDEP
static LIST_HEAD(net_unlink_list);

static inline void net_unlink_todo(struct net_device *dev)
{
	if (list_empty(&dev->unlink_list))
		list_add_tail(&dev->unlink_list, &net_unlink_list);
}
#endif

/* iterate through upper list, must be called under RCU read lock */
#define netdev_for_each_upper_dev_rcu(dev, updev, iter) \
	for (iter = &(dev)->adj_list.upper, \
	     updev = netdev_upper_get_next_dev_rcu(dev, &(iter)); \
	     updev; \
	     updev = netdev_upper_get_next_dev_rcu(dev, &(iter)))

int netdev_walk_all_upper_dev_rcu(struct net_device *dev,
				  int (*fn)(struct net_device *upper_dev,
					    struct netdev_nested_priv *priv),
				  struct netdev_nested_priv *priv);

bool netdev_has_upper_dev_all_rcu(struct net_device *dev,
				  struct net_device *upper_dev);

bool netdev_has_any_upper_dev(struct net_device *dev);

void *netdev_lower_get_next_private(struct net_device *dev,
				    struct list_head **iter);
void *netdev_lower_get_next_private_rcu(struct net_device *dev,
					struct list_head **iter);

#define netdev_for_each_lower_private(dev, priv, iter) \
	for (iter = (dev)->adj_list.lower.next, \
	     priv = netdev_lower_get_next_private(dev, &(iter)); \
	     priv; \
	     priv = netdev_lower_get_next_private(dev, &(iter)))

#define netdev_for_each_lower_private_rcu(dev, priv, iter) \
	for (iter = &(dev)->adj_list.lower, \
	     priv = netdev_lower_get_next_private_rcu(dev, &(iter)); \
	     priv; \
	     priv = netdev_lower_get_next_private_rcu(dev, &(iter)))

void *netdev_lower_get_next(struct net_device *dev,
				struct list_head **iter);

#define netdev_for_each_lower_dev(dev, ldev, iter) \
	for (iter = (dev)->adj_list.lower.next, \
	     ldev = netdev_lower_get_next(dev, &(iter)); \
	     ldev; \
	     ldev = netdev_lower_get_next(dev, &(iter)))

struct net_device *netdev_next_lower_dev_rcu(struct net_device *dev,
					     struct list_head **iter);
int netdev_walk_all_lower_dev(struct net_device *dev,
			      int (*fn)(struct net_device *lower_dev,
					struct netdev_nested_priv *priv),
			      struct netdev_nested_priv *priv);
int netdev_walk_all_lower_dev_rcu(struct net_device *dev,
				  int (*fn)(struct net_device *lower_dev,
					    struct netdev_nested_priv *priv),
				  struct netdev_nested_priv *priv);

void *netdev_adjacent_get_private(struct list_head *adj_list);
void *netdev_lower_get_first_private_rcu(struct net_device *dev);
struct net_device *netdev_master_upper_dev_get(struct net_device *dev);
struct net_device *netdev_master_upper_dev_get_rcu(struct net_device *dev);
int netdev_upper_dev_link(struct net_device *dev, struct net_device *upper_dev,
			  struct netlink_ext_ack *extack);
int netdev_master_upper_dev_link(struct net_device *dev,
				 struct net_device *upper_dev,
				 void *upper_priv, void *upper_info,
				 struct netlink_ext_ack *extack);
void netdev_upper_dev_unlink(struct net_device *dev,
			     struct net_device *upper_dev);
int netdev_adjacent_change_prepare(struct net_device *old_dev,
				   struct net_device *new_dev,
				   struct net_device *dev,
				   struct netlink_ext_ack *extack);
void netdev_adjacent_change_commit(struct net_device *old_dev,
				   struct net_device *new_dev,
				   struct net_device *dev);
void netdev_adjacent_change_abort(struct net_device *old_dev,
				  struct net_device *new_dev,
				  struct net_device *dev);
void netdev_adjacent_rename_links(struct net_device *dev, char *oldname);
void *netdev_lower_dev_get_private(struct net_device *dev,
				   struct net_device *lower_dev);
void netdev_lower_state_changed(struct net_device *lower_dev,
				void *lower_state_info);

/* RSS keys are 40 or 52 bytes long */
#define NETDEV_RSS_KEY_LEN 52
extern u8 netdev_rss_key[NETDEV_RSS_KEY_LEN] __read_mostly;
void netdev_rss_key_fill(void *buffer, size_t len);

int skb_checksum_help(struct sk_buff *skb);
int skb_crc32c_csum_help(struct sk_buff *skb);
int skb_csum_hwoffload_help(struct sk_buff *skb,
			    const netdev_features_t features);

struct sk_buff *__skb_gso_segment(struct sk_buff *skb,
				  netdev_features_t features, bool tx_path);
struct sk_buff *skb_mac_gso_segment(struct sk_buff *skb,
				    netdev_features_t features);

struct netdev_bonding_info {
	ifslave	slave;
	ifbond	master;
};

struct netdev_notifier_bonding_info {
	struct netdev_notifier_info info; /* must be first */
	struct netdev_bonding_info  bonding_info;
};

void netdev_bonding_info_change(struct net_device *dev,
				struct netdev_bonding_info *bonding_info);

#if IS_ENABLED(CONFIG_ETHTOOL_NETLINK)
void ethtool_notify(struct net_device *dev, unsigned int cmd, const void *data);
#else
static inline void ethtool_notify(struct net_device *dev, unsigned int cmd,
				  const void *data)
{
}
#endif

static inline
struct sk_buff *skb_gso_segment(struct sk_buff *skb, netdev_features_t features)
{
	return __skb_gso_segment(skb, features, true);
}
__be16 skb_network_protocol(struct sk_buff *skb, int *depth);

static inline bool can_checksum_protocol(netdev_features_t features,
					 __be16 protocol)
{
	if (protocol == htons(ETH_P_FCOE))
		return !!(features & NETIF_F_FCOE_CRC);

	/* Assume this is an IP checksum (not SCTP CRC) */

	if (features & NETIF_F_HW_CSUM) {
		/* Can checksum everything */
		return true;
	}

	switch (protocol) {
	case htons(ETH_P_IP):
		return !!(features & NETIF_F_IP_CSUM);
	case htons(ETH_P_IPV6):
		return !!(features & NETIF_F_IPV6_CSUM);
	default:
		return false;
	}
}

#ifdef CONFIG_BUG
void netdev_rx_csum_fault(struct net_device *dev, struct sk_buff *skb);
#else
static inline void netdev_rx_csum_fault(struct net_device *dev,
					struct sk_buff *skb)
{
}
#endif
/* rx skb timestamps */
void net_enable_timestamp(void);
void net_disable_timestamp(void);

#ifdef CONFIG_PROC_FS
int __init dev_proc_init(void);
#else
#define dev_proc_init() 0
#endif

static inline bool netdev_xmit_more(void)
{
	return __this_cpu_read(softnet_data.xmit.more);
}

struct class_attribute;

int netdev_class_create_file_ns(const struct class_attribute *class_attr,
				const void *ns);
void netdev_class_remove_file_ns(const struct class_attribute *class_attr,
				 const void *ns);

extern const struct kobj_ns_type_operations net_ns_type_operations;

const char *netdev_drivername(const struct net_device *dev);

void linkwatch_run_queue(void);

static inline netdev_features_t netdev_intersect_features(netdev_features_t f1,
							  netdev_features_t f2)
{
	if ((f1 ^ f2) & NETIF_F_HW_CSUM) {
		if (f1 & NETIF_F_HW_CSUM)
			f1 |= (NETIF_F_IP_CSUM|NETIF_F_IPV6_CSUM);
		else
			f2 |= (NETIF_F_IP_CSUM|NETIF_F_IPV6_CSUM);
	}

	return f1 & f2;
}

static inline netdev_features_t netdev_get_wanted_features(
	struct net_device *dev)
{
	return (dev->features & ~dev->hw_features) | dev->wanted_features;
}
netdev_features_t netdev_increment_features(netdev_features_t all,
	netdev_features_t one, netdev_features_t mask);

/* Allow TSO being used on stacked device :
 * Performing the GSO segmentation before last device
 * is a performance improvement.
 */
static inline netdev_features_t netdev_add_tso_features(netdev_features_t features,
							netdev_features_t mask)
{
	return netdev_increment_features(features, NETIF_F_ALL_TSO, mask);
}

int __netdev_update_features(struct net_device *dev);
void netdev_update_features(struct net_device *dev);
void netdev_change_features(struct net_device *dev);

void netif_stacked_transfer_operstate(const struct net_device *rootdev,
					struct net_device *dev);

netdev_features_t passthru_features_check(struct sk_buff *skb,
					  struct net_device *dev,
					  netdev_features_t features);
netdev_features_t netif_skb_features(struct sk_buff *skb);

static inline bool net_gso_ok(netdev_features_t features, int gso_type)
{
	netdev_features_t feature = (netdev_features_t)gso_type << NETIF_F_GSO_SHIFT;

	/* check flags correspondence */
	BUILD_BUG_ON(SKB_GSO_TCPV4   != (NETIF_F_TSO >> NETIF_F_GSO_SHIFT));
	BUILD_BUG_ON(SKB_GSO_DODGY   != (NETIF_F_GSO_ROBUST >> NETIF_F_GSO_SHIFT));
	BUILD_BUG_ON(SKB_GSO_TCP_ECN != (NETIF_F_TSO_ECN >> NETIF_F_GSO_SHIFT));
	BUILD_BUG_ON(SKB_GSO_TCP_FIXEDID != (NETIF_F_TSO_MANGLEID >> NETIF_F_GSO_SHIFT));
	BUILD_BUG_ON(SKB_GSO_TCPV6   != (NETIF_F_TSO6 >> NETIF_F_GSO_SHIFT));
	BUILD_BUG_ON(SKB_GSO_FCOE    != (NETIF_F_FSO >> NETIF_F_GSO_SHIFT));
	BUILD_BUG_ON(SKB_GSO_GRE     != (NETIF_F_GSO_GRE >> NETIF_F_GSO_SHIFT));
	BUILD_BUG_ON(SKB_GSO_GRE_CSUM != (NETIF_F_GSO_GRE_CSUM >> NETIF_F_GSO_SHIFT));
	BUILD_BUG_ON(SKB_GSO_IPXIP4  != (NETIF_F_GSO_IPXIP4 >> NETIF_F_GSO_SHIFT));
	BUILD_BUG_ON(SKB_GSO_IPXIP6  != (NETIF_F_GSO_IPXIP6 >> NETIF_F_GSO_SHIFT));
	BUILD_BUG_ON(SKB_GSO_UDP_TUNNEL != (NETIF_F_GSO_UDP_TUNNEL >> NETIF_F_GSO_SHIFT));
	BUILD_BUG_ON(SKB_GSO_UDP_TUNNEL_CSUM != (NETIF_F_GSO_UDP_TUNNEL_CSUM >> NETIF_F_GSO_SHIFT));
	BUILD_BUG_ON(SKB_GSO_PARTIAL != (NETIF_F_GSO_PARTIAL >> NETIF_F_GSO_SHIFT));
	BUILD_BUG_ON(SKB_GSO_TUNNEL_REMCSUM != (NETIF_F_GSO_TUNNEL_REMCSUM >> NETIF_F_GSO_SHIFT));
	BUILD_BUG_ON(SKB_GSO_SCTP    != (NETIF_F_GSO_SCTP >> NETIF_F_GSO_SHIFT));
	BUILD_BUG_ON(SKB_GSO_ESP != (NETIF_F_GSO_ESP >> NETIF_F_GSO_SHIFT));
	BUILD_BUG_ON(SKB_GSO_UDP != (NETIF_F_GSO_UDP >> NETIF_F_GSO_SHIFT));
	BUILD_BUG_ON(SKB_GSO_UDP_L4 != (NETIF_F_GSO_UDP_L4 >> NETIF_F_GSO_SHIFT));
	BUILD_BUG_ON(SKB_GSO_FRAGLIST != (NETIF_F_GSO_FRAGLIST >> NETIF_F_GSO_SHIFT));

	return (features & feature) == feature;
}

static inline bool skb_gso_ok(struct sk_buff *skb, netdev_features_t features)
{
	return net_gso_ok(features, skb_shinfo(skb)->gso_type) &&
	       (!skb_has_frag_list(skb) || (features & NETIF_F_FRAGLIST));
}

static inline bool netif_needs_gso(struct sk_buff *skb,
				   netdev_features_t features)
{
	return skb_is_gso(skb) && (!skb_gso_ok(skb, features) ||
		unlikely((skb->ip_summed != CHECKSUM_PARTIAL) &&
			 (skb->ip_summed != CHECKSUM_UNNECESSARY)));
}

static inline void netif_set_gso_max_size(struct net_device *dev,
					  unsigned int size)
{
	dev->gso_max_size = size;
}

static inline void skb_gso_error_unwind(struct sk_buff *skb, __be16 protocol,
					int pulled_hlen, u16 mac_offset,
					int mac_len)
{
	skb->protocol = protocol;
	skb->encapsulation = 1;
	skb_push(skb, pulled_hlen);
	skb_reset_transport_header(skb);
	skb->mac_header = mac_offset;
	skb->network_header = skb->mac_header + mac_len;
	skb->mac_len = mac_len;
}

/* This device needs to keep skb dst for qdisc enqueue or ndo_start_xmit() */
static inline void netif_keep_dst(struct net_device *dev)
{
	dev->priv_flags &= ~(IFF_XMIT_DST_RELEASE | IFF_XMIT_DST_RELEASE_PERM);
}

/* return true if dev can't cope with mtu frames that need vlan tag insertion */
static inline bool netif_reduces_vlan_mtu(struct net_device *dev)
{
	/* TODO: reserve and use an additional IFF bit, if we get more users */
	return netif_is_macsec(dev);
}

extern struct pernet_operations __net_initdata loopback_net_ops;

/* Logging, debugging and troubleshooting/diagnostic helpers. */

/* netdev_printk helpers, similar to dev_printk */

static inline const char *netdev_name(const struct net_device *dev)
{
	if (!dev->name[0] || strchr(dev->name, '%'))
		return "(unnamed net_device)";
	return dev->name;
}

static inline bool netdev_unregistering(const struct net_device *dev)
{
	return dev->reg_state == NETREG_UNREGISTERING;
}

static inline const char *netdev_reg_state(const struct net_device *dev)
{
	switch (dev->reg_state) {
	case NETREG_UNINITIALIZED: return " (uninitialized)";
	case NETREG_REGISTERED: return "";
	case NETREG_UNREGISTERING: return " (unregistering)";
	case NETREG_UNREGISTERED: return " (unregistered)";
	case NETREG_RELEASED: return " (released)";
	case NETREG_DUMMY: return " (dummy)";
	}

	WARN_ONCE(1, "%s: unknown reg_state %d\n", dev->name, dev->reg_state);
	return " (unknown)";
}

__printf(3, 4) __cold
void netdev_printk(const char *level, const struct net_device *dev,
		   const char *format, ...);
__printf(2, 3) __cold
void netdev_emerg(const struct net_device *dev, const char *format, ...);
__printf(2, 3) __cold
void netdev_alert(const struct net_device *dev, const char *format, ...);
__printf(2, 3) __cold
void netdev_crit(const struct net_device *dev, const char *format, ...);
__printf(2, 3) __cold
void netdev_err(const struct net_device *dev, const char *format, ...);
__printf(2, 3) __cold
void netdev_warn(const struct net_device *dev, const char *format, ...);
__printf(2, 3) __cold
void netdev_notice(const struct net_device *dev, const char *format, ...);
__printf(2, 3) __cold
void netdev_info(const struct net_device *dev, const char *format, ...);

#define netdev_level_once(level, dev, fmt, ...)			\
do {								\
	static bool __print_once __read_mostly;			\
								\
	if (!__print_once) {					\
		__print_once = true;				\
		netdev_printk(level, dev, fmt, ##__VA_ARGS__);	\
	}							\
} while (0)

#define netdev_emerg_once(dev, fmt, ...) \
	netdev_level_once(KERN_EMERG, dev, fmt, ##__VA_ARGS__)
#define netdev_alert_once(dev, fmt, ...) \
	netdev_level_once(KERN_ALERT, dev, fmt, ##__VA_ARGS__)
#define netdev_crit_once(dev, fmt, ...) \
	netdev_level_once(KERN_CRIT, dev, fmt, ##__VA_ARGS__)
#define netdev_err_once(dev, fmt, ...) \
	netdev_level_once(KERN_ERR, dev, fmt, ##__VA_ARGS__)
#define netdev_warn_once(dev, fmt, ...) \
	netdev_level_once(KERN_WARNING, dev, fmt, ##__VA_ARGS__)
#define netdev_notice_once(dev, fmt, ...) \
	netdev_level_once(KERN_NOTICE, dev, fmt, ##__VA_ARGS__)
#define netdev_info_once(dev, fmt, ...) \
	netdev_level_once(KERN_INFO, dev, fmt, ##__VA_ARGS__)

#define MODULE_ALIAS_NETDEV(device) \
	MODULE_ALIAS("netdev-" device)

#if defined(CONFIG_DYNAMIC_DEBUG) || \
	(defined(CONFIG_DYNAMIC_DEBUG_CORE) && defined(DYNAMIC_DEBUG_MODULE))
#define netdev_dbg(__dev, format, args...)			\
do {								\
	dynamic_netdev_dbg(__dev, format, ##args);		\
} while (0)
#elif defined(DEBUG)
#define netdev_dbg(__dev, format, args...)			\
	netdev_printk(KERN_DEBUG, __dev, format, ##args)
#else
#define netdev_dbg(__dev, format, args...)			\
({								\
	if (0)							\
		netdev_printk(KERN_DEBUG, __dev, format, ##args); \
})
#endif

#if defined(VERBOSE_DEBUG)
#define netdev_vdbg	netdev_dbg
#else

#define netdev_vdbg(dev, format, args...)			\
({								\
	if (0)							\
		netdev_printk(KERN_DEBUG, dev, format, ##args);	\
	0;							\
})
#endif

/*
 * netdev_WARN() acts like dev_printk(), but with the key difference
 * of using a WARN/WARN_ON to get the message out, including the
 * file/line information and a backtrace.
 */
#define netdev_WARN(dev, format, args...)			\
	WARN(1, "netdevice: %s%s: " format, netdev_name(dev),	\
	     netdev_reg_state(dev), ##args)

#define netdev_WARN_ONCE(dev, format, args...)				\
	WARN_ONCE(1, "netdevice: %s%s: " format, netdev_name(dev),	\
		  netdev_reg_state(dev), ##args)

/* netif printk helpers, similar to netdev_printk */

#define netif_printk(priv, type, level, dev, fmt, args...)	\
do {					  			\
	if (netif_msg_##type(priv))				\
		netdev_printk(level, (dev), fmt, ##args);	\
} while (0)

#define netif_level(level, priv, type, dev, fmt, args...)	\
do {								\
	if (netif_msg_##type(priv))				\
		netdev_##level(dev, fmt, ##args);		\
} while (0)

#define netif_emerg(priv, type, dev, fmt, args...)		\
	netif_level(emerg, priv, type, dev, fmt, ##args)
#define netif_alert(priv, type, dev, fmt, args...)		\
	netif_level(alert, priv, type, dev, fmt, ##args)
#define netif_crit(priv, type, dev, fmt, args...)		\
	netif_level(crit, priv, type, dev, fmt, ##args)
#define netif_err(priv, type, dev, fmt, args...)		\
	netif_level(err, priv, type, dev, fmt, ##args)
#define netif_warn(priv, type, dev, fmt, args...)		\
	netif_level(warn, priv, type, dev, fmt, ##args)
#define netif_notice(priv, type, dev, fmt, args...)		\
	netif_level(notice, priv, type, dev, fmt, ##args)
#define netif_info(priv, type, dev, fmt, args...)		\
	netif_level(info, priv, type, dev, fmt, ##args)

#if defined(CONFIG_DYNAMIC_DEBUG) || \
	(defined(CONFIG_DYNAMIC_DEBUG_CORE) && defined(DYNAMIC_DEBUG_MODULE))
#define netif_dbg(priv, type, netdev, format, args...)		\
do {								\
	if (netif_msg_##type(priv))				\
		dynamic_netdev_dbg(netdev, format, ##args);	\
} while (0)
#elif defined(DEBUG)
#define netif_dbg(priv, type, dev, format, args...)		\
	netif_printk(priv, type, KERN_DEBUG, dev, format, ##args)
#else
#define netif_dbg(priv, type, dev, format, args...)			\
({									\
	if (0)								\
		netif_printk(priv, type, KERN_DEBUG, dev, format, ##args); \
	0;								\
})
#endif

/* if @cond then downgrade to debug, else print at @level */
#define netif_cond_dbg(priv, type, netdev, cond, level, fmt, args...)     \
	do {                                                              \
		if (cond)                                                 \
			netif_dbg(priv, type, netdev, fmt, ##args);       \
		else                                                      \
			netif_ ## level(priv, type, netdev, fmt, ##args); \
	} while (0)

#if defined(VERBOSE_DEBUG)
#define netif_vdbg	netif_dbg
#else
#define netif_vdbg(priv, type, dev, format, args...)		\
({								\
	if (0)							\
		netif_printk(priv, type, KERN_DEBUG, dev, format, ##args); \
	0;							\
})
#endif

/*
 *	The list of packet types we will receive (as opposed to discard)
 *	and the routines to invoke.
 *
 *	Why 16. Because with 16 the only overlap we get on a hash of the
 *	low nibble of the protocol value is RARP/SNAP/X.25.
 *
 *		0800	IP
 *		0001	802.3
 *		0002	AX.25
 *		0004	802.2
 *		8035	RARP
 *		0005	SNAP
 *		0805	X.25
 *		0806	ARP
 *		8137	IPX
 *		0009	Localtalk
 *		86DD	IPv6
 */
#define PTYPE_HASH_SIZE	(16)
#define PTYPE_HASH_MASK	(PTYPE_HASH_SIZE - 1)

extern struct list_head ptype_all __read_mostly;
extern struct list_head ptype_base[PTYPE_HASH_SIZE] __read_mostly;

extern struct net_device *blackhole_netdev;

#endif	/* _LINUX_NETDEVICE_API_H */
