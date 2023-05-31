/*
 * Copyright (c) 2017 Simon Goldschmidt
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT
 * SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 *
 * This file is part of the lwIP TCP/IP stack.
 *
 * Author: Simon Goldschmidt <goldsimon@gmx.de>
 *
 */

/* lwIP includes. */
#include "lwip/debug.h"
#include "lwip/def.h"
#include "lwip/sys.h"
#include "lwip/mem.h"
#include "lwip/stats.h"
#include <rtthread.h>

#if !NO_SYS
#include "arch/sys_arch.h"
#endif

/** Set this to 1 to use a mutex for SYS_ARCH_PROTECT() critical regions.
 * Default is 0 and locks interrupts/scheduler for SYS_ARCH_PROTECT().
 */
#ifndef LWIP_RTT_SYS_ARCH_PROTECT_USES_MUTEX
#define LWIP_RTT_SYS_ARCH_PROTECT_USES_MUTEX     0
#endif

/** Set this to 1 to include a sanity check that SYS_ARCH_PROTECT() and
 * SYS_ARCH_UNPROTECT() are called matching.
 */
#ifndef LWIP_RTT_SYS_ARCH_PROTECT_SANITY_CHECK
#define LWIP_RTT_SYS_ARCH_PROTECT_SANITY_CHECK   1
#endif

/** Set this to 1 to enable core locking check functions in this port.
 * For this to work, you'll have to define LWIP_ASSERT_CORE_LOCKED()
 * and LWIP_MARK_TCPIP_THREAD() correctly in your lwipopts.h! */
#ifndef LWIP_RTT_CHECK_CORE_LOCKING
#define LWIP_RTT_CHECK_CORE_LOCKING              1
#endif

/** Set this to 0 to implement sys_now() yourself, e.g. using a hw timer.
 * Default is 1, where FreeRTOS ticks are used to calculate back to ms.
 */
#ifndef LWIP_RTT_SYS_NOW_FROM_RTT
#define LWIP_RTT_SYS_NOW_FROM_RTT           1
#endif

#ifndef RT_USING_HEAP
# error "lwIP RT-Thread port requires RT_USING_HEAP"
#endif
#ifndef RT_USING_MUTEX
# error "lwIP RT-Thread port requires RT_USING_MUTEX"
#endif
#ifndef RT_USING_MESSAGEQUEUE
# error "lwIP RT-Thread port requires RT_USING_MESSAGEQUEUE"
#endif
#ifndef RT_USING_SEMAPHORE
# error "lwIP RT-Thread port requires RT_USING_SEMAPHORE"
#endif
#ifndef RT_USING_MAILBOX
# error "lwIP RT-Thread port requires RT_USING_MAILBOX"
#endif


#if SYS_LIGHTWEIGHT_PROT && LWIP_RTT_SYS_ARCH_PROTECT_USES_MUTEX
static rt_mutex_t  sys_arch_protect_mutex;
#endif
#if SYS_LIGHTWEIGHT_PROT && LWIP_RTT_SYS_ARCH_PROTECT_SANITY_CHECK
static sys_prot_t sys_arch_protect_nesting;
#endif

/* Initialize this module (see description in sys.h) */
void
sys_init(void)
{
#if SYS_LIGHTWEIGHT_PROT && LWIP_RTT_SYS_ARCH_PROTECT_USES_MUTEX
  /* initialize sys_arch_protect global mutex */
  sys_arch_protect_mutex = rt_mutex_create ("SYS_ARCH_PROTECT", RT_IPC_FLAG_PRIO );
  LWIP_ASSERT("failed to create sys_arch_protect mutex",
              sys_arch_protect_mutex != RT_NULL);
#endif /* SYS_LIGHTWEIGHT_PROT && LWIP_RTT_SYS_ARCH_PROTECT_USES_MUTEX */
}

#if LWIP_RTT_SYS_NOW_FROM_RTT
u32_t
sys_now(void)
{
  return rt_tick_get() * (1000 / RT_TICK_PER_SECOND);
}
#endif

