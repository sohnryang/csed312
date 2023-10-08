# Project 1 Final Report

## Solution

### Alarm Clock

기존의 Alarm clock 구현은 busy wait 방식으로,  thread가 CPU를 점유하며 tick을 기다리는 방법이었다. 이를 CPU를 점유하지 않는 방법으로 재구현하기 위해, 현재 스레드를 block하여 계속 스케줄링 당하지 않도록 구현했다. (커밋 `074d7e5` 참고)

각 스레드가 깨어날 시간을 저장하기 위하여 `struct thread`에 `int64_t wakeup_ticks` 변수를 만들어서 저장했다.

새로 구현한 alarm clock의 기능은 다음과 같이 동작한다.

* `timer_sleep`함수가 호출된다. (`devices/timer.c`)
  * `timer_sleep`함수가 호출되면, 현재 스레드의 `wakeup_ticks`를 스레드가 unblock되어야 할 시간으로 설정한다.
  * 이후, `sleeping_list`에 `wakeup_ticks`가 작은 순서대로 삽입한다. ($O(n)$의 시간 소요.)
    * 이를 비교하기 위한 함수로 `thread_compare_wakeup`을 구현하였다. 리스트 내에 들어간 `list_elem`을 비교하며, `list_elem`으로부터 구조체를 역참조할 수 있게 하는 매크로인 `list_entry`를 사용해서 `thread`를 얻고, 이 `wakeup_ticks`를 비교한다. `list_entry`가 역참조 할 때 메모리의 offset을 계산하기 때문에 스레드를 구한 후 `is_thread`를 사용하여 검증을 해야 안전하다.
  * 현재 스레드를 block한다.
* `timer_inturrupt`가 호출된다. (`devices/timer.c`)

  * `tick`을 증가시키고,
  * `sleeping_list`가 비어있지 않는 한
    * 해당 list의  `wakeup_ticks` 가 현재 `tick`보다 작거나 같은 스레드를 `sleeping_list`에서 제거하고 unblock한다. ($O(1)$의 시간 소요. - 앞에서부터 하나씩 제거가 가능하다.)

`sleeping_list`는 block된 스레드들로 구성되어 있기 때문에 CPU에 의해 스케쥴되지 않는다. 다른 스레드에게 CPU를 양도할 수 있게 된다.

### Priority Scheduler

#### Basic Implementation

우선순위에 맞게 스레드가 실행되기 위해서는 핀토스의 `schedule` 함수에서 `ready_list`로부터 원소를 pop할 때, 가장 먼저 pop되는 스레드가 `ready_list`에 있는 스레드 중에서 가장 우선순위가 높아야 한다. 핀토스의 스레드는 `thread_unblock`, `thread_yield`를 통해 ready 상태가 될 때 `ready_list`에 들어가므로, 이들 함수를 수정하여 높은 우선순위 스레드가 먼저 올 수 있도록 `list_insert_ordered`를 사용해 `ready_list`에 추가하도록 하였다. (커밋 `bd1b38c` 참고)

또한, 세마포어와 같은 synchronization primitive의 접근에 대해서도 앞서 `ready_list`를 순서를 맞췄던 것처럼, 우선순위가 높은 스레드가 `waiters` 리스트에서 먼저 오도록 `list_insert_ordered`, `list_sort` 등을 사용하도록 세마포어, lock, condition variable의 구현을 수정하였다. (커밋 `6b3e869`, `bc5dbd8`, `c8797d6` 참고)

#### Preemption

만약 핀토스가 시작해서 종료할 때까지 실행할 스레드가 고정되어 있고, 우선순위가 바뀌지 않는다면 앞서 언급한 `ready_list`에 `list_insert_ordered` 함수를 사용하여 스레드를 삽입하는 알고리즘으로 충분할 것이다. 하지만, 실제 핀토스에서는 새로운 스레드가 생성되거나, 이미 실행되는 중인 스레드의 우선 순위가 변경되는 것이 충분히 가능하다. 현재 실행 중인 스레드보다 우선순위가 높은 스레드가 생성되거나, 현재 실행 중인 스레드의 우선순위가 변경되어 `ready_list`에 현재 스레드보다 우선순위가 높은 스레드가 존재한다면, preemption을 통해 더 높은 우선순위의 스레드가 실행될 수 있어야 한다. Preemption은 스레드가 생성되는 `thread_create`, `thread_set_priority`에서 발생할 수 있기 때문에, 두 함수의 뒤에서 `thread_could_preempt` 함수로 검사한 다음 preempt 가능할 때 `thread_yield`를 실행하도록 하였다. (커밋 `556c0f1`, `1f8cced` 참고)

Synchronization primitive에서도 preemption이 일어나는 것이 가능하다. `sema_up`에서 `thread_unblock`을 실행하여 `ready_list`에 현재 실행 중인 스레드보다 더 우선순위가 높은 스레드가 추가되는 상황이 충분히 가능하다. 따라서 `sema_up`에서 `thread_unblock`을 수행한 뒤, 만약 preemption이 가능한 경우를 검사하였다. `sema_up`의 경우 인터럽트 핸들러에서 호출될 수 있는 함수이기 때문에, 이후 discussion에서 설명할 것처럼 인터럽트 핸들러에서 실행되는 경우를 따로 처리하였다. (커밋 `6b3e869`, `97a3896` 참고)

