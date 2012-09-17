/*! Thread management */
#define _K_THREAD_C_
#define _K_SCHED_
#include "thread.h"

#include "memory.h"
#include "device.h"
#include "sched.h"
#include <arch/processor.h>
#include <arch/interrupt.h>
#include <types/bits.h>
#include <lib/list.h>
#include <lib/string.h>
#include <kernel/errno.h>

static list_t all_threads; /* all threads */

static kthread_t *active_thread = NULL; /* active thread */

static void kthread_remove_descriptor ( kthread_t *kthread );
/* idle thread */
static void idle_thread ( void *param );


/*! initialize thread structures and create idle thread */
void kthreads_init ()
{
	int prio;

	list_init ( &all_threads );

	active_thread = NULL;
	ksched_init ();

	(void) kthread_create ( idle_thread, NULL, 0, SCHED_FIFO, 0, NULL, 0 );

	/* initialize memory pool for threads */
	pi.heap = kmalloc ( PROG_HEAP_SIZE );
	pi.heap_size = PROG_HEAP_SIZE;

	prio = pi.prio;
	if ( !prio )
		prio = THREAD_DEF_PRIO;

	(void) kthread_create ( pi.init, NULL, 0, SCHED_FIFO, prio, NULL, 0 );

	kthreads_schedule ();
}

/*!
 * Create new thread
 * \param start_routine Starting function for new thread
 * \param arg Parameter sent to starting function
 * \param sched_policy Thread scheduling policy
 * \param sched_priority Thread priority
 * \param stackaddr Address of thread stack (if not NULL)
 * \param stacksize Stack size
 * \return Pointer to descriptor of created kernel thread
 */
kthread_t *kthread_create ( void *start_routine, void *arg, uint flags,
       int sched_policy, int sched_priority, void *stackaddr, size_t stacksize )
{
	kthread_t *kthread;
	int stack_provided = FALSE;

	/* thread descriptor */
	kthread = kmalloc ( sizeof (kthread_t) );
	ASSERT ( kthread );

	/* initialize thread descriptor */
	kthread->id = k_new_id ();

	kthread->queue = NULL;
	kthreadq_init ( &kthread->join_queue );

	if ( !stackaddr || !stacksize )
	{
		if ( !stacksize )
			stacksize = DEFAULT_THREAD_STACK_SIZE;

		stackaddr = kmalloc ( stacksize );
	}
	ASSERT ( stackaddr && stacksize );

	if ( stack_provided )
	{
		kthread->stack = NULL;
		kthread->stack_size = 0;
	}
	else {
		kthread->stack = stackaddr;
		kthread->stack_size = stacksize;
	}

	arch_create_thread_context ( &kthread->context, start_routine, arg,
				     pi.exit, stackaddr, stacksize );

	kthread->flags = flags;
	kthread->retval = 0;
	kthread->errno = 0;
	kthread->exit_status = NULL;
	kthread->pparam = NULL;

	list_append ( &all_threads, kthread, &kthread->all );

	kthread->sched_policy = sched_policy;
	if ( sched_priority < 0 )
		sched_priority = 0;
	if ( sched_priority >= PRIO_LEVELS )
		sched_priority = PRIO_LEVELS - 1;
	kthread->sched_priority = sched_priority;

	kthread->ref_cnt = 1;
	kthread_move_to_ready ( kthread, LAST );

	return kthread;
}

/*! Suspend thread (kthreads_schedule must follow this call) */
inline int kthread_suspend ( kthread_t *kthread, void *wakeup_action,
			     void *param )
{
	if ( !kthread )
		kthread = active_thread;

	ASSERT ( kthread->state == THR_STATE_ACTIVE ||
		 kthread->state == THR_STATE_READY );

	if ( kthread->state == THR_STATE_READY )
		kthread_remove_from_ready ( kthread );

	kthread->state = THR_STATE_SUSPENDED;
	kthread->cancel_suspend_handler = wakeup_action;
	kthread->cancel_suspend_param = param;

	return 0;
}

/*!
 * Thread exit/cancel thread
 * \param kthread Thread descriptor
 */
