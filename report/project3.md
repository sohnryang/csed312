# Project 3 Final Report

## Solution

### Frame Table and Supplement Page Table

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

#### New structure for Frame Table

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

#### New structure for Supplement Page Table

##### `mmap_info`

Frame의 `struct list mappings`와 관계된 구조체이다. `mmap_info->elem`을 `frame->mappings`에 삽입하여 Frame의 Memory Mapping을 나타내고, 프로세스 별로 Memory mapping의 중복을 없애고 관리하기 위해 `mmap_info->map_elem`을 `thread->mmaps`에 삽입한다. 멤버는 다음과 같다.

* `void *upage`
  * User가 접근하는 Virtual Address의 Offset 부분을 제외한, Virtual Page의 Address이다.
* `void *pd`
  * Page Directory를 가리키는 pointer이다.

* `struct hash_elem map_elem`
  * Virtual Address에서 Physical Address로의 Translation을 Hash map을 통해 구현했다. 해당 Hash map은 각 Process 별로 존재 (`thread->mmaps`)하며, 각자 고유한 Translation을 중복 없이 관리하기 위하여 정의하였다.
* `struct file *file` / `bool writable` / `bool exe_mapping` / `off_t offset` / `uint32_t mapped_size`
  * Virtual Address에서 Physical Address로 Translation 시 Frame이 가지는 정보를 다루고 있는 Field들이며, Anonymous Page와 File-Mapped Page에 따라 초기화 방식 및 함수를 달리 하여 구현했다.
* `struct list_elem elem` / `struct frame *frame`
  * `frame->mappings`에 삽입하기 위한 Element와, 역으로 Element가 자신이 삽입된 Frame을 찾기 위해 가지고 있는 Frame pointer이다.
* `struct list_elem chunk_elem`
  * System Call에 의해 생성된 `mmap_usr_block`의 `mmap_usr_block->chunks`에 삽입하기 위한 Element이다.

#### Initialization

해당 `struct frame` 은 `vm/frame.c`의 `frame_init`에서 초기화된다.

* User page `kpage` 를 `NULL`로 초기화한다. 해당 `kpage` 에 해당하는 Physical Address가 `frame`이다.
* Process를 복사 시 `stub`을 통해 함수를 호출하게 되는데, 이 Stub Frame을 생성하기 위해  `is_stub`을 `true`로 초기화한다.
* Swap 여부를 초기화한다. 생성 시 Frame이 Physical Memory에 올라가게 되기 때문에 `is_swapped_out`을 `false`로, `swap_sector`를 `-1`로 초기화한다.
* `frame`의 Physical Address로의 Mapping은 `struct list`를 통해 구현하였으며, 이 list를 초기화한다.

`frame_init`은 Frame이 새로 필요할 경우  `frame`을 할당할 때 마다 초기화되도록 구현하였다. 다음과 같은 Function Call을 통해 `frame_init`이 불리게 된다.

* User의 System Call 등으로 `vm/vmm.c`의 `vmm_setup_user_block`이 불린 경우 User의 Virtual Address를 기준으로 `vmm_create_file_map`이 불리며,  Stack을 생성 (`userprog/process.c`의 `setup_stack`)과 성장(`vm/vmm.c`의 `vmm_grow_stack`)시 어떤 Process에도 귀속되지 않은 Anonymous Page를 할당받기 위해 `vmm_create_anonymous`가 호출된다. 이 과정에서 `mmap_info`를 생성하는 데 오류가 생긴 경우 `mmap_info`의 이미 생긴 Chunk들을 모두 제거하여 메모리를 처음과 같은 상황으로 돌려놓고 실패를 반환한다.

* `vmm_create_file_map` (File-mapped Page) 혹은 `vmm_create_anonymous`에서 (Anonymous Page) `vmm_link_mapping_to_thread`이 호출된다.

  * 해당 함수의 인자로 `mmap_init_file_map`(File-mapped Page)와 `mmap_init_anonymous` (Anonymous Page)에서 생성된 Physical Address Translation Data를 전달한다.