#### Priority Donation

Design report에서 설명했듯, 핀토스의 lock을 단순히 priority scheduling에 따라 구현하면 priority inversion이 일어날 가능성이 존재한다. 따라서 `lock_acquire`에서 기다리는 중인 스레드는 현재 lock을 소유하고 있는 스레드에 우선 순위를 기부한다. 우선 순위 기부를 위해 `lock_acquire`에서 `lock`의 `holder`에 대해, `priority_donor` 리스트에 현재 스레드를 추가한 다음, `thread_donate_priority` 함수를 실행하여 현재 스레드가 기다리고 있는 lock의 소유자를 거슬러 올라가면서 `priority_donor` 리스트에서 최대 priority를 구해 donation을 수행하도록 구현하였다. (커밋 `556c0f1` 참고)

반면, lock을 해제하는 상황에서는 우선 `thread_lock_clear` 함수를 호출한다. 이 함수에서는 현재 스레드의 `priority_donor`에 있는 스레드 중에서, 해제 중인 lock을 acquire하기 위해 기다리는 스레드를 `priority_donor`에서 제거한다. 이후, `thread_update_priority` 함수를 호출하여 `priority_donor` 리스트에 남아 있는 스레드에서 donation을 계산한다. (커밋 `459011e`, `50c01a8` 참고)

### Advanced Scheduler

구현한 MLFQS scheduler는 일정 시간마다 실시간으로 priority를 업데이트하는 스케줄러이다. 이를 위해, `timer_interrupt` 함수에 일정 틱마다 우선순위를 결정하는 데 필요한 값들을 설정하도록 구현했으며, 연산에 필요한 스레드와 관련되있는 상수를 `MLFQS_[NAME]`의 형태로 `thread.h`에 정의하였다. (커밋 `f26f860` 참고, Bug fix: 커밋 `c3afce5` 참고)

기존에 구현된 Priority scheduler에서 사용하던 `thread_set_priority`함수는 사용하지 않는다.

* 매 tick마다 현재 스레드의 `recent_cpu`값을 증가시킨다 - `thread_mlfqs_inc_recent_cpu`
* `MLFQS_PRIORITY_UPDATE_FREQ` (4) tick마다 모든 스레드의 priority를 업데이트한다. - `thread_mlfqs_set_priority`
* `TIMER_FREQ`(100) tick마다 `load_avg`를 최근 60개 구간의 구간 이동 평균을 계산하여 갱신하고, 모든 스레드의 `recent_cpu` 값을 재조정한다. - `thread_mlfqs_load_avg`, `thread_foreach`

각 스레드의 값을 조절하기 위한 `thread_mlfqs_[operation]` 형태의 함수들은 `thread.c`에 작성하였다. `thread_mlfqs_inc_recent_cpu` 및 `therad_mlfqs_update_load_avg` 등 대부분의 함수들에서 현재 스레드가 `idle_thread`인지 아닌지를 판별하는 것이 중요하며, 이는 `idle_thread`는 CPU를 차지하는 스레드가 없을 경우 CPU를 점유하는, scheduler에 독립적인 스레드이기 때문이다.

계산 과정에서 소수 값이 필요할 수 있으며, (최종적으로 계산된 priority는 소수 값을 round하여 `int`로 사용하게 된다), 소수 값을 나타내기 위해 fixed-point notation을 나타내기 위해 `fp_arithmetic.h` 에 `int32_t`와 `int64_t`를 각각 `fp_t`와 `fp_lt`로 정의했다. 또한, 간단한 정수를 곱하거나 나누는 경우가 아닐 때 코드를 간결하게 구성하기 위해 `FIFTYNINE_SIXTIETH`와 `ONE_SIXTIETH`를 미리 정의하였다. (커밋 `a73d443` 참고)

기존에 핀토스를 빌드하기 위한 `Makefile.build`에는 스레드의 기본적인 기능을 구현하기 위한 `.c` 파일만 존재했다. 명시적으로 고정소수점 연산을 include시켜서 빌드하기 위해 해당 파일에 `threads/fp_arithmetic.c`를 추가했다. (커밋 `caa2723` 참고)

기본적인 기능을 구현 후, 빌드를 위해 컴파일을 진행했을 때 `thread_foreach([thread_mlfqs function], NULL)`에서 Compile warning이 발생하였다. `[thread_mlfqs function]`은 인자로 `struct thread*` 타입의 인자를 1개만 받지만 `thread_foreach`를 통해 인자를 전달할 땐 `void *aux`를 전달한다. (아무 인자도 받지 않기 때문에 이 값은 `NULL`로 전달했었다.) 두 함수 간 명시적인 type의 conflict가 발생하여 Compile warning이 발생했으며, 이를 해결하기 위해 모든 스레드 (`all_list`) 를 순회하면서 기존의 기능을 동일하게 수행하는 함수인 `threa_mlfqs_set_priority_all`과 `thread_mlfqs_set_recent_cpu_all`을 작성했다. (커밋 `28d628e` 참고)

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