u32_t
sys_jiffies(void)
{
  return rt_tick_get();
}

#if SYS_LIGHTWEIGHT_PROT

sys_prot_t
sys_arch_protect(void)
{
#if LWIP_RTT_SYS_ARCH_PROTECT_USES_MUTEX
  rt_err_t  ret;
  LWIP_ASSERT("sys_arch_protect_mutex != RT_NULL", sys_arch_protect_mutex != RT_NULL);

  ret = rt_mutex_take (sys_arch_protect_mutex, RT_WAITING_FOREVER);
  LWIP_ASSERT("sys_arch_protect failed to take the mutex", ret == RT_EOK);
#else /* LWIP_RTT_SYS_ARCH_PROTECT_USES_MUTEX */
  rt_enter_critical();
#endif /* LWIP_RTT_SYS_ARCH_PROTECT_USES_MUTEX */
#if LWIP_RTT_SYS_ARCH_PROTECT_SANITY_CHECK
  {
    /* every nested call to sys_arch_protect() returns an increased number */
    sys_prot_t ret = sys_arch_protect_nesting;
    sys_arch_protect_nesting++;
    LWIP_ASSERT("sys_arch_protect overflow", sys_arch_protect_nesting > ret);
    return ret;
  }
#else
  return 1;
#endif
}

void
sys_arch_unprotect(sys_prot_t pval)
{
#if LWIP_RTT_SYS_ARCH_PROTECT_USES_MUTEX
  rt_err_t  ret;
#endif
#if LWIP_RTT_SYS_ARCH_PROTECT_SANITY_CHECK
  LWIP_ASSERT("unexpected sys_arch_protect_nesting", sys_arch_protect_nesting > 0);
  sys_arch_protect_nesting--;
  LWIP_ASSERT("unexpected sys_arch_protect_nesting", sys_arch_protect_nesting == pval);
#endif

#if LWIP_RTT_SYS_ARCH_PROTECT_USES_MUTEX
  LWIP_ASSERT("sys_arch_protect_mutex != RT_NULL", sys_arch_protect_mutex != RT_NULL);

  ret = rt_mutex_release(sys_arch_protect_mutex);
  LWIP_ASSERT("sys_arch_unprotect failed to give the mutex", ret == RT_EOK);
#else /* LWIP_RTT_SYS_ARCH_PROTECT_USES_MUTEX */
  rt_exit_critical();
#endif /* LWIP_RTT_SYS_ARCH_PROTECT_USES_MUTEX */
  LWIP_UNUSED_ARG(pval);
}

#endif /* SYS_LIGHTWEIGHT_PROT */

void
sys_arch_msleep(u32_t delay_ms)
{
  rt_thread_mdelay(delay_ms);
}

#if !LWIP_COMPAT_MUTEX

/* Create a new mutex*/
err_t
sys_mutex_new(sys_mutex_t *mutex)
{
  static uint16_t counter = 0;
  char tname[RT_NAME_MAX] = {0};
  LWIP_ASSERT("mutex != RT_NULL", mutex != RT_NULL);

  rt_snprintf(tname, RT_NAME_MAX, "lwip_mutex%d", counter);
  counter++;

  mutex->mut = rt_mutex_create(tname, RT_IPC_FLAG_PRIO);
  if(mutex->mut == RT_NULL) {
    SYS_STATS_INC(mutex.err);
    return ERR_MEM;
  }
  SYS_STATS_INC_USED(mutex);
  return ERR_OK;
}

void
sys_mutex_lock(sys_mutex_t *mutex)
{
  rt_err_t  ret;
  LWIP_ASSERT("mutex != RT_NULL", mutex != RT_NULL);
  LWIP_ASSERT("mutex->mut != RT_NULL", mutex->mut != RT_NULL);

  ret = rt_mutex_take(mutex->mut, RT_WAITING_NO);
  LWIP_ASSERT("failed to take the mutex", ret == RT_EOK);
}

