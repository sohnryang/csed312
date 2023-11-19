# Project 3 Design Report

## Analysis of the current implementation

### Address Structure

현재 구현되어있는 32-bit Virtual Address의 Bit 구조는 다음과 같이 정의되어 있다. (`threads/pte.h`)

```
_3_________2_________1_________0
10987654321098765432109876543210
<--PDI---><--PTI---><--OFFSET-->
```

하위 12bit는 Offset이며, 실제 연속적인 Address에서 메모리가 존재해야 한다.

* Bitmasking을 통해 주소의 값을 계산하기 때문에, 12bit 단위가 존재하여 할당 및 참조에 기준을 두어야 한다. 이를 위해 `thread/vaddr.h`에서 `PGSIZE`는 `1 << 12` 로 정의되어 있다. 또한, 할당의 기준으로써 모든 Page 혹은 Frame의 시작 주소 하위 12bit는 `0x000`이어야만 한다.

상위 20bit는 Page에 대한 정보를 담고 있다. 구체적으로는 Page Directory와 Page Table로 구분할 수 있으며, 이들은 Virtual address인 Page로부터 Physical address인 Frame으로의 translation을 하려는 목적을 갖는다.

Address translation이 이루어 졌을 때 Offset 12bit는 보존되며, 상위 20bit의 Page 정보가 실제 Physical Frame의 주소 정보로 변환되어 사용되게 된다.

현재 Address translation은 매우 간단한 형태로 구현되어있으며, Virtual address와 Physical address를 One-to-one mapping하여 사용하고 있다 (`threads/vaddr.h`의 `vtop` 및 `ptov`에서 가상 주소에 `PHYS_BASE`를 더하고 빼서 translation을 진행한다.)

### Page directory & table Structure

앞서 설명한 Address의 구조에서, 상위 20bit를 다시 PDI와 PTI로 나눌 수 있으며, 각각은 Page directory index와 Page table index에 해당되는 값들이다. Address translation이 두 단계 (Page directory → Page table)에 거쳐 일어난다.

여기서 PTI 와 PDI는 각각 Page Table Index와 Page Directory Index를 의미하며, 이들 구조 또한 32-bit를 나누어 정의되어 있다. 상위 24bit의 역할은 PD와 PT에서 의미하는 바가 다르다.

* PD (Page Directory): 가장 큰 Address translation 단위이다.  10bit를 차지하여 `1 << 10 == 1024` 개의 Page table pointer로 구성이 되어 있다. (Physical Address Field가 특정 Page Table의 주소를 가리킨다.)
* PT (Page Table): Page directory에서 특정된 Page table으로, 10bit를 차지하여 `1 << 10 == 1024`개의 Page pointer로 구성되어있다. (Physical Address Field가 실제 Data나 Code가 담긴 영역의 주소를 가리킨다.)

Virtual memory의 관리를 위해 사용할 Flags는 아래의 그림과 같이 Bit masking을 통해 사용할 수 있도록 정의되어 있다. 하위 bit부터 나열했을 때 

* `P` bit는 Page directory / table이 실존하는지를 나타내는 bit이다. 해당 bit가 꺼질 경우 다른 flag는 무시된다.
  * 해당 bit에 접근하는 `userprog/pagedir.c` 내의 함수에는 `pagedir_destroy`, `pagedir_set_page`, `pagedir_get_page`, `pagedir_clear_page`가 있으며, 해당 Page가 실존하는지를 ASSERT 하거나, 페이지를 free하면서 실존 여부를 지우는데 사용하고 있다.

* `W` bit는 해당 Page directory / table의 읽기 및 쓰기 권한을 나타내는 bit이다. 해당 bit가 꺼진 경우 Physical address가 가리키는 대상이 Read-only 상태이며, 켜진 경우 Read 및 Write 모두 허용된다.
* `U` bit는 해당 Page directory / table에 User가 접근할 수 있는지를 의미하는 bit이다. 모든 Physical address에 Kernel이 접근할 수 있는 것에 비해, User의 접근은 해당 flag가 켜진 경우에만 허용된다.
* `A` bit는 해당 Page directory  / table이 최근에 접근되었는지를 의미하는 bit이다. 이를 이용해 Cache에 Page table을 올렸을 때 eviction을 하기 위한 metric으로 사용할 수 있다.
  * `userprog/pagedir.c`의 `pagedir_is_accessed`와 `pagedir_set_accessed`로 bitmask된 값을 읽거나 적용할 수 있다.

