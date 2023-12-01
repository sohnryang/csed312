# Project 3 Final Report

## Solution

### Introducing Virtual Memory on PintOS

#### New structures

이번 프로젝트에서 구현한 Virtual Memory는 프로세스 별로 서로 다른 Virtual Address에서 Physical Address로의 Translation을 가져야 한다.  `threads/thread.h`에서 각 프로세스가 가져야 할 Frame Table, Mapping Table을 정의하였다. 해당 Table들을 구현하기 위해 `list` 자료구조를 사용했다.

```c
#ifdef VM
	struct list frames;
	struct hash mmaps;
	struct list mmap_blocks;
	void *esp_before_syscall;
#endif
```

프로세스를 생성하고 Load하는 과정에서 Virtual Memory System의 Initialization을 `vmm_init`함수를  통해 수행하도록 구현했다. 기존의 Process Start 과정과 동일하게, Virtual Memory System의 초기화에 실패한 경우 해당 프로세스를 비정상 종료시키게 된다.

```c
#ifdef VM
  success = vmm_init ();
  if (!success)
    {
      palloc_free_page (file_name);
      palloc_free_page (argv);
      thread_exit ();
    }
#endif
```

`vmm_init` 함수는 `vm/vmm.c`에 존재하며, 앞서 정의한 Frame 및 Mapping List를 초기화한다. 또한,  `hash_init`을 통해 현재 프로세스의 Memory Translation을 관리하기 위한 Mapping Hash Table을 할당한다.

```c
bool
vmm_init (void)
{
  struct thread *cur;

  cur = thread_current ();

  list_init (&cur->frames);
  list_init (&cur->mmap_blocks);
  return hash_init (&cur->mmaps, mmap_info_hash, mmap_info_less, NULL);
}
```

##### `frame`

Process의 `struct list frames`와 관계된 구조체이다. `frame->elem`을 `thread->frames`에 삽입하여 프로세스에 소속된 `frame`을 관리하며, 멤버로는 다음이 있다.

* `void *kpage`
  * 할당받은 Physical Memory를 향한 Pointer이다.
* `bool is_stub` 
* `bool is_swapped_out` / `block_sector_t swap_sector`
  * Page Swapping이 일어난 경우를 추적하기 위해 존재하는 멤버들로, 해당 Frame이 Swapped-out 되었는지를 저장하고, Swapped-out이 일어난 경우 Back storage device의 어떤 Block에 존재하는지 기록한다. (Physical memory에 존재하는 경우 `swap_sector`를 `-1`로 유지한다.)
* `struct list mappings` 
  * 해당 Physical Memory의 Physical Address와, User가 접근하는 Virtual Address의 Translation을 기록하기 위한 구조체인 `mmap_info`들을 삽입한다.
* `struct list_elem elem` / `struct list_elem global_elem`
  * Process별로 `thread->frames`에 삽입될 `elem`과, 현재 Physical Memory에 올라와 있는 모든 Frame을 관리하기 위해 전역적으로 정의된 `struct list active_frames`에 삽입될 원소 `global_elem`이다.

##### `mmap_info`

Frame의 `struct list mappings`와 관계된 구조체이다. `mmap_info->elem`을 `frame->mappings`에 삽입하여 Frame의 Memory Mapping을 나타내고, 프로세스 별로 Memory mapping의 중복을 없애고 관리하기 위해 `mmap_info->map_elem`을 `thread->mmaps`에 삽입한다. 멤버는 다음과 같다.

* `void upage`
  * User가 접근하는 Virtual Address의 Offset 부분을 제외한, Virtual Page의 Address이다.
* `struct hash_elem map_elem`
  * Virtual Address에서 Physical Address로의 Translation을 Hash map을 통해 구현했다. 해당 Hash map은 각 Process 별로 존재 (`thread->mmaps`)하며, 각자 고유한 Translation을 중복 없이 관리하기 위하여 정의하였다.
* `struct file *file` / `bool writable` / `bool exe_mapping` / `off_t offset` / `uint32_t mapped_size`
  * Virtual Address에서 Physical Address로 Translation 시 Frame이 가지는 정보를 다루고 있는 Field들이며, Anonymous Page와 File-Mapped Page에 따라 초기화 방식 및 함수를 달리 하여 구현했다.
