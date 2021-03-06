#include <linux/slab.h>
#include <linux/kthread.h>

#include <linux/errno.h>
#include <linux/types.h>

#include <linux/netdevice.h>
#include <linux/ip.h>
#include <linux/in.h>
#include <linux/delay.h>
#include <linux/un.h>
#include <linux/unistd.h>
#include <linux/wait.h>
#include <linux/ctype.h>
#include <asm/unistd.h>
#include <net/sock.h>
#include <net/tcp.h>
#include <net/inet_connection_sock.h>
#include <net/request_sock.h>
#include <linux/skbuff.h>
#include <linux/workqueue.h>
#include "lframe.h"

#define SERVER_PORT 55555
#define SERVER_ADDR 0x7f000001

int tcpio_thread(void);
int tcpio_start(void);

struct tcpio_info {
	int	connected;
	struct	socket *client_socket;
	struct	task_struct *accept_worker;
};

struct tcpio_info *tcpio_info;
static struct 	workqueue_struct *tcpio_wq;

typedef struct {
	struct	work_struct work;
	struct	list_head list;
	spinlock_t tcpio_lock;
} tcpio_work_t;


tcpio_work_t tcpio_work;
int  get_tcpio_status(void)
{
	return tcpio_info->connected;
}
void *alloc_tcpio_mem(int size)
{
	tcpio_msg_t *tmsg;
	tmsg = kmalloc(sizeof(tcpio_msg_t) + size, GFP_ATOMIC);
	memset(tmsg, 0, sizeof(tcpio_msg_t) + size);
	if(tmsg) {
		tmsg->len=size;
	} else {
		printk("Unable to allocate %d bytes with flag GFP_ATOMIC\n", size);
	}
	return tmsg;
}

void free_tcpio_mem(void *buf)
{
	kfree(buf);
}


int create_socket(void)
{
	int error;
	struct socket *socket;
	struct sockaddr_in sin;
	char one = 1;

	if(tcpio_info->connected == 1 && lfconfig.reconfig != 1) {
		printk("already connected\n");
		return 0;
	}
	if (tcpio_info->client_socket != NULL) {
		printk("[%s]release the client_socket\n", __func__);
		sock_release(tcpio_info->client_socket);
		tcpio_info->client_socket = NULL;
	} 

	error = sock_create(PF_INET, SOCK_STREAM, IPPROTO_TCP, &tcpio_info->client_socket);
	if (error < 0) {
		printk(KERN_ERR "CREATE SOCKET ERROR");
		return -1;
	}

	socket = tcpio_info->client_socket;
	tcpio_info->client_socket->sk->sk_reuse = 1;
	socket->ops->setsockopt(socket, SOL_TCP, TCP_NODELAY, &one, sizeof(one));

	if(lfconfig.serverip != 0) {
		sin.sin_addr.s_addr = lfconfig.serverip;
		sin.sin_family = AF_INET;
		if(lfconfig.dport)
			sin.sin_port = htons(lfconfig.dport);
		else
			sin.sin_port = htons(SERVER_PORT);
	} else {
		sin.sin_addr.s_addr = htonl(SERVER_ADDR);
		sin.sin_family = AF_INET;
		sin.sin_port = htons(SERVER_PORT);
	}
	error = socket->ops->connect(socket, (struct sockaddr *)&sin, sizeof(sin), 0);
	if (error < 0) {
		tcpio_info->connected = 0;
		printk(KERN_ERR "connect failed");
		return -1;
	} else {
		printk("tcp io connected\n");
		tcpio_info->connected = 1;
	}
	return 0;

}
/*enqueue incoming buffer & schedule work */
int tcpio_send(tcpio_msg_t *tmsg)
{
	int ret = -1;
	if(tmsg) {
		INIT_LIST_HEAD(&tmsg->list);	
		spin_lock(&tcpio_work.tcpio_lock);
		list_add(&tmsg->list, &tcpio_work.list);
		spin_unlock(&tcpio_work.tcpio_lock);
		ret = queue_work( tcpio_wq, (struct work_struct *)&tcpio_work);
	}
	return ret;
}
/* process list of buffers */
void tcpio_wq_function(struct work_struct *work)
{
	struct msghdr msg;
	int ret = 0;
	struct socket *sock = tcpio_info->client_socket;
	tcpio_msg_t *node, *tempnode;

	if(tcpio_info->connected == 0) {
		create_socket();
		sock = tcpio_info->client_socket;
	}
	list_for_each_entry_safe(node, tempnode, &tcpio_work.list, list) {
		if(tcpio_info->connected == 1) {
        		struct kvec iv = {node->buffer, node->len};
			if (sock == NULL) {
				printk("sock is NULL\n");
				return ;
			}
			memset(&msg, 0, sizeof(msg));
			ret = kernel_sendmsg(sock, &msg, &iv, 1, node->len);
			if (ret < 0) {
				printk("kernel_sendmsg returned %d\n", ret);
				tcpio_info->connected = 0;
			}
		}

		spin_lock(&tcpio_work.tcpio_lock);
    		list_del(&node->list);
		spin_unlock(&tcpio_work.tcpio_lock);
		free_tcpio_mem(node);
	}
}
/* http://www.ibm.com/developerworks/linux/library/l-tasklets/index.html */
/* http://www.roman10.net/2011/07/28/linux-kernel-programminglinked-list/ */
int tcpio_start()
{
	tcpio_wq = create_workqueue("tcpio_queue");
	if (tcpio_wq) {
		/* Queue some work (item 1) */
		INIT_LIST_HEAD(&tcpio_work.list);
		tcpio_work.tcpio_lock = __SPIN_LOCK_UNLOCKED(tcpio_work.tcpio_lock);
		INIT_WORK( (struct work_struct *)&tcpio_work, tcpio_wq_function );
	} else {
		printk("Unable to create workqueue \"tcpio_queue\"\n");
	}
	return 0;
}

int tcpio_test(void)
{
	tcpio_msg_t *tmsg;
	tmsg = alloc_tcpio_mem(256);
	memset(tmsg->buffer, 'a', 256);
	tcpio_send(tmsg);
	return 0;
}

int init_tcpio()
{
	tcpio_info = kmalloc(sizeof(struct tcpio_info), GFP_KERNEL);
	if(tcpio_info == NULL) {
		printk("Unable to allocate tcpio_info\n");
		return -1;
	}
	memset(tcpio_info, 0, sizeof(struct tcpio_info));
	tcpio_start();
	return 0;
}

void cleanup_tcpio()
{
	flush_workqueue(tcpio_wq);
	destroy_workqueue(tcpio_wq);
	/* free allocated resources before exit */
	if (tcpio_info->client_socket != NULL) {
		printk("release the client_socket\n");
		sock_release(tcpio_info->client_socket);
		tcpio_info->client_socket = NULL;
	}

	kfree(tcpio_info);
	tcpio_info = NULL;
}
