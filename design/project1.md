# Project 1 Design Report

## Analysis of the current implementation

### Thread System

```c
struct thread
{
  /* Owned by thread.c. */
  tid_t tid;                 /* Thread identifier. */
  enum thread_status status; /* Thread state. */
  char name[16];             /* Name (for debugging purposes). */
  uint8_t *stack;            /* Saved stack pointer. */
  int priority;              /* Priority. */
  struct list_elem allelem;  /* List element for all threads list. */

  /* Shared between thread.c and synch.c. */
  struct list_elem elem; /* List element. */

#ifdef USERPROG
  /* Owned by userprog/process.c. */
  uint32_t *pagedir; /* Page directory. */
#endif

  /* Owned by thread.c. */
  unsigned magic; /* Detects stack overflow. */
};
```

핀토스의 스레드는 `thread.h` 파일에 정의된 `thread` 구조체로 관리된다. `thread` 구조체는 thread id(`tid`), 상태(`status`), 스택 포인터(`stack`), 우선 순위(`priority`)를 저장한다.

구조체의 가장 마지막에 있는 `magic` 필드는 스택 오버플로우를 감지하는 데에 사용된다. 핀토스에서 스레드는 palloc 메모리 할당기에 의해 한 페이지 (4096바이트)를 할당받고, `tid`, `status` 등의 `thread` 구조체 필드들은 0바이트부터 저장되고, 스택의 데이터는 4KB부터 0바이트를 향해 쌓이기 때문에 스택에 너무 많은 데이터가 저장된다면 스레드의 데이터를 덮어쓸 위험이 있다. 만약 스택에 과도하게 데이터가 쌓인다면 구조체의 `magic` 필드가 가장 먼저 덮어 씌어질 것이고, 핀토스는 이 `magic` 필드가 지정된 값 (`THREAD_MAGIC`)값과 달라질 때 스택 오버플로우가 발생한 것으로 판정하고 커널 패닉을 일으킨다.

핀토스 스레드의 상태는 `thread_status` enum의 네 가지 중 하나의 값으로 결정된다.

```c
enum thread_status
{
  THREAD_RUNNING, /* Running thread. */
  THREAD_READY,   /* Not running but ready to run. */
  THREAD_BLOCKED, /* Waiting for an event to trigger. */
  THREAD_DYING    /* About to be destroyed. */
};
```

핀토스 스레드의 우선 순위는 0 (`PRI_MIN`) 이상 63 (`PRI_MAX`) 이하의 정수 값이고, 기본 우선 순위는 31 (`PRI_DEFAULT`) 이다.

`THREAD_RUNNING`, `THREAD_READY`, `THREAD_BLOCKED`, `THREAD_DYING` 각각의 의미는 다음과 같다.

- `THREAD_RUNNING`: 스레드가 현재 실행 중
- `THREAD_READY`: 스레드가 실행 중은 아니지만 언제든지 실행될 수 있는 상태
- `THREAD_BLOCKED`: 스레드가 특정 조건을 만족할 때까지 기다리는 상태
- `THREAD_DYING`: 곧 삭제될 상태

핀토스의 스레드 시스템에서는 모든 스레드를 모아 놓은 리스트인 `all_list`와 THREAD_READY 상태인 스레드를 모아 놓은 `ready_list` 두 개의 리스트를 관리하면서 스레드를 추가, 삭제하고 스케줄링한다.

#### Thread-Related Functions

##### `thread_init`

```c
void
thread_init (void)
{
  ASSERT (intr_get_level () == INTR_OFF);

  lock_init (&tid_lock);
  list_init (&ready_list);
  list_init (&all_list);

  /* Set up a thread structure for the running thread. */
  initial_thread = running_thread ();
  init_thread (initial_thread, "main", PRI_DEFAULT);
  initial_thread->status = THREAD_RUNNING;
  initial_thread->tid = allocate_tid ();
}
```

thread id 할당을 위한 `lock`, `tid_lock` 초기화, ready list와 전체 스레드 리스트를 초기화한 다음, 메인 스레드를 생성한다.

##### `thread_start`

