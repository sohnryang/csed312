# Project 1 Final Report

## Solution

### Alarm Clock

### Priority Scheduler

### Advanced Scheduler

## Discussion

### Interrupt Check in `sema_up`

핀토스의 `sema_up` 함수 주석에 나와 있듯, `sema_up` 함수는 인터럽트 핸들러에서 실행될 수도 있다. 초기 구현에서는 만약 세마포어를 기다리고 있다가 unblock된 스레드가 현재 스레드보다 우선순위가 높을 경우, `thread_yield` 함수를 실행해 preemption이 일어나게 만들었다.

하지만 앞서 design report에서 설명했듯 `thread_yield`와 같이 인터럽트 처리 중 스레드를 멈출 수 있는 함수가 실행될 경우 OS 전체가 멈추는 결과가 일어날 수 있기 때문에, `thread_yield` 함수 구현에서는 `ASSERT`를 사용하여 이를 명시적으로 막는다. 다르게 말하면, 인터럽트 실행 중 `sema_up`에서 preemption이 일어나는 경우 커널 패닉이 발생한다. `make check`를 통해 수행되는 테스트를 비롯해 project 1 관련 코드에서는 인터럽트 핸들러에서 `sema_up`을 실행하는 곳이 없지만, `devices/ide.c`와 같이 이후 project에서 실행하는 코드에서는 앞서 설명한 이유로 커널 패닉이 충분히 일어날 수 있다. 따라서 preemption이 일어날 수 있는 상황일 때, `intr_context` 함수를 실행해 인터럽트 처리 중인지를 확인하고, 만약 그렇다면 `thread_yield` 대신 `intr_yield_on_return` 함수를 사용해 인터럽트 처리가 끝난 뒤에 preemption이 일어나도록 수정하였다.

### A Bug in Advanced Scheduler