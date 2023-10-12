# Project 2 Design Report

## Analysis of the current implementation

## Design plan

### Process Termination Messges

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

### Denying Writes to Executables
