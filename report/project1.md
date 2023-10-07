# Project 1 Final Report

## Solution

### Alarm Clock

기존의 Alarm clock 구현은 busy wait 방식으로,  thread가 CPU를 점유하며 tick을 기다리는 방법이었다. 이를 CPU를 점유하지 않는 방법으로 재구현하기 위해, 현재 스레드를 block하여 계속 스케줄링 당하지 않도록 구현했다.

새로 구현한 alarm clock의 기능은 다음과 같이 동작한다.

* `timer_sleep`함수가 호출된다. (`devices/timer.c`)

  * `timer_sleep`함수가 호출되면, 현재 스레드의 `wakeup_ticks`를 스레드가 unblock되어야 할 시간으로 설정한다.

  * 이후, `sleeping_list`에 `wakeup_ticks`가 작은 순서대로 삽입한다. ($O(n)$의 시간 소요.)
    * 이를 비교하기 위한 함수로 `thread_compare_wakeup`을 구현하였다.

  * 현재 스레드를 block한다.

* `timer_inturrupt`가 호출된다. (`devices/timer.c`)

  * `tick`을 증가시키고,
  * `sleeping_list`가 비어있지 않는 한
    * 해당 list의  `wakeup_ticks` 가 현재 `tick`보다 작거나 같은 스레드를 `sleeping_list`에서 제거하고 unblock한다. ($O(1)$의 시간 소요.)

`sleeping_list`는 block된 스레드들로 구성되어 있기 때문에 CPU에 의해 스케쥴되지 않는다. 다른 스레드에게 CPU를 양도할 수 있게 된다.

### Priority Scheduler

### Advanced Scheduler

## Discussion

### Priority Check in Condition Variables

`cond_signal`에서 `waiters` 리스트에 있는 `semaphore_elem` 원소를 꺼낼 때, 세마포어 간의 우선순위를 고려하지 않는 버그가 발생하였다. 해당 버그를 수정하기 위하여 두 세마포어에 대해, 각 세마포어를 기다리는 스레드 중 가장 우선순위가 높은 것끼리 비교하는 `semaphore_compare_priority` 함수를 구현하였고, 이 함수에 따라 세마포어 리스트를 정렬하는 것으로 해결하였다.

해당 버그는 커밋 `c8797d6`에서 수정되었다.

### Interrupt Check in `sema_up`

핀토스의 `sema_up` 함수 주석에 나와 있듯, `sema_up` 함수는 인터럽트 핸들러에서 실행될 수도 있다. 초기 구현에서는 만약 세마포어를 기다리고 있다가 unblock된 스레드가 현재 스레드보다 우선순위가 높을 경우, `thread_yield` 함수를 실행해 preemption이 일어나게 만들었다.

하지만 앞서 design report에서 설명했듯 `thread_yield`와 같이 인터럽트 처리 중 스레드를 멈출 수 있는 함수가 실행될 경우 OS 전체가 멈추는 결과가 일어날 수 있기 때문에, `thread_yield` 함수 구현에서는 `ASSERT`를 사용하여 이를 명시적으로 막는다. 다르게 말하면, 인터럽트 실행 중 `sema_up`에서 preemption이 일어나는 경우 커널 패닉이 발생한다. `make check`를 통해 수행되는 테스트를 비롯해 project 1 관련 코드에서는 인터럽트 핸들러에서 `sema_up`을 실행하는 곳이 없지만, `devices/ide.c`와 같이 이후 project에서 실행하는 코드에서는 앞서 설명한 이유로 커널 패닉이 충분히 일어날 수 있다. 따라서 preemption이 일어날 수 있는 상황일 때, `intr_context` 함수를 실행해 인터럽트 처리 중인지를 확인하고, 만약 그렇다면 `thread_yield` 대신 `intr_yield_on_return` 함수를 사용해 인터럽트 처리가 끝난 뒤에 preemption이 일어나도록 수정하였다.

해당 버그는 커밋 `97a3896`에서 수정되었다.

### A Bug in Advanced Scheduler

Advanced scheduler를 구현하고 테스트하는 도중 `mlfqs-nice-10` 테스트에서 스레드가 실행된 tick count가 테스트에서 요구하는 것과 다른 상황이 발생하였다.

```
FAIL tests/threads/mlfqs-nice-10
Some tick counts were missing or differed from those expected by more than 25.
thread   actual <-> expected explanation
------ -------- --- -------- ----------------------------------------
     0      701 >>> 672      Too big, by 4.
     1      601  =  588
     2      497  =  492
     3      400  =  408
     4      300  =  316
     5      220  =  232
     6      144  =  152
     7       89  =  92
     8       40  =  40
     9       12  =  8
```

출력에서 볼 수 있듯 0번 스레드가 필요한 것보다 더 많이 실행되었다. 코드를 확인한 결과 `timer_interrupt` 함수에서 load average보다 각 프로세스의 recent_cpu 값을 먼저 세팅하는 것을 볼 수 있었다. 이런 코드의 경우 `load_avg`값은 0으로 초기화되기 때문에, OS 실행 초기에 `(1/60) * ready_threads`만큼의 오차가 발생한다. `load_avg`는 일종의 exponentially weighted moving average이기 때문에, 이 오차는 시간이 지남에 따라 감소하게 된다. 따라서 수정 이전 코드에서 나온 결과와 같이, 먼저 실행된 0번 스레드가 요구되는 것보다 더 많이 실행되고, 나머지 스레드의 경우에는 시간이 지나 초기에 발생한 오차가 감소한 뒤에 생성되어 실행되기 때문에 위와 같은 현상이 발생하는 것으로 추정된다.

해당 버그는 커밋 `c3afce5`에서 수정되었다.