void
sys_mutex_unlock(sys_mutex_t *mutex)
{
  rt_err_t  ret;
  LWIP_ASSERT("mutex != RT_NULL", mutex != RT_NULL);
  LWIP_ASSERT("mutex->mut != RT_NULL", mutex->mut != RT_NULL);

  ret = rt_mutex_release(mutex->mut);
  LWIP_ASSERT("failed to give the mutex", ret == RT_EOK);
}

void
sys_mutex_free(sys_mutex_t *mutex)
{
  LWIP_ASSERT("mutex != RT_NULL", mutex != RT_NULL);
  LWIP_ASSERT("mutex->mut != RT_NULL", mutex->mut != RT_NULL);

  SYS_STATS_DEC(mutex.used);
  rt_mutex_delete(mutex->mut);
  mutex->mut = RT_NULL;
}

#endif /* !LWIP_COMPAT_MUTEX */

err_t
sys_sem_new(sys_sem_t *sem, u8_t initial_count)
{
  static uint16_t counter = 0;
  char tname[RT_NAME_MAX] = {0};
  LWIP_ASSERT("sem != RT_NULL", sem != RT_NULL);
  LWIP_ASSERT("initial_count invalid (not 0 or 1)",
    (initial_count == 0) || (initial_count == 1));

  rt_snprintf(tname, RT_NAME_MAX, "lwip_sem%d", counter);
  counter++;

  sem->sem = rt_sem_create(tname, initial_count, RT_IPC_FLAG_FIFO);
  if(sem->sem == RT_NULL) {
    SYS_STATS_INC(sem.err);
    return ERR_MEM;
  }
  SYS_STATS_INC_USED(sem);

  return ERR_OK;
}

void
sys_sem_signal(sys_sem_t *sem)
{
  rt_err_t  ret;
  LWIP_ASSERT("sem != RT_NULL", sem != RT_NULL);
  LWIP_ASSERT("sem->sem != RT_NULL", sem->sem != RT_NULL);

  ret = rt_sem_release(sem->sem);
  LWIP_ASSERT("sys_sem_signal: sane return value", (ret == RT_EOK));
}

u32_t
sys_arch_sem_wait(sys_sem_t *sem, u32_t timeout_ms)
{
  rt_err_t  ret;
  LWIP_ASSERT("sem != RT_NULL", sem != RT_NULL);
  LWIP_ASSERT("sem->sem != RT_NULL", sem->sem != RT_NULL);

  if(!timeout_ms) {
    /* wait infinite */
    ret = rt_sem_take(sem->sem, RT_WAITING_FOREVER);
    LWIP_ASSERT("taking semaphore failed", ret == RT_EOK);
  } else {
    ret = rt_sem_take(sem->sem, rt_tick_from_millisecond(timeout_ms));
    if (ret == -RT_ETIMEOUT) {
      /* timed out */
      return SYS_ARCH_TIMEOUT;
    }
    LWIP_ASSERT("taking semaphore failed", ret == RT_EOK);
  }

  /* Old versions of lwIP required us to return the time waited.
     This is not the case any more. Just returning != SYS_ARCH_TIMEOUT
     here is enough. */
  return 1;
}

void
sys_sem_free(sys_sem_t *sem)
{
  LWIP_ASSERT("sem != RT_NULL", sem != RT_NULL);
  LWIP_ASSERT("sem->sem != RT_NULL", sem->sem != RT_NULL);

  SYS_STATS_DEC(sem.used);
  rt_sem_delete(sem->sem);
  sem->sem = RT_NULL;
}

err_t
sys_mbox_new(sys_mbox_t *mbox, int size)
{
  LWIP_ASSERT("mbox != RT_NULL", mbox != RT_NULL);
  LWIP_ASSERT("size > 0", size > 0);
  static uint16_t counter = 0;
  char tname[RT_NAME_MAX] = {0};

  rt_snprintf(tname, RT_NAME_MAX, "lwip_mbox_%d", counter);
  counter++;

  mbox->mbx = rt_mb_create(tname, (rt_size_t )size, RT_IPC_FLAG_FIFO );
  if(mbox->mbx == RT_NULL) {
    SYS_STATS_INC(mbox.err);
    return ERR_MEM;
  }
  SYS_STATS_INC_USED(mbox);
  return ERR_OK;
}