* `struct list_elem elem` / `struct frame *frame`
  * `frame->mappings`에 삽입하기 위한 Element와, 역으로 Element가 자신이 삽입된 Frame을 찾기 위해 가지고 있는 Frame pointer이다.
* `struct list_elem chunk_elem`
  * System Call에 의해 생성된 `mmap_usr_block`의 `mmap_usr_block->chunks`에 삽입하기 위한 Element이다.

##### `mmap_usr_block`

PintOS Project 2에서 OS의 기본적인 System call과 File System의 System call을 구현하였다. Project 3에선 Memory를 관리하기 위한 System Call인 `mmap_` 과 `munmap_`을 구현하였다. System call을 통해 할당된 Memory는 따로 `mmap_usr_block`이라는 구조체로 관리했으며, 해당 구조체는 `thread->mmap_blocks`에 `mmap_usr_block->elem`을 삽입함으로써 관계를 가진다. 멤버는 다음과 같다.

* `mapid_t id`
  * Process별로 정의된 `struct list mmap_blocks`  에 해당 `mmap_usr_block`을 삽입할 때, 다른 Block들과 구분하기 위한 ID이다.
* `struct file* file`
  * User가 `mmap_` System call의 인자로 넘긴 파일이다.
* `struct list chunks`
  * `mmap_usr_block`에 해당하는 Address Transition을 관리하기 위한 `struct mmap_info`를 저장하기 위한 자료구조이다.
* `struct list_elem elem`
  * `frame->mmap_blocks`에 삽입하기 위한 Elements이다.

#### New System Calls

##### `mmap_`

2개의 인자를 받는 System Call로, 각각 File mapping을 위한 File descriptor `int fd`와 Mapping을 수행할 Virtual Address `void* addr`이다. 다음과 같이 System Call을 Handle하였다.

* `mmap_usr_block`을 할당받고, 해당 `mmap_usr_block`에서 사용할 `map_id`를 결정한다.
* (필요 시) `fd`에 해당하는 File 을 다시 열어서 Valid한 File Descriptor를 보장받은 상태에서 `mmap_init_user_block (block, id, file_reopened)`로 할당받은 Physical Memory에 File 정보를 등록한다.
* `vmm_setup_user_block`으로 `addr`의 주소로 Address Translation이 이루어지도록 `mmap_info` 를 설정하고,  File map을 생성한다.
* 현재 프로세스의 `thread->mmap_blocks`에 Memory Map을 수행한 `mmap_usr_block`의 `mmap_usr_block->elem`을 할당받은 `map_id`의 순서에 따라 삽입한다.

##### `munmap_`

`mmap_` System Call을 통해 할당받은 `mmap_usr_block`을 정리한다. 인자로 `mmap_usr_block` 의 Identificator인 `mapid_t id`이 주어진다. 다음과 같이 System Call을 Handle하였다.

* `vmm_get_mmap_user_block (id)`를 통해 Memory Map에 해당하는 User block을 찾는다. 존재하지 않는 경우 `NULL`을 반환한다

* 유효한 User Block이 주어진 경우, `vmm_cleanup_user_block`을 호출하여 해당 Block을 Unmap한다.

### Frame Table

Process마다 Frame table이 있고, 이를 `threads/thread.h`에서 `struct thread`의 멤버로`struct list frames`를 도입하였다. 프로세스에 할당된 프레임을 관리하기 위한 구조체로, `frame`의 구조는 다음과 같다.

```c
struct frame
{
  void *kpage; /* Address to a page from user pool. */

  bool is_stub;        /* Is this frame a stub frame? */
  bool is_swapped_out; /* Is this frame swapped out? */

  struct list mappings;  /* List of mappings. */
  struct list_elem elem; /* Element for frame table. */

  struct list_elem global_elem; /* Element for global frame list. */
  block_sector_t swap_sector;   /* Sector number of saved space. */
};

```

#### Initialization

해당 `struct frame` 은 `vm/frame.c`의 `frame_init`에서 초기화된다.