* `D` bit는 해당 Page table에 접근되어 값이 변경되었는지를 의미하는 bit이다. 해당 Bit가 변경된 Page table에 대해서만 디스크에 기록하도록 구현할 수 있으며, 이를 통해 느린 저장소로의 불필요한 접근을 막을 수 있다.
  * `userprog/pagedir.c`의 `pagedir_is_dirty`와 `pagedir_set_dirty`로 bitmask된 값을 읽거나 적용할 수 있다.

* `AVL` bit는 Kernel이 해당 Page directory / table을 사용할 수 있는지의 여부를 나타내는 bit다 (3개의 bit를 사용하여 bitmasking을 한다)

아래의 그림은 Page Directory Entry (Page Table을 가리키고, 해당 Page table의 정보를 표시)와 Page Table Entry (Page를 가리키고, 해당 Page의 정보를 표시)의 구조를 나타낸 도식으로, 상위 20bit Physical address와 하위 12bit Flag 중 명시한 flag를 표시하였다.

```
_3_________2_________1_________0
10987654321098765432109876543210
____________________AVL__DA__UWP
<----PHYS. ADDR----><---FLAGS-->
```

### Address Translation

가상 주소에서 물리 주소로의 전환은 다음과 같이 이루어진다.

* Page directory (31~22)
  * Page directory로부터 해당하는 PDI 번째 Entry를 참조한다.
  * Entry의 상위 20bit가 해당하는 Page table의 물리 주소이다.
    * 만약 하위 12bit의 `P` (Present) flag가 꺼져 있다면, 실존하는 page를 참조하고자 한 것이 아니기 때문에 Page Fault Exception이 발생하게 된다.
* Page table (21~12)
  * PDI로부터 찾아낸 Page Table에서 PTI 번째 Entry를 참조한다.
  * Entry의 상위 20bit가 해당하는 Page의 물리 주소이다.
    * 만약 하위 12bit의 `P` (Present) flag가 꺼져 있다면, 실존하는 page를 참조하고자 한 것이 아니기 때문에 Page Fault Exception이 발생하게 된다.
* Offset (11~0)
  * Page directory와 Page table을 거쳐 찾아낸 Page의 실제 주소의 하위 12bit는 0이다. (앞서 설명한 Page align) 여기에 Offset을 OR로 bitmasking하여 최종적인 Address translation을 마친다.



### Page allocation

Pintos 내에서 페이지를 할당하도록 노출된 함수는 `palloc ` 계열의 함수가 있었다. 실제로 thread나 userprog의 구현에서, thread 혹은 process의 정보를 저장하기 위해 해당 `palloc` 계열의 함수를 사용하여 할당했었다. `palloc` 계열의 함수는 내부에서 `palloc_get_multiple`과 `palloc_free_multiple`을 사용하여 실제 할당과 해제를 진행하게 된다. 해당 함수들은 다음의 사항을 고려하면서 구현되어져 있다.

* 권한
  * `palloc_get_multiple`의 인자로 주어진 `flags`에서 User page인지 Kernel page인지 구분한다. Kernel은 Kernel page와 모든 User의 User page에 접근할 수 있으나, User는 자신의 User page밖에 접근할 수 없다. 또한, Kernel의 page는 `kernel_pool`에서 할당받고 User의 page는 `user_pool`에서 할당받는 형태로, 두 할당 위치를 명시적으로 분리하였음을 확인하였다.
  * `palloc_free_multiple` 함수에서도 `page_from_pool` 함수를 내부에서 호출하고, page를 할당해 준 pool을 User와 Kernel을 구분하여 원래 pool로 반환해 주는 구조를 확인할 수 있었다.
* 단위
  * `palloc_get_multiple` 및 `palloc_free_multiple`에서 `memset`이나 `bitmap` 연산을 진행할 때 `PGSIZE`의 `page_cnt`배 만큼 메모리를 관리하는 코드를 확인할 수 있다.
* 구현 방식
  * `palloc_get_multiple` 및 `palloc_free_multiple`에서 `bitmap` 자료형을 통해 `page_cnt`개 만큼 연속적으로 할당해 줄 수 있는 page의 영역을 찾는다. 
