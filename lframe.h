#ifndef _LFRAME_H
#define _LFRAME_H
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/kprobes.h>
#include <linux/kallsyms.h>
#include <linux/debugfs.h> 
#include <linux/fs.h>   
#include <linux/time.h>

typedef void (*lframe_init_t)(void *);
typedef void (*lframe_exit_t)(void *);

typedef struct lframe_entry {
    	lframe_init_t init;
    	lframe_exit_t exit;
	char modname[16];
	struct jprobe probe;
	char *data;
	int  tsize;
	int  usize;
	int  idx;
} lframe_entry_t;

struct lframe_config {
	int sock_io_proto;
	int dport;
	int serverip;
	int reconfig;
};

typedef struct {
	struct 	list_head list;
	int  	len;
	char	buffer[];
} tcpio_msg_t;

typedef struct {
	unsigned int msgtype;
	unsigned int msgid;
	unsigned int msglen;
	unsigned int reserved;
} lio_hdr_t;

typedef unsigned int lhkey_t;

typedef struct lh_entry {
	struct 	list_head list;
	lhkey_t	key;
	int	count;
} lh_entry_t;


typedef struct lh_funcs {
	int (*search) (void *, void *);	
	int (*free) (void *);
} lh_func_t;
typedef struct lh_table {
	int 		size;
	lh_func_t 	ops;
	lh_entry_t 	table[];
} lh_table_t;

/**********************************************************************
 * lframe_msg_type enum holds different types messages supported 
**********************************************************************/

enum lframe_msg_type {
	TCP_PROBE,
	MEM_PROBE,
	MAX_MSGTYPE
	};

/**********************************************************************
 * struct lf_timer is a reference struct, which holds information 
 * about active timers actived by lframe
**********************************************************************/

typedef void (*lftimerfun)(unsigned long arg);

typedef struct lf_timer {
	struct 	list_head list;
	struct timer_list timer;
	lftimerfun	handler;
	unsigned long	data;
	int		interval;
	int		active;
} lftimer_t;


typedef int (*searchfunp_t) (void *, void *);
typedef int (*freefunp_t) (void *);


extern void *alloc_tcpio_mem(int size);
extern void free_tcpio_mem(void *buf);
extern int tcpio_send(tcpio_msg_t *tmsg);
extern int  get_tcpio_status(void);

extern struct lframe_config lfconfig;
extern void cleanup_tcpio(void);
extern int init_tcpio(void);
extern int create_socket(void);


extern lh_table_t * lh_init(lh_func_t *ops, int size);
extern void lh_exit(lh_table_t *lht);
extern void * lh_search(lh_table_t *lht, lhkey_t key, void *data);
extern int lh_insert(lh_table_t *lht, void *entry, lhkey_t key);
extern int lh_delete(lh_table_t *lht, void *entry, lhkey_t key);


extern int init_lftimer(void);
extern lftimer_t * lftimer_create(lftimerfun handler, unsigned long data, int secs);
extern int lftimer_start(lftimer_t *node);
extern int lftimer_stop(lftimer_t *node);
extern int lftimer_delete(lftimer_t *delnode);
extern int lftimer_mod(lftimer_t *node);
extern int exit_lftimer(void);
extern void __hexdump(unsigned char *start, int size, char *funname, int line);

#define register_lframe(name, initfun, exitfun)		\
    static lframe_entry_t __lframe_ ## initfun ## exitfun	\
    __attribute__((__section__("LFRAME"))) __used = {			\
	.init = (lframe_init_t)initfun,					\
	.exit = (lframe_exit_t)exitfun,					\
	.modname = #name,					\
    }

#define hexdump(ptr, size) \
	do {\
		__hexdump(ptr, size, (char *)__func__, (int) __LINE__);\
	}while (0)

static inline int install_probe(struct jprobe *probe, kprobe_opcode_t *cb, char *symbol )
{
	int ret;
	probe->entry = (kprobe_opcode_t *) cb;
        probe->kp.addr = (kprobe_opcode_t *) kallsyms_lookup_name(symbol);
        if (!probe->kp.addr) {
                printk("unable to find %s to plant jprobe\n", symbol);
                return -1;
        }

        if ((ret = register_jprobe(probe)) < 0) {
                printk("register_jprobe failed, returned %d\n", ret);
                return -1;
        }
        printk("planted jprobe at %p, handler addr %p\n", probe->kp.addr, probe->entry);
	return ret;
}

static inline void uninstall_probe(struct jprobe *probe, char *symbol)
{
	unregister_jprobe(probe);
	printk("jprobe %s unregistered\n", symbol?:"");
}

static inline int io_open(void)
{
	return create_socket();	
}
static inline int io_send(void *arg)
{
	return tcpio_send(arg);
}
static inline int get_io_status(void)
{
	return get_tcpio_status();
}

extern struct dentry *basedir; 
int init_lframectl(void);
int exit_lframectl(void);
#endif
