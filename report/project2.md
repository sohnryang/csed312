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

### File Descriptors

앞서 설명한 `file_descriptor_list`에 File descriptor를 직접 저장하지는 않으며, 해당 File descriptor와 관련된 요소들로 구조체를 정의하여 사용했다. 멤버들은 다음과 같다.

* File descriptor ID (`STDIN`과 `STDOUT`은 각각 `0`과 `1`로 미리 정해져 있다)
* File descriptor가 가지는 실제 파일의 포인터
* `file_descriptor_list`에 삽입이 가능한 `struct list_elem`
* `STDIN` 및 `STDOUT`인 경우를 구분하기 위한 boolean value

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

* 해당 Syscall에서 요구하는 인자는 없다.
* 반환되지 않는 Syscall Handler이다.

단순히 PintOS System를 Shutdown시킨다. 전원을 내리기 때문에 Return될 수 없는 함수이고, 이를 `NOT_REACHED ()`를 명시적으로 작성하였다.

##### `exit_`

* 해당 Syscall에서 요구하는 인자는 1개로, 종료 상태인 `int status`를 스택에서 pop한다.
* 반환되지 않는 Syscall Handler이다.

`process_trigger_exit`을 호출하며, 해당 함수 내에서 프로세스가 점유했던 자원의 정리 및 부모 프로세스로의 반환 과정이 이뤄지게 된다. 동작은 다음과 같다.