void
sys_mbox_post(sys_mbox_t *mbox, void *msg)
{
  rt_err_t  ret;
  LWIP_ASSERT("mbox != RT_NULL", mbox != RT_NULL);
  LWIP_ASSERT("mbox->mbx != RT_NULL", mbox->mbx != RT_NULL);

  ret = rt_mb_send_wait(mbox->mbx, (rt_ubase_t )msg, RT_WAITING_FOREVER);
  LWIP_ASSERT("mbox post failed", ret == RT_EOK);
}

err_t
sys_mbox_trypost(sys_mbox_t *mbox, void *msg)
{
  rt_err_t  ret;
  LWIP_ASSERT("mbox != RT_NULL", mbox != RT_NULL);
  LWIP_ASSERT("mbox->mbx != RT_NULL", mbox->mbx != RT_NULL);

  ret = rt_mb_send (mbox->mbx, (rt_ubase_t )msg);
  if (ret == RT_EOK) {
    return ERR_OK;
  } else {
    LWIP_ASSERT("mbox trypost failed", ret == -RT_EFULL);
    SYS_STATS_INC(mbox.err);
    return ERR_MEM;
  }
}

err_t
sys_mbox_trypost_fromisr(sys_mbox_t *mbox, void *msg)
{
  rt_err_t  ret;
  LWIP_ASSERT("mbox != RT_NULL", mbox != RT_NULL);
  LWIP_ASSERT("mbox->mbx != RT_NULL", mbox->mbx != RT_NULL);

  ret = rt_mb_send(mbox->mbx, (rt_ubase_t)msg);
  if (ret == RT_EOK) {
    return ERR_OK;
  } else {
    LWIP_ASSERT("mbox trypost failed", ret == -RT_EFULL);
    SYS_STATS_INC(mbox.err);
    return ERR_MEM;
  }
}

u32_t
sys_arch_mbox_fetch(sys_mbox_t *mbox, void **msg, u32_t timeout_ms)
{
  rt_err_t  ret;
  void *msg_dummy;
  LWIP_ASSERT("mbox != RT_NULL", mbox != RT_NULL);
  LWIP_ASSERT("mbox->mbx != RT_NULL", mbox->mbx != RT_NULL);

  if (!msg) {
    msg = &msg_dummy;
  }

  if (!timeout_ms) {
    /* wait infinite */
    ret = rt_mb_recv(mbox->mbx, (rt_ubase_t *)msg, RT_WAITING_FOREVER);
    LWIP_ASSERT("mbox fetch failed", ret == RT_EOK);
  } else {
    ret = rt_mb_recv(mbox->mbx, (rt_ubase_t *)msg, timeout_ms);
    if (ret == -RT_ETIMEOUT) {
      /* timed out */
      *msg = RT_NULL;
      return SYS_ARCH_TIMEOUT;
    }
    LWIP_ASSERT("mbox fetch failed", ret == RT_EOK);
  }

  /* Old versions of lwIP required us to return the time waited.
     This is not the case any more. Just returning != SYS_ARCH_TIMEOUT
     here is enough. */
  return 1;
}

u32_t
sys_arch_mbox_tryfetch(sys_mbox_t *mbox, void **msg)
{
  rt_err_t  ret;
  void *msg_dummy;
  LWIP_ASSERT("mbox != RT_NULL", mbox != RT_NULL);
  LWIP_ASSERT("mbox->mbx != RT_NULL", mbox->mbx != RT_NULL);

  if (!msg) {
    msg = &msg_dummy;
  }

  ret = rt_mb_recv(mbox->mbx, (rt_ubase_t *)msg, 0);
  if (ret == -RT_ETIMEOUT) {
    *msg = RT_NULL;
    return SYS_MBOX_EMPTY;
  }
  LWIP_ASSERT("mbox fetch failed", ret == RT_EOK);

  /* Old versions of lwIP required us to return the time waited.
     This is not the case any more. Just returning != SYS_ARCH_TIMEOUT
     here is enough. */
  return 1;
}