```c
void
thread_start (void)
{
  /* Create the idle thread. */
  struct semaphore idle_started;
  sema_init (&idle_started, 0);
  thread_create ("idle", PRI_MIN, idle, &idle_started);

  /* Start preemptive thread scheduling. */
  intr_enable ();

  /* Wait for the idle thread to initialize idle_thread. */
  sema_down (&idle_started);
}
```

idle thread를 생성하고, 인터럽트를 활성화한다. Idle thread가 생성됨을 확인한 다음 함수를 종료하기 위해 `idle_started` 세마포어를 사용한다.

##### `thread_tick`

```c
void
thread_tick (void)
{
  struct thread *t = thread_current ();

  /* Update statistics. */
  if (t == idle_thread)
    idle_ticks++;
#ifdef USERPROG
  else if (t->pagedir != NULL)
    user_ticks++;
#endif
  else
    kernel_ticks++;

  /* Enforce preemption. */
  if (++thread_ticks >= TIME_SLICE)
    intr_yield_on_return ();
}
```

타이머 인터럽트에 의해 실행되는 함수이다. 현재 실행 중인 스레드가 idle thread인지, 아닌지에 따라 구분하여 `idle_ticks` 혹은 `kernel_ticks` 변수를 증가시킨다. 또한 `thread_ticks`를 증가시킨 뒤 `TIME_SLICE` 이상 지났는지 확인하여 일정 주기마다 `thread_yield`를 실행되도록 만든다. 이때 `thread_yield` 대신 `intr_yield_on_return`을 사용하는 이유는 `thread_tick` 함수가 타이머 인터럽트에 의해 실행되는 함수이기 때문이다. 인터럽트 도중에 스레드를 sleep 시킬 수 없기 때문에, 인터럽트의 처리가 끝난 후에 `thread_yield`가 실행되도록 한다.

##### `thread_create`

```c
tid_t
thread_create (const char *name, int priority,
               thread_func *function, void *aux)
{
  struct thread *t;
  struct kernel_thread_frame *kf;
  struct switch_entry_frame *ef;
  struct switch_threads_frame *sf;
  tid_t tid;

  ASSERT (function != NULL);

  /* Allocate thread. */
  t = palloc_get_page (PAL_ZERO);
  if (t == NULL)
    return TID_ERROR;

  /* Initialize thread. */
  init_thread (t, name, priority);
  tid = t->tid = allocate_tid ();

  /* Stack frame for kernel_thread(). */
  kf = alloc_frame (t, sizeof *kf);
  kf->eip = NULL;
  kf->function = function;
  kf->aux = aux;

  /* Stack frame for switch_entry(). */
  ef = alloc_frame (t, sizeof *ef);
  ef->eip = (void (*) (void))kernel_thread;

  /* Stack frame for switch_threads(). */
  sf = alloc_frame (t, sizeof *sf);
  sf->eip = switch_entry;
  sf->ebp = 0;

  /* Add to run queue. */
  thread_unblock (t);

  return tid;
}
```

주어진 함수 `function`에 인자 `aux`를 주어 실행하는 스레드를 생성하고, 실행 가능한 상태가 되도록 `thread` 구조체의 각종 필드를 초기화한다. `thread_unblock` 함수를 사용하여 ready list에 생성된 스레드를 넣은 다음 생성된 스레드의 thread id를 반환한다.

##### `thread_block`

```c
void
thread_block (void)
{
  ASSERT (!intr_context ());
  ASSERT (intr_get_level () == INTR_OFF);

  thread_current ()->status = THREAD_BLOCKED;
  schedule ();
}
```

현재 스레드의 상태를 `THREAD_BLOCKED`로 설정하고, `schedule` 함수를 실행한다. 인터럽트 처리 도중에 스레드를 block시킬 경우 OS 자체를 멈추는 결과를 일으킬 수 있기 때문에, `ASSERT`를 통해 인터럽트 처리 중 이 함수가 실행될 경우 커널 패닉이 일어나게 한다.

### Synchronization Primitives

## Design Plan

### Alarm Clock

### Priority Scheduler

### Advanced Scheduler