int kthread_exit ( kthread_t *kthread, void *exit_status )
{
	kthread_t *released;
	kthread_q *q;
	void **p;

	ASSERT ( kthread );

	if ( kthread->state == THR_STATE_PASSIVE )
		return EXIT_SUCCESS; /* thread is already finished */

	if ( kthread->state == THR_STATE_READY )
	{
		/* remove target 'thread' from its queue */
		if ( !kthread_remove_from_ready ( kthread ) )
			ASSERT ( FALSE );
	}
	else if ( kthread->state == THR_STATE_WAIT )
	{
		/* remove target 'thread' from its queue */
		if ( !kthreadq_remove ( kthread->queue, kthread ) )
			ASSERT ( FALSE );
	}
	else if ( kthread->state == THR_STATE_SUSPENDED )
	{
		/* cancellation routine */
		if ( kthread->cancel_suspend_handler )
			kthread->cancel_suspend_handler ( kthread,
				kthread->cancel_suspend_param );
	}
	else if ( kthread->state == THR_STATE_ACTIVE )
	{
		if ( kthread != active_thread )
			return ESRCH; /* thread descriptor corrupted ! */
	}
	else {
		return ESRCH; /* thread descriptor corrupted ! */
	}

	kthread->state = THR_STATE_PASSIVE;
	kthread->exit_status = exit_status;

	/* any thread waiting on this? */
	q = &kthread->join_queue;

	while ( (released = kthreadq_remove ( q, NULL )) != NULL )
	{
		/* save exit status to all waiting threads */
		p = kthread_get_private_param ( released );
		if ( p )
			*p = exit_status;

		kthread_move_to_ready ( released, LAST );
	}

	/* defer removing thread resources until last moment (switch) */
	kthread->flags |= THR_FLAG_DELETE;

	if ( kthread == active_thread )
		kthreads_schedule ();

	return EXIT_SUCCESS;
}

/*! Internal function for removing (freeing) thread descriptor */
static void kthread_remove_descriptor ( kthread_t *kthread )
{
	ASSERT ( kthread );

	k_free_id ( kthread->id );
	kthread->id = 0;

#ifdef DEBUG
	ASSERT( kthread == list_find_and_remove (&all_threads, &kthread->all) );
#else
	(void) list_remove ( &all_threads, 0, &kthread->all );
#endif

	kfree ( kthread );
}

/*!
 * Put given thread or active thread (when kthread == NULL) into queue of
 * "waited" thread
 */
void kthread_wait_thread ( kthread_t *waiting, kthread_t *waited )
{
	ASSERT ( waited );

	waited->ref_cnt++;
	kthread_enqueue ( waiting, &waited->join_queue );
}

/*! Get exit status of finished thread (and free descriptor) */
void kthread_collect_status ( kthread_t *waited, void **retval )
{
	ASSERT ( waited );

	if ( retval )
		*retval = waited->exit_status;

	waited->ref_cnt--;
	if ( !waited->ref_cnt )
		kthread_remove_descriptor ( waited );
}

/*! Switch to other thread */
void kthread_switch_to_thread ( kthread_t *from, kthread_t *to )
{
	ASSERT ( to == active_thread );

	if ( from && ( from->flags & THR_FLAG_DELETE ) )
	{
		/* delete thread resources with switch */

		from->flags &= ~THR_FLAG_DELETE;
		arch_switch_to_thread_with_cleanup ( from,
						     kthread_get_context (to) );
	}
	else {
		arch_switch_to_thread (
		/* from */	( from ? kthread_get_context ( from ) : NULL ),
		/* to */	kthread_get_context ( to )
		);
	}
}

/*! release finished thread stack just before switching to another thread */
void kthread_cleanup ( kthread_t *kthread )
{
	/* release thread stack */
	if ( kthread->stack )
		kfree ( kthread->stack );

	kthread->ref_cnt--;
	if ( !kthread->ref_cnt )
		kthread_remove_descriptor ( kthread );
}

/*! operations on thread queues (blocked threads) --------------------------- */

/*!
 * Put given thread or active thread (when kthread == NULL) into queue 'q_id'
 * - if kthread != NULL, thread must not be in any list and 'kthreads_schedule'
 *   should follow this call before exiting from kernel!
 */
void kthread_enqueue ( kthread_t *kthread, kthread_q *q )
{
	ASSERT ( ( kthread || active_thread ) && q );

	if ( !kthread )
		kthread = active_thread;

	kthread->state = THR_STATE_WAIT;
	kthread->queue = q;

	kthreadq_append ( kthread->queue, kthread );
}

/*!
 * Release single thread from given queue (if queue not empty)
 * \param q Queue
 * \return 1 if thread was released, 0 if queue was empty
 */
int kthreadq_release ( kthread_q *q )
{
	kthread_t *kthread;

	ASSERT ( q );

	kthread = kthreadq_remove ( q, NULL );

	if ( kthread )
	{
		kthread_move_to_ready ( kthread, LAST );
		return 1;
	}
	else {
		return 0;
	}
}

/*!
 * Release all threads from given queue (if queue not empty)
 * \param q Queue
 * \return number of thread released, 0 if queue was empty
 */
