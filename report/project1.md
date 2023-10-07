# Project 1 Final Report

## Solution

### Alarm Clock

### Priority Scheduler

### Advanced Scheduler

## Discussion

### Interrupt Check in `sema_up`

핀토스의 `sema_up` 함수 주석에 나와 있듯, `sema_up` 함수는 인터럽트 핸들러에서 실행될 수도 있다. 초기 구현에서는 만약 세마포어를 기다리고 있다가 unblock된 스레드가 현재 스레드보다 우선순위가 높을 경우, `thread_yield` 함수를 실행해 preemption이 일어나게 만들었다.

하지만 앞서 design report에서 설명했듯 `thread_yield`와 같이 인터럽트 처리 중 스레드를 멈출 수 있는 함수가 실행될 경우 OS 전체가 멈추는 결과가 일어날 수 있기 때문에, `thread_yield` 함수 구현에서는 `ASSERT`를 사용하여 이를 명시적으로 막는다. 다르게 말하면, 인터럽트 실행 중 `sema_up`에서 preemption이 일어나는 경우 커널 패닉이 발생한다. `make check`를 통해 수행되는 테스트를 비롯해 project 1 관련 코드에서는 인터럽트 핸들러에서 `sema_up`을 실행하는 곳이 없지만, `devices/ide.c`와 같이 이후 project에서 실행하는 코드에서는 앞서 설명한 이유로 커널 패닉이 충분히 일어날 수 있다. 따라서 preemption이 일어날 수 있는 상황일 때, `intr_context` 함수를 실행해 인터럽트 처리 중인지를 확인하고, 만약 그렇다면 `thread_yield` 대신 `intr_yield_on_return` 함수를 사용해 인터럽트 처리가 끝난 뒤에 preemption이 일어나도록 수정하였다.

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