* User page `kpage` 를 `NULL`로 초기화한다. 해당 `kpage` 에 해당하는 Physical Address가 `frame`이다.
* Process를 복사 시 `stub`을 통해 함수를 호출하게 되는데, 이 Stub Frame을 생성하기 위해  `is_stub`을 `true`로 초기화한다.
* Swap 여부를 초기화한다. 생성 시 Frame이 Physical Memory에 올라가게 되기 때문에 `is_swapped_out`을 `false`로, `swap_sector`를 `-1`로 초기화한다.
* `frame`의 Physical Address로의 Mapping은 `struct list`를 통해 구현하였으며, 이 list를 초기화한다.

`frame_init`은 Frame이 새로 필요할 경우  `frame`을 할당할 때 마다 초기화되도록 구현하였다. 따라서 불릴 때의 함수 Call Stack을 추적하면 다음과 같이 File을 Memory-map하기 위해 / Anonymous Page를 만들 때 마다 호출이 된다.

> `frame_init` 
>
> ​	→ `vmm_map_to_new_frame` 
>
> ​		→ `vmm_create_anonymous` 
>
> ​		→ `vmm_create_file_map`

### Lazy Loading

### Supplemental Page Table

### Stack Growth

처음에 Process가 시작할 때 Stack은 `userprog/process.c`의 `load` 함수에서 `if (!setup_stack (esp))`와 같은 형태로 Stack을 생성했었다. Project 3에서 Virtual Memory를 도입하면서, 해당 `setup_stack` 함수를 `vmm_create_anonymous`로 Stack 영역을 생성하였다.

기존의 `userprog/exception.c`에 있는 Page Fault Handler를 수정해서 Stack이 자랄 때 마다 새로운 Page를 할당하도록 구현한다. Page를 할당해야 하는 경우의 조건을 만족시키는 경우에만 할당하며 그 경우는 다음과 같다. Address에 해당하는 Stack 영역이 Physical Memory 상에 존재하지 않는 상태에서 호출할 경우 해당 Stack 영역을 위한 Page를 할당 시도하고, 재실행을 해야 한다. 순전히 Invalid한 Virtual Address에 접근하는 경우를 제외해야 한다.

해당 Address의 Stack 영역이 Physical Memory에 존재하지 않는 경우는 다음의 두 가지 경우가 있을 것이다.

* 한 번도 할당되지 않은 영역이었던 경우 - `vmm_grow_stack` in `vm/vmm.c`

  해당 오류는 User 및 Kernel 양 측에서 발생 가능한 오류이다.

  * User의 경우 프로세스를 실행하면서 Stack에 Push하는 과정에서 Page를 넘기게 되는 경우 발생할 수  있다. 해당 Interrupt frame의 `sp` register를 관찰하여 Invalid한지 판단한다.
  * Kernel의 경우 System Call을 호출하고, System Call 내부에서 Page를 넘기게 되는 경우 발생할 수 있다. System Call이 호출되기 직전의 `sp` register를 관찰하여 Invalid한지 판단한다.

  이 때 Exception을 Handle하기 위해, 정상적인 Memory Access의 판단과 그 때의 Handling은 다음과 같이 진행할 수 있다. 

  * Invalid한 Virtual Address에 접근하는 경우에는 미리 Invalid임을 반환한다. 이 경우엔 Virtual Address로 Kernel Stack에 접근하는 경우와 기존의 Stack pointer보다 일정 거리 이상 떨어진 Virtual Address를 참조하는 경우가 있다. 해당 조건을 `vmm_grow_stack`에서 Stack을 자랄 수 없는 경우로 반환하였다.
  * 그 외의 경우는 정말 Page가 할당되지 않아 발생한 Page Fault Exception이기에, 해당 Exception이 불리게 된 Address의 Page를 생성한다.  `vmm_grow_stack`에서 `vmm_create_anonymous (pg_round_down (fault_addr), true)`로 Fault Address에 해당하는 Page를 생성했다.