int kthreadq_release_all ( kthread_q *q )
{
	ASSERT ( q );
	int cnt = 0;

	while ( kthreadq_release (q) )
		cnt++;

	return cnt;
}

/*! thread queue manipulation */
inline void kthreadq_init ( kthread_q *q )
{
	ASSERT ( q );
	list_init ( &q->q );
}
inline void kthreadq_append ( kthread_q *q, kthread_t *kthread )
{
	ASSERT ( kthread && q );
	list_append ( &q->q, kthread, &kthread->list );
}
inline void kthreadq_prepend ( kthread_q *q, kthread_t *kthread )
{
	ASSERT ( kthread && q );
	list_prepend ( &q->q, kthread, &kthread->list );
}
inline kthread_t *kthreadq_remove ( kthread_q *q, kthread_t *kthread )
{
	ASSERT ( q );
	if ( kthread )
		return list_find_and_remove ( &q->q, &kthread->list );
	else
		return list_remove ( &q->q, FIRST, NULL );
}
inline kthread_t *kthreadq_get ( kthread_q *q )
{
	ASSERT ( q );
	return list_get ( &q->q, FIRST );
}
inline kthread_t *kthreadq_get_next ( kthread_t *kthread )
{
	ASSERT ( kthread );
	return list_get_next ( &kthread->list );   /* kthread->queue->q.first->object */
}

/*! get thread scheduling policy */
inline int kthread_get_sched_policy ( kthread_t *kthread )
{
	if ( kthread )
		return kthread->sched_policy;
	else
		return active_thread->sched_policy;
}

inline int kthread_get_prio ( kthread_t *kthread )
{
	if ( kthread )
		return kthread->sched_priority;
	else
		return active_thread->sched_priority;
}
int kthread_set_prio ( kthread_t *kthread, int prio )
{
	kthread_t *kthr = kthread;
	int old_prio;

	if ( !kthr )
		kthr = active_thread;

	old_prio = kthr->sched_priority;

	/* change thread priority:
	(i)	if its active: change priority and move to ready
	(ii)	if its ready: remove from queue, change priority, put back
	(iii)	if its blocked: if queue is sorted by priority, same as (ii)
	*/
	switch ( kthr->state )
	{
	case THR_STATE_ACTIVE:
		kthr->sched_priority = prio;
		kthread_move_to_ready ( kthr, LAST );
		kthreads_schedule ();
		break;

	case THR_STATE_READY:
		kthread_remove_from_ready (kthr);
		kthr->sched_priority = prio;
		kthread_move_to_ready ( kthr, LAST );
		kthreads_schedule ();
		break;

	case THR_STATE_WAIT: /* as now there is only FIFO queue */
		kthr->sched_priority = prio;
		break;

	case THR_STATE_PASSIVE: /* report error or just change priority? */
		kthr->sched_priority = prio;
		break;
	}

	return old_prio;
}

/*! Get-ers, Set-ers and misc ----------------------------------------------- */

inline int kthread_is_active ( kthread_t *kthread )
{
	ASSERT ( kthread );
	if ( kthread->state == THR_STATE_ACTIVE )
		return TRUE;
	else
		return FALSE;
}

inline int kthread_is_ready ( kthread_t *kthread )
{
	ASSERT ( kthread );

	return	kthread->state == THR_STATE_ACTIVE ||
		kthread->state == THR_STATE_READY;
}

inline int kthread_is_alive ( kthread_t *kthread )
{
	return	kthread->state != THR_STATE_PASSIVE &&
		kthread_check_kthread ( kthread );
}

inline int kthread_is_passive ( kthread_t *kthread )
{
	ASSERT ( kthread );
	if ( kthread->state == THR_STATE_PASSIVE )
		return TRUE;
	else
		return FALSE;
}

inline int kthread_is_suspended (kthread_t *kthread, void **func, void **param)
{
	if ( !kthread )
		kthread = active_thread;

	if ( kthread->state == THR_STATE_SUSPENDED )
	{
		if ( func )
			*func = kthread->cancel_suspend_handler;
		if ( param )
			*param = kthread->cancel_suspend_param;

		return TRUE;
	}

	return FALSE;
}

/*! check if thread descriptor is valid, i.e. is in all thread list */
inline int kthread_check_kthread ( kthread_t *kthread )
{
	return kthread && list_find ( &all_threads, &kthread->all );
}

inline int kthread_get_id ( kthread_t *kthread )
{
	if ( kthread )
		return kthread->id;
	else
		return active_thread->id;
}

inline kthread_t *kthread_get_active ()
{
	return active_thread;
}

