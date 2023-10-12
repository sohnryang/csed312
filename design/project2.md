# Project 2 Design Report

## Analysis of the current implementation

## Design plan

### Process Termination Messges

`exit` system call이나 비정상적인 system call 호출이나 메모리 참조 등에 의해 프로세스를 종료해야 할 경우, exit code를 설정하고 `thread_exit`을 실행하는 함수룰 구현할 것이다. `process name: exit(status)` 메시지는 이 함수에서 `thread_exit`을 실행하기 전 `printf`를 통해 출력하면 될 것이다.

### Argument Passing

### System Call

#### Interface and Security Concerns

핀토스에서 system call을 호출할 때에는 스택에 system call 번호와 system call의 인자들을 push한 다음 `int 0x80` 명령어를 실행하는 과정을 거치게 된다. 핀토스의 기본 구현에서, 이미 인터럽트 핸들러가 세팅되어 있기 때문에 `int 0x80` 명령어를 실행하면 `syscall_handler` 함수가 실행된다. 기본 구현에서는 `syscall_handler`에서 아무런 동작 없이 스레드를 종료시키지만, 실제 구현에서는 여기에서 system call 번호를 읽어 주어진 system call에 맞는 동작 (파일시스템 접근, 자식 프로세스 생성 등)을 수행해야 할 것이다.

System call은 OS와 사용자 프로그램이 상호작용하는 통로이고, 기본적으로 OS는 사용자 프로그램을 100% 불신해야 하기 때문에 system call 호출과 함께 주어지는 데이터에 대한 검증이 필요할 것이다. 예를 들어, 악의적인 사용자 프로그램은 `esp`를 변조하여 system call 호출 도중에 엉뚱한 메모리 주소를 참조하게 만들거나, 포인터를 넘겨줄 때 `NULL`이나 커널 메모리 공간의 주소 등을 넘겨 비정상적인 동작을 있으킬 수 있다. 이 구현에서는 page fault handler의 구현을 바꾸어 user space pointer를 역참조할 것이다. 이를 위해서 다음과 비슷한 코드를 사용할 예정이다. (아래 코드는 핀토스 문서의 코드를 수정한 것이다.)

```c
int
checked_copy_byte_from_user (const uint8_t *usrc)
{
  int res;

  if (!is_valid_user_ptr (usrc))
    return -1;

  asm ("movl $1f, %0\n\t"
       "movzbl %1, %0\n\t"
       "1:"
       : "=&a"(res)
       : "m"(*usrc));
  return res;
}
```

위 코드의 작동 과정은 다음과 같다. 우선 주어진 user space pointer `usrc`가 실제로 user space 영역에 있는 데이터를 가리키는지 `is_valid_user_ptr` 함수로 확인한다. 이어지는 inline assembly에서는 우선 `1` 레이블에 해당되는 주소를 `eax`에 로드하고, 그 다음 `usrc`가 가리키는 주소를 `eax` 레지스터에 로드한다. 만약 `usrc`가 `NULL` 등 정상적인 주소를 가리키지 않는 포인터라면 page fault가 발생하게 된다. page fault handler에서는 우선 context 정보를 통해 page fault가 일어난 위치를 판정하고, 만약 위 코드에서 page fault가 발생한 것이면 `eip`를 `eax`로, `eax`를 -1로 설정하여 위 코드로 다시 돌아간 다음, 메모리 참조가 실패했을 때 -1을 반환하도록 할 수 있다. 이렇게 user space에서 바이트 하나를 복사하는 함수를 일종의 primitive로 사용하여 user space에서 데이터를 복사해 오는 `memcpy`등의 함수를 구현할 수 있을 것이다.

#### Process Control Block

사용자 프로세스는 system call을 통해 파일시스템 접근, 자식 프로세스 관리 등을 수행하게 된다. 이때 프로세스가 연 파일, 자식 프로세스 등의 상태를 기억해 두고 시스템 콜에서 관리할 필요가 있다. 이를 위해서는 process control block 구조체를 만들어 현재 프로세스의 exit code, load 성공 여부 등을 저장해 둘 것이다. Process control block은 스레드 구조체와 lifetime이 다르기 때문에 필드에 직접 저장하는 대신 별도로 할당하여 포인터로 참조한다.

프로세스의 exit code, load 성공 여부를 저장하는 필드는 다른 프로세스와 공유되기 때문에 synchronization이 필요하다. 여기에서는 프로세스 간의 synchronization을 위해 exit code, load 성공 여부의 결과가 결정되었을 때를 다른 프로세스에게 알리기 위해 세마포어를 사용한다.

#### System Call Implementation

##### Halt

Halt system call의 경우에는 매우 단순하다. `shutdown_power_off`를 호출하면 핀토스가 종료할 것이다.

##### Child Process

