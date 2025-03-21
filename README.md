# xv6 운영체제 프로젝트 설명

본 프로젝트는 성균관대학교 서의성 교수님의 운영체제 수업의 과제 결과물입니다. xv6 운영체제는 MIT에서 교육용으로 개발한 간단한 UNIX 계열 운영체제입니다. 본 과제에서는 xv6에 여러 기능을 추가하고 수정하는 작업을 진행했습니다.

### 프로젝트 목표

본 프로젝트의 목표는 xv6 운영체제에 대한 이해도를 높이고, 실제 운영체제 개발 과정을 경험하는 것입니다. 이를 위해 다음과 같은 작업을 수행했습니다.

*   xv6 운영체제 부팅
*   시스템 호출 구현
*   CPU 스케줄링 알고리즘 구현
*   가상 메모리 관리 기법 구현
*   페이지 교체 알고리즘 구현

### 프로젝트 내용

본 프로젝트는 총 5개의 세부 프로젝트로 구성되어 있으며, 각 프로젝트의 내용은 다음과 같습니다.

*   **프로젝트 0: xv6 운영체제 부팅**

    *   xv6 운영체제를 부팅하고, 기본적인 동작을 확인합니다.
*   **프로젝트 1: 시스템 호출 구현**

    *   새로운 시스템 호출을 xv6 커널에 추가합니다.
    *   `getnice`, `setnice`, `ps` 시스템 호출을 구현하여 프로세스의 우선순위를 조회하고 변경하며, 프로세스 정보를 출력하는 기능을 추가했습니다.
*   **프로젝트 2: CPU 스케줄링 알고리즘 구현**

    *   xv6의 기본 스케줄링 알고리즘을 CFS(Completely Fair Scheduler) 알고리즘으로 대체합니다.
    *   CFS 알고리즘을 구현하여 프로세스들이 CPU 시간을 공정하게 할당받도록 했습니다.
*   **프로젝트 3: 가상 메모리 관리 기법 구현**

    *   xv6에 `mmap()`, `munmap()`, `freemem()` 시스템 호출과 페이지 폴트 핸들러를 구현합니다.
    *   `mmap()`을 통해 파일이나 익명 메모리 영역을 프로세스의 주소 공간에 매핑하고, 페이지 폴트 핸들러를 통해 필요할 때 페이지를 할당하는 가상 메모리 관리 기능을 구현했습니다.
*   **프로젝트 4: 페이지 교체 알고리즘 구현**

    *   xv6에 페이지 레벨 스와핑 기능을 구현하고, LRU 목록으로 스왑 가능한 페이지를 관리합니다.
    *   클락 알고리즘을 사용하여 페이지 교체 정책을 구현하고, 메모리 부족 상황에서 페이지를 스왑 아웃하고 스왑 인하는 기능을 구현했습니다.