* 이미 할당 되었으나 Swap당해 Physical Memory에 없는 경우 - `vmm_handle_not_present` in `vm/vmm.c`
  
  * 이 경우 Disk에서 해당 Address를 포함하는 Page를 Swap-in 하여 Physical Memory에 올린다. 이 과정에서 다른 Frame이 Evict당할 수 있다. 자세한 내용은 [Swap Table](#Swap-Table)에서 서술하였다.

양쪽의 경우 모두 Physical Memory에 다시 Frame을 올리고, Page Fault Exception이 발생한 Instruction을 다시 실행한다는 공통점이 있다.

### File Memory Mapping

### Swap Table

#### Data Structure

Swap을 구현하기 위해 정의한 Data Structure는 다음과 같다.

```c
static bool swap_present;
static struct lock swap_lock;
static struct list active_frames;
static struct block *swap_block_dev;
static struct bitmap *swap_block_map;
static struct list_elem *clock_hand;
```

* `static struct lock swap_lock`

  여러 프로세스에서 동시에 Page Swap을 하는 경우가 존재할 수 있으며, 정상적인 Swapping의 보장을 위하여 Swap하는 과정을 Critical Section으로 두고, 각 프로세스 사이의 Synchronization이 필요하다.

* `static struct list active_frames`

  현재 Physical Memory에 올라가 있는 Frame을 기록하기 위한 `list`이다.

* `static struct block *swap_block_dev`

  Frame을 Swap하여 Backing Store 역할을 하는 저장소의 Pointer이다.

* `static struct bitmap *swap_block_map`

  `swap_block_dev`의 block size 만큼의 bit를 가지도록 초기화되는 Bitmap이다. Bitmap을 사용하여 현재 Swappable한 Block section을 Abstract하게 관리할 수 있으며, Disk의 상황을 Swap 시 마다 Query하는 구현마다 Overhead가 작다. Swap-in 하여 Physical Memory으로 읽어들이면서 해당하는 Bit를 0으로 설정하고, Swap-out 하여 Swap Device에 Block을 작성하는 경우 해당하는 Bit를 1로 설정한다.

* `static struct list_elem *clock_hand` 

  `active_list`에서 현재 Evict당할 Page를 찾기 위해 Clock Algorithm을 사용하도록 구현했다. 해당 `list_elem`은 Clock Algorithm 사용 시에 있어서 필요한 Clock hand이다.

Back storage에 접근하여 Read/Write를 시도 시, 접근 단위는 Block으로 이루어진다. 반면, Virtual Memory System에서는 Memory를 Page 단위로 관리한다. 따라서, Swap-in / Swap-out 시 각 단위 간의 크기 변환이 적절히 이루어져야 하며, 이 때 Page와 Block 간의 비례상수를 `vm/swap.c`에서 `#define SECTORS_PER_PAGE (PGSIZE / BLOCK_SECTOR_SIZE)`로 정의하여 사용하고 있다.

#### Initialization

Swap table은 `threads/init.c`에서 `swap_init` 함수를 호출해 초기화한다. 해당 함수는 `vm/swap.c`에 정의하였다. 정의했던 `lock`과 `list`의 초기화를 진행하며, `swap_block_dev`를 `BLOCK_SWAP`역할의 Block을 찾아 초기화하고, 해당 Block의 크기에 맞추어 Bitmap의 크기를 결정한다. 

#### Swap-In

Back Storage에 있던 Frame을 Physical Memory로 가져 와 Activate하는 과정을 Swap-in이라고 부르며, 이를 `vm/vmm.c`에 있는 `swap_read_frame`을 통해 구현했다. 다음과 같은 동작을 한다.

* 해당 함수의 인자로 Swap-out되어있던 `struct frame *frame`이 전달되며, `frame->swap_sector`을 참조함으로써 Back Storage의 어떤 영역에 Swap-out된 데이터가 존재하는 지 확인할 수 있다.
* `frame->swap_sector`로부터 한 Page 만큼 (== `SECTORS_PER_PAGE` 개의 Block) 의 데이터를 `block_read`를 통해 읽는다. 
* 읽어온 Swap sector에 해당하는 Bitmap의 Bit들을 0으로 설정하고, `frame->swap_sector`를 -1로 설정하여 해당 Frame이 Physical Memory에 존재함을 명시하였다.

#### Swap-Out

Physical Memory에서 Frame을 Back Storage로 옮기는 과정을 Swap-out이라고 부르며, 이를 `vm/vmm.c`에 있는 `swap_write_frame`을 통해 구현했다. Swap-out은 Physical Memory의 공간이 부족한 상황에서 현재 필요한 Frame을 Physical Memory에 가져오는 경우, 사용되지 않는 Frame을 Evict하는 과정에서 발생한다. 해당 Page swapping의 전체 과정에 대한 내용은 [Page Swapping Sequence](#Page-Swapping-Sequence)에 서술했다. `swap_write_frame`은 다음과 같은 동작을 한다.

* 한 Frame을 Write할 수 있도록 `SECTORS_PER_PAGE`개의 연속된 Block sector를 찾고, 탐색된 Block Sector를 Bitmap에 기록하며, 해당 Block Sectors의 첫 번쨰 Block Index를 계산한다.
  * 이 모든 동작을 `bitmap_scan_and_flip`을 통해 구현할 수 있다.
* 해당 `frame->swap_sector`를 탐색한 Block Sector로 설정한다.
* Back Storage의 `swap_sector`에서 시작하여 한 Frame의 내용을 `block_write`를 통해 작성한다.

#### Choosing Evictee(Victim Frame)

Eviction Frame을 선정하기 위해 Clock Algorithm을 사용했다. Clock Algorithm에 의해 Victim가 될 수 있는 Frame들은  `active_frames`에 존재하는 Frame이다. Victim을 찾기 위해 `vm/swap.c`의 `swap_find_victim`에서 구현했으며, Victim을 판별하기 위한 함수는 `vm/swap.c`의 `check_and_clear_accessed_bit`에서 판별한다.

`active_frame`에 해당하는 Frame의 관리는  `vm/swap.c`에 있는 `swap_register_frame` 및 `swap_unregister_frame`을 통해 등록/등록 해제할 수 있다. 또한, `swap_register_frame`내에서 `clock_hand == NULL`인 경우에 `clock_hand`가 새로 등록한 Frame을 가리키도록 설정하고, `swap_unregister_frame`에서 Clock hand가 등록 해제될 Frame을 가리키고 있는 경우의 Handling을 진행한다.

`swap_find_victim`과 `check_and_clear_accessed_bit`의 동작은 다음과 같다.

* `swap_find_victim`은 `clock_hand`가 가리키는 Frame entry가 `check_and_clear_accessed_bit` 을 만족시키는 동안 `active_frames`를 처음부터 끝까지 순회한다.
* `check_and_clear_accessed_bit`의 인자로  `struct frame *frame`이 주어진다. 인자로 주어진 해당 Frame의 Mapping list의 모든 Memory Map entry들의 User page의 Access bit를 확인하고, 다음 Clock이 돌 때까지 Access되었는지를 확인하기 위해 Access bit를 `false`로 설정한다.  대상이 되는 Entry의 Access bit를 모두 `OR` 한 값을 반환한다.
* 따라서, `swap_find_victim` 의 `while` loop을 탈출할 때 `clock_hand`는 Victim으로 최근에 어떤 Memory mapped page도 Access되지 않은 Frame을 가리킬 것이며, 해당 frame의 pointer를 반환하였다.

#### Page Swapping Sequence

Frame이 없음을 Handling하고, 필요한 Frame을 Back Storage에서 Read하기 위해 더 이상 사용하지 않는 Frame을 선정하여 Back storage로 옮겨 필요한 Frame을 Read하는 전체적인 함수 호출 Sequence는 다음과 같이 정리할 수 있다.

* Page Fault가 일어난다. `userprog/exception.c`의 Page Fault Handler `page_fault`에서 이를 Handle한다.
  * Page Fault의 원인으로 현재 Physical Memory에 대상 Frame이 없는 경우 `vmm/vmm.c`의 `vmm_handle_not_present`가 호출된다.
* `vmm_handle_not_present`에서 Page Fault가 발생한 Address의 Frame을 Physical Memory로 올리기 위해 Swap을 진행한다. (Swap 과정은 Critical section임으로 `swap_lock`에 접근하여 다른 프로세스의 swap을 막는다.)
  * Swap-out할 Frame을 찾고, (`victim  = swap_find_victim()`), 해당 Frame을 Deactivate한다. (`vmm_deactivate_frame (victim)`)
    * `vmm_deactivate_frame` 내에서 `swap_write_file`을 호출해 Back Storage에 Frame을 작성한다.
  * Swap-in 하기 위해 `palloc_get_page`로 Frame 공간을 할당하고, `vmm_active_frame`을 호출한다.
    * `vmm_activate_frame` 내에서 `swap_read_file`을 호출해 Back Storage로부터 Frame을 읽어 Physical Memory에 작성하여 Page Swap을 완료한다.

### On-Process Termination

## Discussion