`exec` system call이 실행되면 child process의 process control block을 생성하고, 현재 프로세스의 child process 리스트에 새로 생성된 프로세스를 추가한다. 프로세스의 load가 실패했을 경우의 처리도 필요하기 때문에, process control block의 load 성공 여부를 저장하는 세마포어를 사용하여 load가 끝날 때까지 기다린 다음, load 성공 여부에 맞게 child process의 tid를 리턴한다.

`exit` system call이 실행되면 process control block의 exit code를 쓰고, `wait` system call에서 기다리는 중인 parent process에게 종료를 알리기 위해 semaphore를 사용하여 signalling 한 다음, 프로세스가 사용 중인 자원을 처리한 뒤 종료한다.

`wait` system call은 child process의 리스트에서 주어진 pid를 가지는 프로세스를 찾은 다음, 찾은 프로세스의 process control block의 exit code가 정해질 때까지 세마포어를 이용하여 기다린다. Child process가 종료함이 확인되면 자식 프로세스의 process control block을 할당 해제하고, exit code를 반환한다.

중요한 점은 process control block의 할당 해제 시점은 프로세스가 종료할 때가 아니라, 부모 프로세스에서 자식 프로세스에 대해 `wait` system call을 호출할 때라는 것이다. 따라서 process control block의 lifetime은 `thread` 구조체보다 더 길고, 이 때문에 process control block은 `thread` 구조체의 필드로 저장되는 것이 아니라 별도로 할당될 필요가 있다.

##### File System

핀토스의 파일 시스템은 동시성을 염두에 두고 설계된 것이 아니기 때문에, 파일 시스템 관련 코드는 모두 critical section으로 간주하고 lock을 통해 접근을 통제해야 한다.

`create` system call은 파일 시스템에 주어진 경로에 따라 새로운 파일을 생성한다.

`remove` system call은 파일 시스템에서 주어진 경로의 파일을 삭제한다.

`open` system call은 파일 시스템에서 주어진 경로의 파일을 `filesys_open` 함수로 열고, process control block에 열린 파일의 descriptor를 추가한다. 파일을 여는 것이 성공하였다면 열린 파일의 descriptor 번호를 반환하고, 여는 데 실패하였다면 -1을 반환한다.

`filesize` system call은 file descriptor 번호가 주어졌을 때 열린 파일의 파일 크기를 계산하여 반환한다. 만약 standard output, standard input에 해당하는 파일의 크기를 구하려고 하거나, 주어진 file descriptor에 해당되는 열린 파일이 없다면 프로세스를 비정상 종료시킨다.

`read` system call은 file descriptor 번호와 읽은 데이터를 저장할 버퍼, 읽을 데이터의 크기가 주어졌을 때 지정된 길이만큼의 데이터를 버퍼에 읽어들인다. 이때 사용자가 악의적인 버퍼 주소를 전달할 수 있기 때문에, 버퍼를 할당한 다음 고정된 크기만큼 나누어 읽어들이고, 주어진 주소에 데이터를 에러 검사가 추가된 함수를 사용하여 복사하는 방법으로 구현할 것이다. 키보드에서 읽어 들여야 하는 경우에는 `input_getc` 함수를 사용하고, 콘솔 출력이나 비정상적인 file descriptor에서 읽는 것을 시도할 때에는 프로세스를 비정상 종료시킨다.

`write` system call은 file descriptor 번호와 쓸 데이터를 저장할 버퍼, 쓸 데이터의 크기가 주어졌을 때 지정된 길이만큼의 데이터를 쓴다. 앞서 설명한 `read` system call과 고정된 길이의 버퍼를 일단 커널 메모리에 할당한 다음, 커널 버퍼 크기에 맞게 읽어들여 나누어 실제 write를 수행할 것이다. 콘솔에 출력할 때에는 `putbuf` 함수를 사용하고, 콘솔 입력이나 비정상적인 file descriptor에 출력하는 것을 시도할 때는 프로세스를 비정상 종료시킨다.

`seek` system call은 file descriptor 번호와 새로운 위치를 받고, 열린 파일의 read/write가 일어나는 위치를 변경한다. 콘솔 입력, 출력이나 비정상적인 file descriptor 번호를 받았다면 프로세스를 비정상 종료시킨다.

`tell` system call은 주어진 file descriptor 번호에 해당하는 파일에서 read/write가 일어나는 위치를 반환한다. 앞서 설명한 `seek` system call과 마찬가지로 콘솔 입력, 출력이나 비정상적인 file descriptor 번호를 받았다면 프로세스를 비정상 종료시킨다.

`close` system call은 현재 열린 파일의 file descriptor 번호를 받아 파일을 닫는다. 비정상적인 file descriptor 번호를 받았다면 프로세스를 비정상 종료시킨다.

### Denying Writes to Executables
