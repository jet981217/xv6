# 프로젝트 2: CPU 스케줄링

## 과제 요구사항

*   xv6에 CFS(Completely Fair Scheduler) 구현
*   실행 가능한 프로세스 중에서 최소 가상 실행 시간(virtual runtime)을 가진 프로세스 선택
*   각 타이머 인터럽트마다 실행 시간(runtime) 및 가상 실행 시간(vruntime) 업데이트
*   작업이 시간 할당량을 초과하여 실행되면 CPU 양보 강제 실행
*   기본 nice 값은 20이며, 0에서 39까지의 범위를 가짐. nice 값 20의 weight는 1024
*   Nice(0-39)에서 weight로의 변환 (Linux와 같이 미리 정의된 배열로 하드 코딩)

    ```
    weight = 1024 / (1.25)^(nice - 20)
    ```

    | nice | weight | nice | weight | nice | weight | nice | weight |
    | :--- | :----- | :--- | :----- | :--- | :----- | :--- | :----- |
    | 0    | 88761  | 10   | 9548   | 20   | 1024   | 30   | 110    |
    | 1    | 71755  | 11   | 7620   | 21   | 820    | 31   | 87     |
    | 2    | 56483  | 12   | 6100   | 22   | 655    | 32   | 70     |
    | 3    | 46273  | 13   | 4904   | 23   | 526    | 33   | 56     |
    | 4    | 36291  | 14   | 3906   | 24   | 423    | 34   | 45     |
    | 5    | 29154  | 15   | 3121   | 25   | 335    | 35   | 36     |
    | 6    | 23254  | 16   | 2501   | 26   | 272    | 36   | 29     |
    | 7    | 18705  | 17   | 1991   | 27   | 215    | 37   | 23     |
    | 8    | 14949  | 18   | 1586   | 28   | 172    | 38   | 18     |
    | 9    | 11916  | 19   | 1277   | 29   | 137    | 39   | 15     |

*   시간 할당량 계산 (스케줄링 대기 시간은 10 ticks)

    ```
    time_slice = 10 tick * (weight of current process / total weight of runnable processes)
    ```

*   가상 실행 시간 계산

    ```
    vruntime += Δruntime * (weight of nice 20 (1024) / weight of current process)
    ```

## 구현 세부 사항

*   **새로 포크된 프로세스:** 부모 프로세스의 vruntime을 상속받음
*   **깨어난 프로세스:** 가상 실행 시간은 (준비 큐에 있는 프로세스의 최소 vruntime - vruntime(1 tick))으로 설정

    ```
    vruntime(1 tick) = 1 tick * (weight of nice 20 (1024) / weight of task)
    ```

    *   프로세스가 깨어날 때 `sched()`를 호출하지 않음
    *   현재 프로세스의 시간 할당량이 만료되도록 함
    *   깨어난 프로세스는 (위의 공식에 따라) 최소 vruntime을 가짐
    *   그러나 현재 프로세스의 시간 할당량이 만료되기 전에 깨어난 프로세스를 스케줄링하지 않음 (xv6의 기본 동작)
*   CFS가 올바르게 구현되었는지 확인하기 위해 `ps()`를 수정해야 함
*   **예상 출력 (`mytest.c`)**

    *   프로세스에 대한 다음 정보 출력
    *   millitick 단위 사용 (tick에 1000 곱하기)
    *   runtime, vruntime, total tick
    *   runtime 및 vruntime을 표시하기 위해 float/double 유형을 사용하지 않음
    *   커널은 부동 소수점 연산을 가능한 한 피함
    *   이름 섹션의 들여쓰기는 프로세스의 이름이 길거나 (최대 10자) 매우 큰 값(runtime, vruntime)을 갖는 경우에도 정렬되어야 함
*   vruntime 정수 오버플로 케이스 고려
    *   정수 범위를 넘어서더라도 문제 없이 작동해야 함
    *   정상적으로 출력되어야 함
    *   runtime, total tick에 대해서는 걱정하지 않아도 됨
*   시간 할당량이 6.5인데 타이머 인터럽트가 1 tick마다 발생하는 경우 (컨텍스트 스위치는 1 tick에서만 발생할 수 있음)
    *   작업은 시간 할당량(7 ticks)을 초과하여 실행되고 vruntime을 추가함

## 제출

*   xv6에 CFS 구현
*   Ji Server에서 제출 및 제출 확인 바이너리 파일 사용

    ```sh
    $ make clean
    $ ~swe3004/bin/submit pa2 xv6-public
    ```

    *   여러 번 제출할 수 있으며, 제출 내역은 check-submission을 통해 확인할 수 있음
    *   마지막 제출만 채점됨
*   보고서

    *   iCampus에 제출
    *   수정된 코드 및 설명
    *   보고서 분량 자유
    *   `pa2_2023123456.pdf`