* 원자성 (Atomicity)
  * `palloc_get_multiple`에서는 메모리를 할당해 줄 공간을 찾기 위해 `bitmap_scan_and_flip`을 사용하는데, 이를 Critical section으로 간주하고 해당 코드 수행 전후로 할당받을 pool (`user_pool` or `kernel_pool`)의 lock을 `lock_acquire(&pool->lock)`과 `lock_release(&pool->lock)`을 통해 동일한 메모리가 여러 번 할당되는 일이 없도록 구현되어 있다.
  * `palloc_free_multiple`에서는 메모리를 할당 해제할 때 `bitmap_set_multiple`을 사용하는데, 이 때는 `palloc_get_multiple`과 다르게 해당하는 pool의 lock을 acquire하지 않는다.

## Design plan

### Frame Table

#### Requirement

Physical memory는 page frame 단위로 관리되고, 각 page frame을 모아 관리할 자료 구조가 필요하다. Frame table은 OS가 사용 중인 각 page frame마다 해당되는 physical address와 메타데이터를 저장하고 관리한다. Frame table은 핀토스에서 eviction policy 등을 구현할 때 활용할 수 있다. Frame table의 목적은 사용자 프로세스가 사용하는 page frame을 관리하는 것이기 때문에, frame table이 관리하는 page는 user pool에서 할당되어야 한다.

#### Plans

우선 각각의 page frame의 정보를 저장하는 `frame` 구조체는 다음과 같이 정의된다.

```c
/* Physical frame. */
struct frame
{
  void *kpage; /* Address to a page from user pool. */

  bool is_stub;        /* Is this frame a stub frame? */
  bool is_swapped_out; /* Is this frame swapped out? */

  struct list mappings;  /* List of mappings. */
  struct list_elem elem; /* Element for frame table. */

  ...
};
```

여기서 `kpage`는 user pool에서 얻어 온 page 주소를 저장하고, `is_stub`, `is_swapped_out`은 각각 이 `frame`이 stub이거나 swap-out 상태인지를 나타낸다. 이 구현에서는 memory의 mapping이 처음 만들어질 때 그에 해당하는 `frame` 구조체가 하나 할당되는데, 이 구조체는 우선 `kpage`가 `NULL` 상태로 초기화되었다, demand paging 등에 의해 실제 메모리 공간을 받는다. 이때 아직 실제 메모리 공간을 할당받지 않은 상태를 stub 상태로 정의한다.