void
sys_mbox_free(sys_mbox_t *mbox)
{
  LWIP_ASSERT("mbox != RT_NULL", mbox != RT_NULL);
  LWIP_ASSERT("mbox->mbx != RT_NULL", mbox->mbx != RT_NULL);

  rt_mb_delete(mbox->mbx);

  SYS_STATS_DEC(mbox.used);
}

sys_thread_t
sys_thread_new(const char *name, lwip_thread_fn thread, void *arg, int stacksize, int prio)
{
  rt_thread_t rtos_task;
  sys_thread_t lwip_thread;

  LWIP_ASSERT("invalid stacksize", stacksize > 0);

  /* lwIP's lwip_thread_fn matches FreeRTOS' TaskFunction_t, so we can pass the
     thread function without adaption here. */
  rtos_task = rt_thread_create(name, thread, arg, (rt_uint32_t)stacksize, prio, 20);
  LWIP_ASSERT("task creation failed", rtos_task != RT_NULL);

  rt_thread_startup(rtos_task);

  lwip_thread.thread_handle = rtos_task;
  return lwip_thread;
}

#if LWIP_NETCONN_SEM_PER_THREAD
#error LWIP_NETCONN_SEM_PER_THREAD==1 not supported
#endif /* LWIP_NETCONN_SEM_PER_THREAD */

#if LWIP_RTT_CHECK_CORE_LOCKING
#if LWIP_TCPIP_CORE_LOCKING
/** The global semaphore to lock the stack. */
extern sys_mutex_t lock_tcpip_core;

/** Flag the core lock held. A counter for recursive locks. */
static u8_t lwip_core_lock_count;
static rt_thread_t lwip_core_lock_holder_thread;

void
sys_lock_tcpip_core(void)
{
   sys_mutex_lock(&lock_tcpip_core);
   if (lwip_core_lock_count == 0) {
     lwip_core_lock_holder_thread = rt_thread_self();
   }
   lwip_core_lock_count++;
}

void
sys_unlock_tcpip_core(void)
{
   lwip_core_lock_count--;
   if (lwip_core_lock_count == 0) {
       lwip_core_lock_holder_thread = 0;
   }
   sys_mutex_unlock(&lock_tcpip_core);
}

#endif /* LWIP_TCPIP_CORE_LOCKING */

#if !NO_SYS
static rt_thread_t lwip_tcpip_thread;
#endif

void
sys_mark_tcpip_thread(void)
{
#if !NO_SYS
  lwip_tcpip_thread = rt_thread_self();
#endif
}

void
sys_check_core_locking(void)
{
  /* Embedded systems should check we are NOT in an interrupt context here */
  /* E.g. core Cortex-M3/M4 ports:
         configASSERT( ( portNVIC_INT_CTRL_REG & portVECTACTIVE_MASK ) == 0 );

     Instead, we use more generic FreeRTOS functions here, which should fail from ISR: */
  rt_enter_critical();
  rt_exit_critical();

#if !NO_SYS
  if (lwip_tcpip_thread != 0) {
    rt_thread_t current_thread = rt_thread_self();

#if LWIP_TCPIP_CORE_LOCKING
    LWIP_ASSERT("Function called without core lock",
                current_thread == lwip_core_lock_holder_thread && lwip_core_lock_count > 0);
#else /* LWIP_TCPIP_CORE_LOCKING */
    LWIP_ASSERT("Function called from wrong thread", current_thread == lwip_tcpip_thread);
#endif /* LWIP_TCPIP_CORE_LOCKING */
  }
#endif /* !NO_SYS */
}

#endif /* LWIP_RTT_CHECK_CORE_LOCKING*/