* [Process Termination Messages](#Process Termination Messages)를 출력한다.
* 해당 Process가 점유했던 File의 File descriptor를 `process_cleanup_fd`를 통해 file을 닫는다.
  * 유효한 File descriptor이 가리키는 `NULL`아닌 파일을 닫는다. File system에서 전역적으로 공유하는 lock인 `fs_lock`을 `thread.c`에 선언하고, 해당 lock을 사용해 파일을 닫는 Operation에 대한 atomicity를 보장했다.
  * File descriptor를 리스트에서 빼고, File descriptor가 점유했던 메모리를 `palloc_free_page`를 사용해 할당 해제한다.
* 종료될 Process의 exit code를 세팅하고,  `exit_sema`를 `sema_up`한다. *(→ 부모 프로세스의 `wait` Syscall에서 동일한 semaphore를 `sema_down`한다.)*
* 어떤 상황에서던지, 실행 중인 파일에 작성되는 것을 막기 위해 가장 마지막에 현재 실행 중인 파일을 닫는다. (`file_close (cur->pcb->exe_file)`)
* `thread_exit`를 사용해 종료한다.

이후 `process_trigger_exit`에는 도달할 수 없으며 (`NOT_REACHED ()`), 그 Caller인 `exit_`도 `NOT_REACHED ()`를 명시하여 실행될 수 없는 영역을 명시하여 구현했다.

##### `exec`

* 해당 Syscall에서 요구하는 인자는 1개로, `const char *filename`을 스택에서 pop한다. (해당 `filename` string을 바로 사용하지 않고, 이를 복사하여 사용한다.)
* 반환되는 값은 (성공적으로 자식 프로세스를 생성한 경우) 생성된 자식 프로세스의 PID / (실패한 경우) `-1`을 반환한다.

해당 Syscall은 여러 단계를 거쳐서 실행되며, 다음과 같은 Calling sequence를 거쳐 Command line으로부터 parse하여 프로그램을 프로세스로 실행하게 된다.

* `process_execute(filename_copy)`
  * 복사된 `filename`을 Parsing하여, 스레드의 이름을 설정하고 입력받은 Command line을 실행하는 `thread_create`를 호출한다.

* `thread_create (prog_name, PRI_DEFAULT, start_process, fn_copy)`
  * Kernel thread stack frame의 `function`과 그 인자를  `start_process`와 `fn_copy`로 설정한다.
  * 해당 함수 내에서는 User program에서 PCB를 할당받고, 원소들을 초기화한다.
    * Process ID를 설정한다.
    * Semaphore `exit_sema`와 `load_sema`를 0으로 설정한다(Synchronization).
    * 해당 Process가 소유하고 있는 파일들의 File descriptor list를 초기화한다.
  * 부모 프로세스와 자식 프로세스 간의 관계를 만들어 준다. 부모 프로세스(`thread_current`)의 `child_pcb_list`에 자식 프로세스의 `child_pcb_elem`을 삽입하도록 구현하여 관계를 세웠다.

* `start_process(fn_copy)`
  * 새로 자식 프로세스가 생성되고 CPU를 점유했을 때 가장 처음 실행되며, Command line을 parse하여 `argc`와 `argv`를 설정한다.
  * `load (argv[0], &if_.eip, &if_.esp)` 를 통해 `argv[0]` (프로그램 명)을 수행한다.
    * `load` 함수 내 마지막 단계에서 최종적으로 Load success를 판단한 경우에 해당 프로세스의 `load_sema`를 `sema_up`한다. *(→ `start_process` 종료 후 `exec` Syscall에서 해당 semaphore를 `sema_down`한다)*
    * 모종의 이유로 parse 또는 `load`에 실패한 경우 비정상 종료를 수행했다.

  * 받아온 인자를 최종적으로 Convention에 따라 Stack에 쌓아 올린다. (자세한 내용은 [Argument Passing](#Argument Passing)에서 서술했다.)
    * Stack에 모든 정보를 저장한 이후, 더 이상 사용하지 않을 `file_name`과 `argv`를 할당 해제하였다.

  * File Descriptor Table에 `0`과 `1`을 추가한다. 각각 `stdin`과 `stdout`이다.


단계를 거친 후에 더 이상 사용하지 않는 `filename_copy`를 할당 해제한다. 이후, Synchronization을 위해 `load`가 끝날 때 까지 대기하도록 자식 프로세스의 `load_sema`를 `sema_down`한다. (→ 이전 단계의 `load` 함수 내에서 해당 semaphore를 `sema_up`한다)

`filename`을 복사해서 사용하는 이유는 다음과 같다.

* `filename`은 `process_execute`의 인자로 들어가게 되고,  `thread_create`으로 `start_process`를 실행하는 스레드를 생성하게 된다. 최종적으로 `filename`이 `start_process`의 인자로 전달되었을 때, `void*` 로 전달되기 떄문에 Constantness를 보장하기 어렵다. 따라서 복사해서 해당 string을 사용해야만 한다.

최종적으로 `process_execute`의 반환은 Caller에게 (부모 프로세스) 자식의 PID를 반환한다. `process_execute`의 실행 이후 복사했던 `filename`을 할당 해제하고, 반환된 PID를 통해 자식 프로세스를 찾는다. 이후 자식의 load가 끝날 때까지 기다리며 (자식의 `load_sema`를 `sema_down`한다) 정상적으로 Load 되었는지 판별한다.

##### `wait`

* 해당 Syscall에서 요구하는 인자는 1개로, 자식 프로세스의 `int pid`를 스택에서 pop한다.
* 반환되는 값은 (유효한 자식 프로세스의 PID인 경우) 자식 Process의 exit code / (그렇지 않은 경우) `-1`을 반환한다.

자식 프로세스의 `exit_sema`를 `sema_down`한다. *(→ 자식 프로세스의 `exit` Syscall에서 동일한 semaphore를 `sema_up`한다.)*  - 자식 프로세스가 종료된 시점에 Synchronizate한다.

이후, 종료된 자식 프로세스의 PCB를 부모 프로세스의 자식 리스트에서 제거하고, 자식 프로세스의 PCB를 할당 해제하도록 구현했다.

#### File Manipuliation

여러 프로세스가 동시에 파일에 접근하게 된다면 파일과 관련된 Operation을 진행하면서 Undefined behavior가 발생할 수 있다. 따라서 모든 File system 관련 System Call이 호출될 때, 단 한 프로세스만이 파일에 접근해야 하는 Atomicity를 가진다. 이를 전역적으로 공유하는 `fs_lock` Lock을 사용해 구현하였다. 파일 시스템 관련 Syscall 호출에서  Critical Section은 `filesys_{operation}` 형태의 함수이며, 이에 접근 시 `thread_fs_lock_acquire`을 수행하고 Critical Section을 벗어나면서 `thread_fs_lock_release`를 수행한다. (이하 보고서에 `fs_lock`을 사용하지 않은 경우에만 명시하였으며, `fs_lock`에 대한 언급이 엇ㅂ는 `filesys_{operation}` 형태의 Syscall 호출은 모두 `fs_lock`을 사용하여 Atomicity를 관리한다.)

파일 관련 System Call에서 인자로 파일의 이름 (`const char*` type)을 전달 시, 앞서 설명한 이유와 동일한 이유로 string을 복사하여 전달하게 된다. 복사 받을 메모리를 할당하는 과정에서 문제가 발생하는 경우 `process_trigger_exit(-1)`을 명시하여 비정상적 종료로 처리하였다.

##### `create`

* 해당 Syscall에서 요구하는 인자는 2개로, 생성할 파일의 이름 `const char *filename`과 그 크기 `unsigned initial_size`를 순서대로 stack에서 pop한다.
* 반환되는 값은 파일 생성 성공 여부이다. `filename`을 복사하지 못했거나, `filename`이 empty string인 경우에도 생성 실패를 반환한다.

##### `remove`

* 해당 Syscall에서 요구하는 인자는 1개로, 삭제할 파일의 이름 `const char *filename`을 stack에서 pop한다.
* 반환되는 값은 파일 삭제 성공 여부이다. `filename`을 복사하지 못한 경우에도 삭제 실패를 반환한다.

##### `open`

* 해당 Syscall에서 요구하는 인자는 1개로, 열 파일의 이름 `const char *filename` 을 stack에서 pop한다.
* 반환되는 값은 (정상적으로 수행된 경우) 연 파일의 File descriptor / (`filesys_open`에서 실패하거나 메모리 할당에 실패한 경우) `-1`을 반환한다.



* File descriptor를 할당하기 위해 `process_get_first_free_fd_num` 함수를 정의하였다.
  * 현재 프로세스 PCB의 `file_descriptor_list`에 첫 번째로 비어 있는 File descriptor를 찾는다.
  * `struct file_descriptor*`에 할당할 FIle descriptor를 설정하고, `filesys_open`을 통해 연 파일을 지정해 준 뒤 file descriptor 순서에 따라`file_descriptor_list`에 삽입한다.


##### `filesize`

* 해당 Syscall에서 요구하는 인자는 1개로, filesize를 구할 파일의 File descriptor`int fd`를 stack에서 pop한다.
* 반환되는 값은 주어진 `fd`에 해당하는 파일의 filesize를 반환한다.



다음의 두 Syscall `read`와 `write`는 파일의 값을 메모리로 직접 읽어오거나 / 메모리의 값을 직접 파일로 쓰지 않고 버퍼를 거쳐 입출력을 진행하게 된다. 버퍼는 `palloc_get_page`를 통해 Syscall 함수 초입에 할당받으며, 모든 Operation이 끝난 뒤 Syscall 함수가 return하기 전에 `palloc_free_page`를 통해 할당 해제하였 다. 버퍼를 할당받지 못한 경우에는 Syscall에서 `0`을 반환하며 (아무 정보도 읽거나 쓰지 못함), 버퍼로부터 / 버퍼로의 메모리 복사가 실패한 경우 해당 프로세스는 비정상 종료된다.

##### `read`

* 해당 Syscall에서 요구하는 인자는 3개로, 읽을 파일의 File descriptor`int fd`와 저장할 주소 `void* buf`, 읽어올 크기 `unsigned length`를 순서대로 stack에서 pop한다.
* 반환되는 값은 실제로 읽은 길이를 반환한다.



* 비정상 종료로 오류를 처리하는 경우에  `STDOUT`에서 읽어오는 경우를 추가했다. (`screen_out`이 참인 경우)
* 이후 File descriptor에 해당하는 파일로부터 `read_buffer`를 거쳐서 읽어온다. (복사 실패 시 할당된 메모리를 할당 해제 후 비정상 종료)
  * `STDIN` (`keyboard_in`이 참인 경우): **`fs_lock`을 acquire/release하지 않고** `getc`를 통해 buffer address에 character를 복사한다.
  * 그 외의 경우 `file_read`를 통해 파일을 읽고, 읽은 크기를 비교하며 buffer address에 읽은 값을 복사한다.

##### `write`

* 해당 Syscall에서 요구하는 인자는 3개로, 작성할 파일의 File descriptor`int fd`와 작성할 데이터의 주소 `void* buf`,  작성할 크기 `unsigned length`를 순서대로 stack에서 pop한다.
* 반환되는 값은 실제로 작성한 길이를 반환한다.



* 비정상 종료로 오류를 처리하는 경우는 `STDIN`에서 쓰는 경우를 추가했다. (`keyboard_in`이 참인 경우)
* 이후 `write_buffer`에 작성할 내용을 복사하고, File descriptor에 해당하는 파일에 작성한다. (버퍼에 복사 실패 시 할당된 메모리를 할당 해제 후 비정상 종료)
  * `STDOUT` (`screen_out`이 참인 경우): **`fs_lock`을 acquire/release하지 않고** `putbuf`를 통해 표준 출력을 수행한다.
  * 그 외의 경우 `file_write`를 통해 파일을 작성한다.

##### `seek`

* 해당 Syscall에서 요구하는 인자는 2개로, 파일의 File descriptor`int fd`와 커서의 위치 `unsigned position`을 순서대로 stack에서 pop한다.
* 반환되는 값은 없다. (다만, 이를 `syscall_func`  type에 맞추기 위해 명시적으로 `0`을 반환하였다.)



* 비정상 종료로 오류를 처리하는 경우에 File descriptor의 해당 파일이 없는 경우(`STDIN`  및 `STDOUT`)의 `seek` Syscall 호출을 추가하였다.
* 이후 `file_seek`를 통해 파일의 커서를 주어진 `position`으로 옮긴다.

##### `tell`

* 해당 Syscall에서 요구하는 인자는 1개로, 파일의 File descriptor `int fd`를 stack에서 pop한다.

* 반환되는 값은 해당 파일의 커서 위치를 반환한다.

  

* 비정상 종료로 오류를 처리하는 경우에 File descriptor의 해당 파일이 없는 경우(`STDIN`  및 `STDOUT`)의 `tell` Syscall 호출을 추가하였다.
* 이후 `file_tell`을 통해 파일의 커서를 반환한다.

##### `close`

* 해당 Syscall에서 요구하는 인자는 1개로, 닫을 파일의 File descriptor `int fd`를 stack에서 pop한다.
* 반환되는 값은 없다. (다만, 이를 `syscall_func`  type에 맞추기 위해 명시적으로 `0`을 반환하였다.)
* `process_cleanup_fd`를 정의하여 호출하였다.
  * 해당 함수 내에서는 `file_close`를 사용하여 파일을 닫고, 프로세스 PCB의 `file_descriptor_list`에서  닫을 파일의 `elem`을 제거한다.
  * 이후 할당받았던 `struct file_descriptor`를 할당 해제한다.


### Denying Writes to Executables

`process.c`의 `load` 내에서  `filesys_open`을 통해 실행할 파일을 open하고,  현재 실행되는 프로세스의 PCB의 `exe_file` 필드에 열려 있는 파일을 추가하였다. Denying writes to Executables는 `file_deny_write`와 `file_allow_write`를 사용하여 구현하였으며, 여기서 `file_allow_write`는 `file_close` 내에서 진행되도록 미리 구현되어 있었다. 

* `file_deny_write`를 하는 시점은 다음과 같다.

  * `load` 내에서 성공적으로 파일을 load한 경우 (`success == true`) load된 파일에 `file_deny_write`를 사용하여 작성을 막는다. 

  *  `load` 과정에서 `fs_lock`은 `file_open`을 사용하기 위해 `thread_fs_lock_acquire`를 수행하고, `goto`에 따라 `file_deny_write`를 수행 여부가 결정되며, 최종적으로 label `done`에서 `thread_fs_lock_release`를 수행한다.

* `file_allow_write`는 `file_close`내에서 실행되며, 해당 함수가 실행되는 시점은 다음과 같다.
  * `load`내에서 파일 load에 실패한 경우 (`success == false`) `file_close`를 사용하여 파일을 닫는다.
  * 프로세스를 종료하는 과정에서 `process_trigger_exit`에서 PCB에 등록된 실행 중인 파일(`pcb->exe_file`) 에 대해`file_close`를 실행하도록 구현했다.

## Discussion

### File Descriptor Number Allocation

`open` system call을 통해 파일을 열 때, 사용자 프로세스는 열어놓은 파일을 참조하는데 사용할 file descriptor 번호를 부여받게 된다. 핀토스 문서에서는 이 번호의 부여 방식에 대해 명시적으로 규정해 놓지 않았지만, POSIX 표준에 따르면 프로세스가 사용하지 않는 file descriptor 중에서 가장 낮은 숫자를 부여한다고 설명되어 있다. 이 핀토스 구현에서는 POSIX 표준에 따르도록 구현하기 위해 `process_get_first_free_fd_num` 함수를 구현하여 가장 낮은 file descriptor 번호를 가져오도록 하였다.

해당 기능은 커밋 `6a733c3`에서 구현되었다.

### Out-of-Memory Handling

핀토스에서는 `palloc_get_page` 등의 함수로 메모리를 할당할 때, 남은 공간이 없다면 `NULL`을 반환한다. `NULL` 포인터를 참조하는 버그를 막고, 메모리가 부족한 상황에서도 OS가 안정적으로 동작하도록 하기 위해 `start_process`, `open`, `read`, `write` 등의 함수에서 메모리 할당이 실패한다면 사용자 프로세스를 종료시키는 등의 처리를 추가하였다.

해당 기능은 커밋 `82b0850`에서 구현되었다.

### User Memory Access

사용자 프로그램이 system call을 호출할 때, 커널에서는 system call 번호와 인자 등을 얻기 위해 사용자 메모리를 참조해야 한다. 핀토스 문서에서도 설명되었듯 이를 구현하는 데에는 크게 두 가지 방법이 있다. 포인터가 주어졌을 때 페이지 테이블을 참조해 사용자 메모리인지 확인하거나, `PHYS_BASE`보다 주소가 작은지만 본 다음 참조하고 page fault에서 추가적인 처리를 하는 것이다. 이 핀토스 구현에서는 후자의 방법을 택하여, 사용자 메모리에서 한 바이트를 읽거나 쓰는 함수를 다음과 같이 구현하였다.

```c
/* Read a byte from `usrc`. Return -1 if page fault occurred while copying. */
int
usermem_copy_byte_from_user (const uint8_t *usrc)
{
  int res;

  if (!is_valid_uptr (usrc))
    return -1;

  asm ("movl $1f, %0\n\t"
       "movzbl %1, %0\n\t"
       "1:"
       : "=&a"(res)
       : "m"(*usrc));
  return res;
}

/* Write a byte to `udst`. Return true if success, false if failure. */
bool
usermem_copy_byte_to_user (uint8_t *udst, uint8_t byte)
{
  int error_code;

  if (!is_valid_uptr (udst))
    return false;

  error_code = 0;
  asm ("movl $1f, %0\n\t"
       "movb %b2, %1\n\t"
       "1:"
       : "=&a"(error_code), "=m"(*udst)
       : "q"(byte));
  return error_code != -1;
}
```

함수에 적힌 inline assembly를 따라가 보면, `1:`로 표시된 레이블의 주소를 a register, 즉 eax 레지스터에 우선 적고, `movzbl`을 통한 메모리 접근을 수행하는 것을 볼 수 있다. 주어진 포인터에 문제가 없다면 함수는 정상적으로 반환돨 것이고, 비정상적인 주소가 주어졌다면 page fault handler가 실행된다.

```c
  /* Handle page faults caused by user memory access from kernel. */
  else if (fault_addr < PHYS_BASE)
    {
      f->eip = (void *)f->eax;
      f->eax = -1;
      return;
    }

```

Page fault handler에서는 위 코드와 같이 page fault가 일어난 주소가 `PHYS_BASE` 미만의 주소라면 사용자 프로세스의 메모리를 접근하는 함수에서 오류가 발생한 것으로 간주한다. Page fault는 `movzbl` 명령어에서 발생할 것이기 때문에 eip 레지스터는 eax 레지스터에 아직 저장되어 있는 `1:` 레이블의 주소로 대치되고, eax 레지스터에는 -1이 저장된 뒤 함수의 `1:` 레이블로 실행 흐름이 돌아오게 되어 `res` 혹은 `error_code` 변수에 -1이 저장되게 되고, 함수에서 메모리 참조 오류가 발생하였는지 쉽게 판정할 수 있다. 이와 같은 방법은 포인터 참조 전에 일일이 포인터를 검사하는 것보다 더 빠르기 때문에, 리눅스와 같은 OS에서도 구현되어 있는 것을 볼 수 있다.

한 바이트씩 읽어오는 함수만을 사용하는 것은 불편하기 때문에, `usermem.c` 파일에 `memcpy`, `strlcpy` 등과 유사한 함수를 작성하여 system call 관련 코드에서 활용하였다.