`mappings` 필드는 이 page frame에 mapping되어 있는 가상 주소들의 정보를 [File Memory Mapping](#File Memory Mapping)에서 설명할 `mmap_info` 구조체의 리스트로 저장한다. Demand paging이나 swapping 등에서는 page frame 단위로 이루어지기 때문에, page frame을 메모리로 읽어오거나 디스크에 저장할 때 이 `mappings` 리스트에 있는 `mmap_info` 정보에 따라 읽기/쓰기를 진행한다.

각 프로세스가 사용 중인 page frame 정보는 `elem` 필드를 사용하여 각 프로세스의 스레드마다 가지고 있는 `frames` 리스트에 저장되며, 이것이 frame table의 역할을 한다.

### Lazy Loading

#### Requirement

현재 핀토스의 구현에서는 사용자 프로그램이 로드될 때, 실행 파일의 코드와 데이터가 메모리에 직접 로드된다. 가상 메모리를 사용하여 실행 파일 로드 시점이 아니라, 로드할 데이터가 처음 사용될 때 메모리에 데이터를 가져오는 것을 구현하는 것이 이 요구 사항의 목표이다. 실행 파일의 코드와 데이터는 처음 사용 시점에서 로드하고, 프로그램이 사용할 스택 공간만 미리 메모리에 할당할 것이다. 처음 사용되는 시점에서의 할당을 page fault handler에서 수행해야 한다. Page fault handler의 동작은 잘못된 메모리 참조가 일어나는 상황과 같이 disk I/O가 필요하지 않은 상황과, 파일이나 swap 공간에서 데이터를 가져오는 등 disk I/O가 필요한 상황으로 분류할 수 있다. I/O가 필요하지 않은 코드 경로는 I/O가 필요한 코드 경로보다 먼저 실행되어야 한다.

#### Plans

사용자 프로그램의 실행 파일에서 데이터를 읽어오는 것은 일종의 read-only file mapping으로 간주할 수 있을 것이다. 프로세스의 실행 파일에서 데이터를 읽어오는 작업은 `load_segment` 함수에서 실행되는데, 이 함수에서 실행 파일을 읽는 대신 파일의 로드할 부분을 `PGSIZE` 단위로 잘라 각 부분에 대해 read-only memory mapping을 만들 수 있다. 이를 코드로 나타내면 다음과 같다.

```c
	pos = 0
	while (read_bytes > 0 || zero_bytes > 0)
    {
      /* Calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;

      if (!vmm_create_file_map (upage, file, writable, ofs + pos,
                                page_read_bytes))
        return false;
      pos += page_read_bytes;

      /* Advance. */
      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      upage += PGSIZE;
    }
```

여기서 `mmap_info` 구조체를 생성하여 프로세스 메모리 공간에 매핑하는 과정을 `vmm_create_file_map` 함수에서 수행한다.

Page fault handler에서는 not present 오류 상황에서, 다음과 같은 규칙으로 page fault의 원인을 식별한다.

1. 사용자 코드에서 PF가 발생한 경우: stack growth가 가능한 상황인지 확인하고, 그렇지 않다면 프로세스의 비정상적인 메모리 접근
2. 커널 코드에서 PF가 발생한 경우:
   1. System call을 실행 중인 경우: stack growth가 가능한 상황인지 확인하고, 그렇지 않다면 비정상적인 포인터가 system call에 주어진 상황
   2. System call 상황이 아닌 경우: disk에서 메모리로 데이터 로드

### Supplemental Page Table

#### Requirement

페이지 테이블에 저장되는 정보는 페이지의 가상 주소에 대응되는 물리 주소와, 페이지의 접근 권한 등 한정된 정보만이 저장된다. 핀토스와 같은 OS에서 lazy loading, swapping 등이 정상적으로 동작하기 위해서는 각 페이지에 대해 page table에 저장되는 것 외에 추가적인 데이터를 저장할 필요가 있다. 이러한 추가적인 정보를 저장하기 위해 supplemental page table을 사용한다. Page fault handler에서는 supplemental page table에 있는 정보를 통해 page fault가 발생한 주소에 어떤 데이터를 로드할지 결정한다.

#### Plans

핀토스의 사용자 프로그램이 사용하는 메모리는 파일에서 매핑된 file-mapped와 디스크의 파일과는 상관없이 동작하는 anonymous mapping이 존재한다. 각 페이지에는 한 가지 mapping만 존재하므로, 각 페이지를 mapping으로 관리할 수 있다. 이 핀토스 구현에서는 이 점을 이용하여 supplemental page table과 mapping table을 합치는 방식으로 구현할 것이다. 각 페이지의 데이터는 `mapping_info` 구조체에 저장하는 설계를 생각할 수 있다.

```c
/* Struct describing memory mapped object. */
struct mmap_info
{
  void *upage;       /* User page the file is mapped to. */
  struct file *file; /* Pointer to mapped file. Set to NULL if the mapping is
                        anonymous. */

  bool writable;        /* Whether the mapping is writable. */
  off_t offset;         /* Offset of mapped file. */
  uint32_t mapped_size; /* Size of mapped data. */

  struct list_elem elem; /* Element for linked list in frame. */
  struct frame *frame;   /* Pointer to frame object. */

  struct hash_elem map_elem; /* Element for mapping table. */

  struct list_elem chunk_elem; /* Element for chunks list. */
};
```

`upage` 필드는 mapping이 가지는 가상 주소를 저장한다. `frame` 필드는 mapping이 저장되는 프레임을 가리키고, 한편 `frame` 구조체의 `mappings` 리스트에서 `mmap_info` 구조체를 저장할 수 있도록 `elem` 필드를 사용하도록 한다. 이 `mmap_info` 구조체는 가상 주소를 통해 손쉽고 효율적으로 참조할 수 있도록 해시 테이블을 사용할 것이다. 프로세스가 가진 mapping의 `mmap_info` 구조체는 `hash_elem` 필드를 사용하여 프로세스의 스레드 구조체마다 가지고 있는 `mmaps` 해시 테이블으로 참조할 수 있다.

### Stack Growth

#### Requirement

핀토스의 기존 구현에서, 각 프로세스의 스택 크기는 1페이지로 고정되어 있다. 여기에서는 demand paging 기법을 활용하여 프로세스의 스택 공간이 필요에 따라 늘어나는 메커니즘을 구현한다.

#### Plans

Page fault handler를 수정하여 프로세스의 스택 메모리 영역에서 페이지 폴트가 일어나는 상황에 메모리를 할당받는 방법으로 구현할 수 있다. x86의 `push` instruction은 stack pointer의 값을 바꾸기 전에 permission check를 수행하기 때문에, 한번에 `n` 바이트를 push하는 상황이라면 esp 레지스터 아래 `n` 바이트에서 page fault가 일어날 수 있다. 가장 많은 데이터를 push하는 명령어, `pusha` 명령어는 32바이트를 push하기 때문에, esp 레지스터 아래 최대 32바이트에서 page fault가 일어나는 상황에서 스택 공간을 새로 할당받도록 구현하면 될 것이다. 또한, system call boundary에서 stack access가 처음 일어나는 경우에는 커널 코드가 실행되는 도중에 page fault가 발생하므로 `intr_frame` 구조체의 필드를 읽는 방법으로는 원하는 esp 값을 읽어올 수 없다. 이 경우에는 사용자 코드가 실행되던 중의 esp 값을 따로 저장할 필요가 있다. 또한, 다른 OS와 같이 최대로 스택이 자랄 수 있는 크기도 제한할 필요가 있다.

### File Memory Mapping

#### Requirement

File descriptor가 주어졌을 때, 파일을 사용자 프로세스의 메모리로 mapping하여 메모리 읽기/쓰기를 통해 file I/O를 수행하는 것을 memory mapped file이라고 한다. 파일을 mapping하기 위해 사용하는 system call은 다음과 같다.

```c
mapid_t mmap(int fd, void *addr);
void munmap(mapid_t mapping);
```

`mmap`은 `fd` file descriptor로 열린 파일을 `addr` 주소에 mapping한다. 만약 파일이 화면이나 키보드에 연결되거나, 열려 있지 않은 파일을 mapping하려고 시도하거나, `addr`이 page-aligned된 빈 공간을 가리키지 않는 등 비정상적인 mapping을 만드려는 상황에서는 -1을 반환하고, 정상적으로 mapping이 이루어질 경우에는 `open` system call이 file descriptor를 반환했던 것처럼 mapping id를 반환한다. 이 mapping id는 `munmap` 함수에서 mapping을 해제할 때 사용한다. 여러 프로세스가 같은 파일을 mapping했을 때, 두 프로세스에서 일관된 데이터를 읽는 것을 보장할 필요는 없다.

#### Plans

앞서 [Supplemental Page Table](#Supplemental Page Table)에서 설명한 것과 같이 `mmap_info` 구조체에 memory mapping에 대한 정보를 저장한다. `file` 포인터에는 현재 메모리에 mapping되어 있는 파일을 지정하고, 만약 mapping이 anonymous mapping이라면, 즉 파일을 mapping한 것이 아니라면 `NULL`을 저장한다. `writable` 필드는 현재 mapping에 쓰기가 가능한지, `offset` 필드는 mapping된 파일의 오프셋, `mapped_size`는 mapping된 데이터의 크기를 나타낸다. `offset`, `mapped_size` 필드는 anonymous mapping의 경우에는 무시된다. `mmap_info` 구조체는 한 페이지 단위로 분할하여 파일을 mapping하기 때문에, `mmap`, `munmap` system call에서 한 페이지 단위로 mapping을 관리하는 데에는 불편함이 있을 것이다. 이를 해소하기 위하여 `mmap` system call으로 만들어진 mapping들은 `mmap_user_block` 구조체를 사용하여 한 파일에 대한 mapping 모두를 모을 수 있도록 설계하였다.

```c
/* Struct describing whole file mapped to user memory. Intended for collecting
   memory mappings created by mmap system call. */
struct mmap_user_block
{
  mapid_t id;        /* Map ID of mapping. */
  struct file *file; /* File that is mapped to memory. */

  struct list chunks;    /* List of `mmap_info`s. */
  struct list_elem elem; /* Element for mmap_blocks list. */
};
```

`mmap_user_block` 구조체에서는 한 개의 `mmap` system call으로 만들어진 `id`번 mapping 정보를 저장한다. 이 구조체의 `chunks` 리스트에 페이지 단위로 분할된 `mmap_info`에 담긴 mapping 정보가 리스트 형태로 저장된다. `mmap_user_block` 구조체는 `elem` 필드를 사용하여 프로세스가 실행 중인 스레드의 `mmap_blocks` 리스트에서 접근 가능하도록 설계하였다.

### Swap Table

### On-Process Termination