* `vmm_link_mapping_to_thread`을 File-mapped Page 및 Anonymous Page에서 모두 호출하게 되며, 여기서 Frame을 할당받고 생성된 Frame을 `frame_init`을 통해 초기화하게 된다. 

  이후 Call Stack을 통해 전달된 Address Translation (`struct mmap_info *info`)를 전달하고, 현재 Process와 Frame의 각각 `mmap_info->elem`과 `frame->elem`을 삽입하여 List의 원소로써 등록한다.

#### Management

Process별로 Frame Table이 생성되며, Frame table의 Entry가 되는 Page를 구분하면 크게 두 가지 종류가 있다. 특정 File과 연관되어 file이 mapping된 File-mapped Page와, 어떤 File에도 관계가 있지 않은(주로 Stack) Anonymous Page가 존재한다. `vm/vmm.c`와 `vm/mmap.c`에서 Virtual Memory System을 구현함에 있어서 두 Page의 종류를 구분하여 함수를 구현하였다.

##### Initializing File mapping (`mmap_info`)

Frame을 할당하고 메모리의 값을 읽어 오는 과정보다 선행되어야 할 과정은 Frame의 Physical Address와 User의 Virtual Address를 Mapping하는 과정이다. Lazy Loading의 측면에서 생각했을 때, Address의 Mapping 정보 저장이 선행되어야 꼭 필요한 시점에 User가 Page를 필요로 하는 상황의 Virtual Address에 접근하여 Page Fault Handler를 통하여 메모리의 값을 읽으면 되기 때문이다. 해당 내용은 [Lazy Loading](#Lazy-Loading)에서 자세히 서술했다. 이 카테고리에 해당하는 함수는 다음의 두 함수가 있으며, 반환형은 두 함수 모두 `struct mmap_info*` Type을 반환한다.

* `vmm_create_anonymous`
  * Anonymous Page에 대한 `mmap_info`를 할당하고, `mmap_init_anonymous`를 통해 `mmap_info`의 값을 채운다.
  * `mmap_init_anonymous`에서 User Page의 Virtual Address Page Base와의 Mapping을 저장하며 (`info->upage = upage`), Mapped된 파일이 없음을 명시한다. (`info->file == NULL`, `info->exe_mapping = false`, `info->offset = 0`, `info->mapped_size = 0`)
  * `vmm_link_mapping_to_thread`으로 해당 Address translation을 현재 Process에 등록한다.
  * `vmm_create_frame`으로 Frame을 Allocate할당한다.
* `vmm_create_file_map`
  * File-mapped Page에 대한 `mmap_info`를 할당하고, `mmap_init_file_map`을 통해 `mmap_info`의 값을 채운다.
  * `mmap_init_file_map`에서 User Page의 Virtual Address Page Base와의 Mapping을 저장하며 (`info->upage = upage`), Mapped된 파일 정보를 명시한다. (`info->file == file`, `info->exe_mapping = exe_mapping`,  `info_writeable = writable`, `info->offset = offset`, `info->mapped_size = size`)
    * `writable` 및 `exe_mapping`은 File property에 의존한다.
    * Page보다 File의 크기가 클 수 있으며, 이 때 File을 여러 개의 Page에 나누어 Mapping하기 위해 `offset` 및 `mapped_size`로써 각 페이지가 File에 Map된 Offset과 크기를 저장한다.
  * `vmm_link_mapping_to_thread`으로 해당 Address translation을 현재 Process에 등록한다.
  * `vmm_create_frame`으로 Frame을 Allocate할당한다.

두 함수 모두 마지막으로 Frame과 `mmap_info`를  Mapping(`vmm_map_to_frame`)하여 File mapping 과정을 종료한다.

##### Allocating Frame with Hash Management

앞서 `vmm_create_anonymous` 및 `vmm_create_file_map`의 마지막에서 해당 과정이 진행되며, 크게 나누어 `mmap_info` 할당 및 초기화, 현재 Process에 `mmap_info`연결, `frame` 할당, `mmap_info`와 `frame` 연결으로 구성된다.

* Virtual Address의 Page Base와 (File Memory map인 경우) File의 정보가 내장된 `mmap_info`Hash map의 Element를 할당하고, Anonymous Page 혹은 FIle-mapped page에 따라 달리 초기화를 진행한다.
* `vmm_link_mapping_to_thread`를 사용하여 Hash collision이 일어나지 않는지 확인 후 Hash map에 삽입을 진행한다. 여기서 Stub Frame을 삽입한다.
* Physical Address의 정보와 Physical Memory 상에서 정보의 저장 상황을 나타내는 `frame`을 `vmm_create_frame`을 통해 할당한다.

* 이후 `frame`과 `mmap_info`를 `vmm_map_to_frame`을 사용하여 `mmap_info->elem`을 `frame->mappings`에 삽입하여 Mapping한다. 

##### Searching Frame Mapping

`vmm_lookup_frame`에서 현재 Process의 `thread->mmaps`에 User page base address로 Hash Mapping된 Frame을 찾는다.

해당 함수는 Page Fault Exception Handler에서 Swapped-out된 Page의 Frame이 존재하여 Swap-In 하여 Exception을 해결 할 수 있을 때, Hash를 통해 Frame을 찾고자 호출하게 된다. [`vmm_activate_frame`](#`vmm_activate_frame`)에서 자세히 서술하였다.

##### Removing Frame Mapping

`vmm_unlink_mapping_from_thread`에서 현재 Process의 `thread->mmaps`의 원소였던 `mmap_info->map_elem`을 제거한다. 또한, User가 `mmap_`의 System Call로 Mapping한 `pagedir`에 대해서 `pagedir_clear_page`를 수행하며, 마지막으로 `mmap_info` 자체를 제거한다.

해당 함수를 통해 User의 Memory map을 Clean-up 하기 때문에 `munmap_` System Call을 호출했을 때 불리는 `vmm_cleanup_user_block`통해 호출되고, `mmap_` System Call 호출 시의 Error Handling 시에도 호출된다. 자세한 내용은 [`munmap_`](#`munmap_`)에 서술하였다.

### Lazy Loading

Segment Load는 `userprog/process.c`의 `load_segment` 함수에서 이루어진다. PintOS Project 2에서 구현된 내용에선 직접 `file_read`를 사용하여 한 번에 Load가 이루어 진다. 이를 Lazy Load를 구현하기 위해, File Map만을 생성하였다. `load_segment` 함수가 호출되었을 때 함수의 Call Stack은 다음과 같이 진행될 것이다.

* `userprog/process.c`의 `load_segment`
  * `#ifdef VM ~ #else`에 의해 전처리된 `vmm_create_file_map`이 호출된다.
* `vm/vmm.c`의 `vmm_create_file_map`
* `vm/mmap.c`의 `mmap_init_file_map`이 호출된다.
  * 해당 함수는 인자로 주어진 `struct mmap_info*`에 File mapping에 대한 정보를 기록하는 함수이다.

실제로 Loading은 Address에 접근했을 경우 일어나며, 처음엔 Invalid Address로의 접근이기 때문에 Page Fault Handler에서 Handle할 수 있다. Page Fault Handler의 호출로 의해 Load가 불리는 과정은 다음과 같다.

* `userprog/exception.c`의 `page_fault`에서 `not_present`가 참이 되며, `vmm_handle_not_present`를 호출한다.
* `vm/vmm.c`에 `vmm_handle_not_present`는 Page Fault Address가 해당하는 Virtual Address Page를 생성한다. (이 과정에서 Physical Memory로 올릴 수 없는 경우 Eviction을 진행한다.) 이를 `vmm_activate_frame`을 호출하여 Page Fault Handler를 완성한다. **해당 과정에서 `fs_lock`를 Acquire하였으며, Atomicity를 보장했다. 그 이유는 [Race Condition Between File System and Memory Mapping](#Race-Condition-Between-File-System-and-Memory-Mapping) 에서 자세히 서술했다.**
* `vm/vmm.c`의 `vmm_activate_frame`이 호출되며, 인자로 전달된 Frame의 `frame->is_swapped_out`이 거짓인 경우의 Code를 실행하게 된다. **해당 과정 이후 `fs_lock`을 Release하여 Atomicity를 충족하였다.**
  * `frame`과 관련 있는 `mmap_info->file`이 존재하기 때문에, 조건이 충족되는 경우의 `file_seek`와 `file_read`가 일어나도록 구현했다. 이 부분에서 직접적인 Data의 Load가 이루어지게 된다.

##### `vmm_activate_frame`

`vmm_activate_frame`은 실제로 값을 Physical Memory에 올리기 위한 함수로, Lazy Loading에서 호출되어 Block의 데이터가 읽어들어지는 시점이다. 다음의 두 가지 경우에 값을 Physical Memory에 올릴 필요가 있으며, 각 케이스별로 다르게 동작하도록 구현하였다.

* `frame->swapped_out`이 `true`인 경우에는 현재 Page가 실존하나 Back storage에 존재하는 경우이다.
  * 이 경우 `swap_read_frame`을 사용해 Swapped-out 된 Frame을 읽어들이고 현재 프로세스의 `pagedir`를 설정한다. Swapped-out과 `swap_read_frame`에 대한 내용은  [Swap-Out](#Swap-Out)에서 서술하였다.

* 이외의 경우는 File-mapped 된 `mmap_usr_block`에서 File의 Data를 읽어들이는 경우이다. `mmap_usr_block`과 그 System Call인 `mmap_`에 대해선 [File Memory Mapping](#File-Memory-Mapping)에서 서술하였다.
  * File의 모든 Mapping Entries가 되는 Page들에 대해 다음을 수행한다.
    * File의 크기가 `PGSIZE`보다 클 수 있기 때문에 `PGSIZE` 단위로 File의 Offset을 나누도록 `mmap_` System Call의 `vmm_setup_user_block`에서 설정하였으며, 해당 Offset으로 `file_seek`를 수행하고 `min(PGSIZE, [남은 파일의 길이])` 만큼 File Read를 수행하여 할당된 Page에 작성한다.
    * (마지막 Page의 경우) 파일의 끝을 명시하기 위해 읽고 남은 Page의 공간을 모두 0으로 `memset`하였다.

마지막으로 해당 Frame을 `swap_register_frame`을 통해 Swap scheduler에 등록한다. Swap Scheduling에 대한 내용은 [Choosing Evictee(Victim Frame)](#Choosing-Evictee(Victim-Frame))에서 서술하였다.

#####  `vmm_deactivate_frame`

`vmm_activate_frame`의 반대에 해당하는 `vmm_deactivate_frame`은 Back Storage (혹은 File map의 경우 Disk)에 저장하기 위한 함수이다. Lazy Loading에서 호출되어 Block에 데이터를 쓰는 역할을 수행한다.

모든 `frame->mappings`에 대해 `file_seek`와 `file_write`을 통해 Back storage (혹은 Disk)에 파일을 작성한다. `file_seek`과 `file_write`를 통해 해당하는 Offset으로 이동하여 File을 작성하게 된다. 단, 가능한 한 File system으로 접근을 최소화하기 위해 꼭 필요한 경우만 `file_seek`과 `file_write`를 호출하도록 구현하였으며, File 작성이 필요한 경우는 다음과 같다.

* 파일이 존재하고 (`info->file != NULL`), Executable File에 Mapping되지 않았으며 (`!info->exe_mapping`), 해당 파일에 변화가 존재하는 경우 `pagedir_is_dirty (cur->pagedir, info->upage)`에만 작성해야 한다.

최종적으로 Deactivate를 진행하여 할당되었던 Physical Memory를 Free하며 (`palloc_free_page (frame->kpage)`),  Swap scheduler에서 등록을 해제하였다. Swap Scheduling에 대한 내용은 [Choosing Evictee(Victim Frame)](#Choosing-Evictee(Victim-Frame))에서 서술하였다.

### Stack Growth

처음에 Process가 시작할 때 Stack은 `userprog/process.c`의 `load` 함수에서 `if (!setup_stack (esp))`와 같은 형태로 Stack을 생성했었다. Project 3에서 Virtual Memory를 도입하면서, 해당 `setup_stack` 함수를 `vmm_create_anonymous`로 Stack 영역을 생성하였다.

기존의 `userprog/exception.c`에 있는 Page Fault Handler를 수정해서 Stack이 자랄 때 마다 새로운 Page를 할당하도록 구현한다. Page를 할당해야 하는 경우의 조건을 만족시키는 경우에만 할당하며 그 경우는 다음과 같다. Address에 해당하는 Stack 영역이 Physical Memory 상에 존재하지 않는 상태에서 호출할 경우 해당 Stack 영역을 위한 Page를 할당 시도하고, 재실행을 해야 한다. 순전히 Invalid한 Virtual Address에 접근하는 경우를 제외해야 한다. Lazy Load와 유사하게, Invalid한 Stack의 Address에 접근한 경우에 Address를 포함하는 Stack Page를 만들어 주어야 한다. 구현한 Page Fault Handler에서는 다음과 Stack Page를 생성하도록 구현했다.

* `userprog/exception.c`의 `page_fault`에서, 현존하는 Stack을 넘어선 Address의 접근은 `not_present`가 `true`가 되며, 이 경우 세 번의 조건을 검사한다.
  * `vmm_handle_not_present (fault_addr)`의 경우,  내부에서 `vmm_lookup_frame`를 호출하여 현재 프로세스에 귀속된 Frame 중 존재하는지 찾게 된다. 그러나 Not Present하지만 존재하지 않았던 Address이기 때문에 `vmm_lookup_frame`에서 `false`를 반환하게 된다.
  * `user && vmm_grow_stack (fault_addr, f->esp)`와 `!user && vmm_grow_stack (fault_addr, cur->esp_before_syscall)` 의 경우는 각각 User 레벨과 Kernel 레벨에서 Stack Growth가 필요한 상황이다. 해당 경우 모두 `vmm_grow_stack`을 호출한다.

* `vmm_grow_stack`은 다음과 같은 동작을 한다.
  * 다음과 같은 경우에는 Stack Growth를 막는다.
    * 기존의 Stack pointer보다 일정 거리 이상 떨어진 Virtual Address를 참조하는 경우의 Stack Growth를 막았다.
    * Stack의 Growth가 계속 진행되어 사용 가능한 모든 영역의 끝까지 Stack이 자란 경우 Stack이 성장할 수 없음을 Handle하였다.
  * 그 외의 경우는 정말 Page가 할당되지 않아 발생한 Page Fault Exception이기에, 해당 Exception이 불리게 된 Address의 Page를 생성한다. Stack은 어떤 File에도 Mapping되지 않은 Anonymous Page이기 때문에 `vmm_create_anonymous (pg_round_down (fault_addr), true)`로 Fault Address에 해당하는 Page를 생성했다.

### File Memory Mapping

##### `mmap_usr_block`

PintOS Project 2에서 OS의 기본적인 System call과 File System의 System call을 구현하였다. Project 3에선 File의 Memory mapping을 관리하기 위한 System Call인 `mmap_` 과 `munmap_`을 구현하였다. 해당 System call을 통해 할당된 Memory는 따로 `mmap_usr_block`이라는 구조체로 관리했으며, 해당 구조체는 `thread->mmap_blocks`에 `mmap_usr_block->elem`을 삽입함으로써 Element를 등록한다. 멤버는 다음과 같다.

* `mapid_t id`: Process별로 정의된 `struct list mmap_blocks`  에 해당 `mmap_usr_block`을 삽입할 때, 다른 Block들과 구분하기 위한 ID이다.
* `struct file* file`: User가 `mmap_` System call의 인자로 넘긴 파일이다.
* `struct list chunks`: `mmap_usr_block`에 해당하는 Address Transition을 관리하기 위한 `struct mmap_info`를 저장하기 위한 자료구조이다.
* `struct list_elem elem`: `frame->mmap_blocks`에 삽입하기 위한 Elements이다.

#### New System Calls

##### `mmap_`

2개의 인자를 받는 System Call로, 각각 File mapping을 위한 File descriptor `int fd`와 Mapping을 수행할 Virtual Address `void* addr`이다. 다음과 같이 System Call을 Handle하였다.

* File mapping이 불가능한 경우를 미리 Handle한다. 이 경우는 해당 File descriptor가 `STDIN` 혹은 `STDOUT`의 역할을 수행하는 경우이다.

* `mmap_usr_block`을 할당받고, 해당 `mmap_usr_block`에서 사용할 `id`를 결정한다.

* `fd`에 해당하는 File 을 다시 Open한다. Physical Memory에 File을 처음부터 순차적으로 접근시키기 위해서이다. **이 과정부터  File system에 대한 접근이기에, File system에서 사용하도록 구현한 lock인 `fs_lock`을 통해 Atomicity를 보장하였다.**

* `mmap_init_user_block (block, id, file_reopened)`로 할당받은 `mmap_user_block`의 Metadata를 설정한다. 

* `vmm_setup_user_block`으로 `addr`의 주소로 Address Translation이 이루어지도록 `mmap_info` 를 설정하고,  `vmm_create_file_map`으로 File map만을 생성한다. 파일의 총 사이즈가 한 Block size보다 클 수 있기에, 최대 크기를 `PGSIZE`로 제한하면서 여러 개의 File map을 생성한다. 생성된 `mmap_info`를 `mmap_user_block->chunks`의 끝에 삽입하여 순차적으로 파일을 Mapping할수 있다.

  * FIle map만을 생성하고, 실제로 파일의 Data를 읽어오는 부분은 [Lazy Loading](#Lazy-Loading) 과정에서 진행된다.

  **이 과정까지 File system에 대한 접근이기에, `fs_lock`을 통해 Atomicity를 보장하였다.**

* 이후 Process의 `mmap_blocks` 리스트에 생성한 `mmap_user_block->elem`을 `id`의 오름차순으로 삽입한다.

##### `munmap_`

`mmap_` System Call을 통해 할당받은 `mmap_usr_block`을 정리한다. 인자로 `mmap_usr_block` 의 Identifier인 `mapid_t id`이 주어진다. 다음과 같이 System Call을 Handle하였다.

* `vmm_get_mmap_user_block (id)`를 통해 `id`에 해당하는 `mmap_user_block`을 찾는다. 존재하지 않는 경우 `NULL`을 반환한다

* 유효한 User Block이 주어진 경우, `vmm_cleanup_user_block`을 호출하여 해당 Block을 Un-map한다. **이 과정은 `fs_lock`을 통해 Atomicity를 보장하였다.**
  * `vmm_cleanup_user_block`에서는 `mmap_usr_block->chunks`의 모든 Element (Page로 할당된 File의 Chunk)를 `vmm_deactivate_frame`을 통해 명시적으로 Swap-out을 진행했으며, 이후 `vmm_unmap_from_frame`으로 `mmap_info->thread`  정보를 제거했으며, `vmm_unlink_mapping_from_thread`으로 Address Translation 정보를 제거하고, 최종적으로 `mmap_user_block`을 Free하여 Memory의 Un-map을 완성하였다.


### Swap Table

#### Data Structure

Swap을 구현하기 위해 `vm/swap.c`에서 사용한 Data Structure와 그 역할은 다음과 같다.

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

Physical Memory에서 Frame을 Back Storage로 옮기는 과정을 Swap-out이라고 부르며, 이를 `vm/vmm.c`에 있는 `swap_write_frame`을 통해 구현했다. Swap-out은 Physical Memory의 공간이 부족한 상황에서 현재 필요한 Frame을 Physical Memory에 가져오는 경우, 사용되지 않는 Frame을 Evict하는 과정에서 발생한다.

`swap_write_frame`은 다음과 같은 동작을 한다.

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

### On-Process Termination

PintOS Project 2에서 구현된 `userprog/process.c`의 `process_exit`는 Process의 `pagedir`만 할당 해제하였다. Virtual Memory System을 도입했기 때문에, 다음과 같은 추가적인 구현을 수행하였다.

* 새로 User가 할당한 `mmap_usr_block`을 모두 반환한다. **User가 System Call을 통해 할당한 `mmap_usr_block`은 File System으로 Mapping되었기 때문에, 이를 할당 해제하는 과정에서 `fs_lock`을 사용해 Atomicity를 보장하였다.**
* 마지막으로 Process에서 Frame Table의 모든 Page entry를 할당 해제하고, Frame table과 Memory Mapped table을 할당 해제하여 Virtual Memory Management System을 제거한다. 이 기능은 `vm/vmm.c`의 `vmm_destroy`를 통해 구현하였다.

## Discussion

### Race Condition Between File System and Memory Mapping

현재 구현에서 Process가 Lock을 최대로 Acquire할 수 있는 개수는 2개로, File System에 대한 `fs_lock`와 Swapping을 진행할때 사용하는 `swap_lock`이 있다. 결과는 모든 Virtual Address를 `mmap_info`를 통해 Mapping을 할당하고 부여했다. 두 Lock을 동시에 잡는 경우 Race Condition이 발생할 수 있으며, 그 이유를 추정하자면 다음과 같을 것이다.

* Kernel에서 호출하는 Memory Mapping 과정에서는 `fs_lock`을 일절 Acquire하지 않으며, User가 명시적으로 System Call을 통해 `mmap_usr_block`을 통해 Memory Mapping과 Physical Frame을 만든 경우에만 `fs_lock`을 Acquire하고 Lock을 Hold하여 File system으로의 접근이 이루어지도록 구현하였으며, 기존 Pintos Project 2까지 구현하였던 File System Call (`read_`, `write_` 등)에서도 `fs_lock`을 Hold한다.
* `swap_lock`은 Swapped-out 및 Swapped-in이 수행될 때 Lock을 Acquire하게 된다. 

두 `lock`이 동시에 잡히는 경우는 다음과 같다. Page Fault Handler가 호출되었을 때, 새로운 프레임을 가득 찬 Supplemental Page Table에 삽입하고자 Eviction을 진행할 것이다 (`swap_lock`을 잡고 진행한다). 그러나 Eviction당할 Page가 `fs_lock`을 잡는 경우 - Swap 시점에서 `read_`나 `write_`를 호출하여 Swapped-in이나 Swapped-out의 대상 Frame과 Eviction의 대상인 File이 동일한 경우, `swap_lock`과 `fs_lock` 간의 Race Condition이 발생하게 될 것이며, 이러한 상황이 일어나는 경우는 프로세스의 실행 파일이 User의 System Call에 의해서 Memory-Mapped되는 경우를 대표적으로 예로 들 수 있다. (Swap victim과 File System lock이 같은 경우)

다음과 같은 방법으로 해결하였다.

* Page Fault Handler에 `fs_lock`을 Hold하도록 구현한다. 만약 `vmm_page_not_present`가 호출되어 실횅되는 경우, 앞서 설명한 Race Condition을 이유로 인해 함수의 전 영역을 Critical Section으로 간주해야 했기 때문이다.

### Sharing & Copy-on-Write

핀토스 공식 문서에서는 추가적으로 구현할 만한 기능으로 sharing을 제시하고 있다. 비록 시간적 여유 등의 이유로 이를 구현하지는 못했지만, 현재 설계에서 sharing을 구현하는 복잡도는 크지 않을 것으로 예상된다. 이미 현재 구현에는 리눅스의 object-based reverse mapping과 비슷한 설계를 따랐고, 한 page frame에 여러 virtual address가 사용되는 상황을 충분히 다룰 수 있다. 핀토스의 경우 새로운 프로세스를 생성하기 위해 `fork` system call 대신 `exec` system call을 사용하기 때문에, 실행 파일의 코드 영역을 공유하기 위해서는 같은 실행 파일에서 실행되는 프로세스를 식별해야 한다는 복잡도가 추가될 것이다. 이는 일종의 reference counting을 사용하여, 같은 파일에서 실행된 프로세스는 같은 file pointer를 사용하는 등의 방법으로 구현해야 할 것으로 보인다.

좀 더 나아가, 현재 설계에서는 copy-on-write의 구현 복잡도 또한 그리 높지 않을 것으로 기대할 수 있다. `mmap_info` 구조체에 copy-on-write를 위해 잠시 read only 상태가 되었는지를 나타내는 플래그를 추가하고, page fault handler에서 이를 처리한다면 copy-on-write를 구현할 수 있다.

### Extending Memory Map

핀토스의 `mmap` system call은 상용 OS와 비교할 때 기능이 제한되어 file mapping 정도만이 가능하다. `mmap` system call에 인자를 더 추가하여, 단순한 file mapping 뿐만 아니라 file에 연결되지 않은 anonymous mapping을 만들어 동적 메모리 할당과 유사한 기능을 구현하거나, 여러 프로세스 사이에서 메모리를 공유할 수 있도록 하여 IPC를 구현하는 등 다양한 활용이 가능하게 만들 수 있을 것이다.
