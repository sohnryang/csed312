# Project 2 Final Report

## Solution

### Process Control Block

Process를 구현할 때, Process를 관리하기 위한 구조체가 필요하다. 따라서 이를 `struct pcb`로 정의하여 `thread.c`에서 `struct thread`의 member로 추가하였다. PCB에는 다음과 같은 정보를 담는다.

* Process ID
* Exit Code
* `bool load_success`: 프로세스를 Load하는데 성공했는지에 대한 여부를 저장하는 변수이다. 
* `struct semaphore exit_sema`: 
  * 부모-자식 프로세스 간 관계에서 자식 프로세스가 [`exit_`](#`exit_`)을 수행하면 부모 프로세스가 [`wait`](#`wait`)하여 자식 프로세스의 exit 정보를 수거해야 한다. 이러한 Process termination 과정의 Synchronization을 지원하기 위한 자료구조이다. 자세한 사용을 해당 System call handling function에서 서술했다.
  * Synchronization을 지원하기 위해 해당 Semaphore는 0으로 초기화된다.
* `struct semaphore load_sema`
  * 프로세스가 Load되기도 전에 실행되는 것을 방지하기 위한 Synchronization을 지원하기 위한 자료구조이다. [`exec`](#`exec`) System call handling function에서 자세히 서술했다.
  * 마찬가지로, Synchronization을 지원하기 위해 해당 Semaphore는 0으로 초기화된다.
* `struct list_elem child_pcb_elem`
* `struct list file_descriptor_list`
  * 해당 Process가 가지고 있는 File descriptor을 저장하는 리스트이다. File system의 관리와 구현에 사용하였다.
* `struct file *exe_file`
  * 해당 Process가 현재 실행중인 File에 대한 정보를 담고 있으며, Write on executable 상황을 방지하기 위해 정의해 사용했다.

### Process Termination Messages

Process termination messages는 `process.c`의 `process_trigger_wait`에서 명시적으로 `thread_current()->name`과 `exit_code` 를 명시적으로 출력한다. 구체적인 Process termination 과정은 System call의 `exit_`에 서술했다.

### Argument Passing

인자를 공백 단위로 Parsing하고, 이를 스택에 일정 Convention에 따라 쌓아올려 전달해야한다. 주요 역할별로 함수를 나누어 Parsing, Stack setting을 각각 `parse_args`, `push_args`에서 전담하여 수행한다.

#### `parse_args`

Command line은 Spacing*(Might be multi-spaced)* 된 인자를 공백 기준으로 나누어 각각의 인자를 함수의 인자로 주어진 `char**` array 에 저장한다.

반복문을 돌면서, `strtok_r`을 이용해 마지막으로 tokenize된 위치를 저장하면서 저장된 문자열이 `NULL`이 아닌 동안 공백 단위로 분리하였고, `argv` 배열의 `argc` 번째에 그 주소를 저장했다. 최종적으로 이 함수는 Parsing된 argument의 개수 (`argc`) 를 반환한다.

#### `push_args`

현재 Stack pointer에서 다음과 같은 구성 요소들을 **Address가 높은 순에서 낮은 순으로** stack pointer를 내려가면서 스택에 쓴다. 함수의 인자로 `int argc`, `char **argv`, `void **esp` 가 주어지며, `argc`와 `argv`의 정보를 이용해 `esp`가 referencing하는 stack에 인자를 전달한다.

1. `argv[argc - 1]` 부터 `argv[0]` 의 값을 역순으로 `strlcpy`를 사용해 stack에 작성한다. (`char` type을 복사한다)
   - `Sum of (strlen of each entries) + argc` byte만큼의 공간을 차지한다.
   - 과정 (3)에서 사용하기 위해, 해당 주소를 동적(`palloc_get_page(0)`)으로 할당한 `char **arg_addrs`에 저장한다.
2. 값을 쌓아주고 word align을 맞추기 위해 다음 argument가 4byte 단위에 위치하도록 stack pointer를 빼 준다.
3. 이후 `argv[argc - 1]` 부터 `argv[0]`의 값이 있는 주소를 역순으로 삽입한다. (`char*` type을 복사한다)
   - `sizeof(char*) * (argc)` byte만큼 스택의 공간을 차지한다. 
   - 과정 (1)에서 복사한 `arg_addrs`의 entry들에서 주소를 가져온다.
4. 이후 `argv[0]`이 시작하는 주소를 스택에 삽입한다. (`char**` type을 복사한다)
5. 인자의 개수 `argc`(`int` type)을 스택에 삽입한다.
6. 마지막으로 함수 실행 후 돌아갈 주소를 삽입하는데, 프로세스가 만들어 질 때의 스택은 이러한 컨벤션을 따르기 위해 Dummy address인 `NULL` 을 삽입했다.

최종적으로 할당받았던 `arg_addrs`를 `palloc_free_page`를 통해 Free해 준다.



### System Calls

`process.c`의 `syscall_init`에 의해 `int $0x30`으로 Syscall을 부를 수 있도록 `intr_register_int`를 통해 가장 처음으로 `syscall_handler`가 호출된다. 여기서, 우리는 각 Syscall의 구현을 `syscall_func`type의 Syscall handler 함수들을 배열에 저장해, Syscall ID를 통해 각 Syscall을 처리할 수 있도록 구현했다.

잘못된 Syscall ID를 부를 경우, `process_trigger_exit (-1)`을 사용해 해당 프로세스를 비정상 종료시킨다.

각 Syscall function에 전달하기 위한 인자들은 `pop_arg` Macro function을 이용해 스택으로부터 인자를 pop하여 저장하도록 구현했다.

* `syscall_func`는 `typedef int syscall_func (void *esp)`로 정의되었으며, 실제로 `int` 형의 반환 여부는 syscall function 마다 다르다.

#### User-process Manipulation

##### `halt`

해당 Syscall에서 요구하는 인자는 없다.

단순히 PintOS System를 Shutdown시킨다. 전원을 내리기 때문에 Return될 수 없는 함수이고, 이를 `NOT_REACHED ()`를 명시적으로 작성하였다.

##### `exit_`

해당 Syscall에서 요구하는 인자는 1개로, 종료 상태인 `int status`를 스택에서 pop한다.

`process_trigger_exit`을 호출하며, 해당 함수 내에서 프로세스가 점유했던 자원의 정리 및 부모 프로세스로의 반환 과정이 이뤄지게 된다. 동작은 다음과 같다.

* [Process Termination Messages](#Process Termination Messages)를 출력한다.
* 해당 Process가 점유했던 File의 File descriptor를 `process_cleanup_fd`를 통해 file을 닫는다.
  * 유효한 File descriptor이 가리키는 `NULL`아닌 파일을 닫는다. File system에서 전역적으로 공유하는 lock인 `fs_lock`을 `thread.c`에 선언하고, 해당 lock을 사용해 파일을 닫는 Operation에 대한 atomicity를 보장했다.
  * File descriptor를 리스트에서 빼고, File descriptor가 점유했던 메모리를 `palloc_free_page`를 사용해 할당 해제한다.
* 종료될 Process의 exit code를 세팅하고,  `exit_sema`를 `sema_up`한다. *(→ 해당 프로세스의 부모 프로세스에서 동일한 semaphore를 `sema_down`한다.)*
* 어떤 상황에서던지, 실행 중인 파일에 작성되는 것을 막기 위해 가장 마지막에 현재 실행 중인 파일을 닫는다. (`file_close (cur->pcb->exe_file)`)
* `thread_exit`를 사용해 종료한다.

이후 `process_trigger_exit`에는 도달할 수 없으며 (`NOT_REACHED ()`), 그 Caller인 `exit_`도 `NOT_REACHED ()`를 명시하여 실행될 수 없는 영역을 명시하여 구현했다.

##### `exec`

##### `wait`

#### File Manipuliation

##### `create`

##### `remove`

##### `open`

##### `filesize`

##### `read`

##### `write`

##### `seek`

##### `tell`

##### `close`

### Denying Writes to Executables

## Discussion

### Bug Fixes

