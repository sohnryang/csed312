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

### Lazy Loading

### Supplemental Page Table

### Stack Growth

### File Memory Mapping

### Swap Table

### On-Process Termination