inline void *kthread_get_context ( kthread_t *kthread )
{
	if ( kthread )
		return &kthread->context;
	else
		return &active_thread->context;
}

/*! Get kernel thread descriptor from user thread descriptor */
inline kthread_t *kthread_get_descriptor ( pthread_t *thread )
{
	kthread_t *kthread;

	if ( thread && (kthread = thread->ptr) && thread->id == kthread->id &&
		kthread->state != THR_STATE_PASSIVE )
		return kthread;
	else
		return NULL;
}

inline void kthread_set_active ( kthread_t *kthread )
{
	ASSERT ( kthread );
	active_thread = kthread;
	active_thread->state = THR_STATE_ACTIVE;
	active_thread->queue = NULL;
}
inline void kthread_mark_ready ( kthread_t *kthread )
{
	ASSERT ( kthread );
	kthread->state = THR_STATE_READY;
}
inline void kthread_set_queue ( kthread_t *kthread, kthread_q *queue )
{
	ASSERT ( kthread && queue );
	kthread->queue = queue;
}
inline kthread_q *kthread_get_queue ( kthread_t *kthread )
{
	ASSERT ( kthread );
	return kthread->queue;
}


/*! Temporary storage for blocked thread (save specific context before wait) */
inline void kthread_set_private_param ( kthread_t *kthread, void *pparam )
{
	if ( !kthread )
		kthread = active_thread;
	kthread->pparam = pparam;
}
inline void *kthread_get_private_param ( kthread_t *kthread )
{
	if ( !kthread )
		kthread = active_thread;
	return kthread->pparam;
}


/*! errno and return value */
inline void kthread_set_errno1 ( kthread_t *kthread, int error_number )
{
	if ( !kthread )
		kthread = active_thread;
	kthread->errno = error_number;
}
inline int kthread_get_errno ( kthread_t *kthread )
{
	if ( kthread )
		return kthread->errno;
	else
		return active_thread->errno;
}
inline int *kthread_get_errno_ptr ( kthread_t *kthread )
{
	if ( kthread )
		return &kthread->errno;
	else
		return &active_thread->errno;
}
inline void kthread_set_syscall_retval ( kthread_t *kthread, int ret_val )
{
	if ( !kthread )
		kthread = active_thread;
	ASSERT ( kthread );

	kthread->retval = ret_val;
}
inline int kthread_get_syscall_retval ( kthread_t *kthread )
{
	if ( !kthread )
		kthread = active_thread;
	ASSERT ( kthread );

	return kthread->retval;
}


/*! display active & ready threads info on console */
int kthread_info ()
{
	kthread_t *kthread;
	int i = 1;

	kprintf ( "Threads info\n" );

	kprintf ( "[this]\tid=%d (desc. at %x)\n",
		  active_thread->id, active_thread );

	kprintf ( "\tprio=%d, state=%d, exit_status=%x\n",
		  active_thread->sched_priority, active_thread->state,
		  active_thread->exit_status );

	kthread = list_get ( &all_threads, FIRST );
	while ( kthread )
	{
		kprintf ( "[%d]\tid=%d (desc. at %x)\n",
			  i++, kthread->id, kthread );

		kprintf ( "\tprio=%d, state=%d, exit_status=%x\n",
			  kthread->sched_priority, kthread->state,
			  kthread->exit_status );

		kthread = list_get_next ( &kthread->all );
	}

	return 0;
}

/*! Idle thread ------------------------------------------------------------- */
/*! Idle thread starting (and only) function */
static void idle_thread ( void *param )
{
	while (1)
		suspend ();
}

/*! Change thread scheduling parameters ------------------------------------- */
int kthread_setschedparam (kthread_t *kthread, int policy, sched_param_t *param)
{
	int sched_priority;

	ASSERT_AND_RETURN_ERRNO ( kthread, EINVAL );
	ASSERT_AND_RETURN_ERRNO ( kthread_is_alive (kthread), ESRCH );
	ASSERT_AND_RETURN_ERRNO ( policy == SCHED_FIFO, ENOTSUP );

	if ( param )
	{
		ASSERT_AND_RETURN_ERRNO (
			param->sched_priority >= THREAD_MIN_PRIO &&
			param->sched_priority <= THREAD_MAX_PRIO, EINVAL );

		if ( param->sched_priority )
			sched_priority = param->sched_priority;
		else
			sched_priority = kthread->sched_priority;
	}
	else {
		sched_priority = kthread->sched_priority;
	}

	/* change in priority? */
	if ( kthread->sched_priority != sched_priority )
		kthread_set_prio ( kthread, sched_priority );

	return EXIT_SUCCESS;
}
