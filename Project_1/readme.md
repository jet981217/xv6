# 프로젝트 1: xv6 시스템 콜 추가 및 활용

## 과제 요구사항

* `getpgid()` 시스템 콜을 xv6에 추가
* `getpgid()`를 사용하여 프로세스 그룹 ID를 반환하는 사용자 프로그램 작성
* Ji Server 환경에서 개발 및 실행
* 변경된 코드와 설명을 포함한 보고서 제출

## 변경 사항

### 1. 초기 코드 분석

* xv6 소스 코드 구조 및 시스템 콜 관련 파일 분석
* `sysproc.c`에서 기존 시스템 콜 구현 패턴 파악
* `syscall.h`, `syscall.c`에서 시스템 콜 테이블 및 관련 정의 확인
* `proc.h`에서 프로세스 구조체 및 그룹 ID 관련 멤버 변수 확인

### 2. 코드 수정

* `syscall.h`에 `SYS_getpgid` 정의 추가
* `syscall.c`의 시스템 콜 테이블에 `sys_getpgid` 함수 추가
* `sysproc.c`에 `sys_getpgid` 함수 구현 (현재 프로세스의 그룹 ID 반환)
* `proc.h`의 `struct proc`에 `pgid` 멤버 변수 추가
* 사용자 프로그램 (`getpgid_test.c`) 작성: `getpgid()` 시스템 콜 호출 및 결과 출력

### 3. 코드 실행 및 결과 확인

* 수정된 xv6 커널 및 사용자 프로그램 컴파일 후 실행
* `getpgid_test` 프로그램 실행 결과로 현재 프로세스의 그룹 ID 정상 출력 확인

## 개발 환경

* **서버**: Ji Server
* **OS**: Unix-like 환경
* **빌드 및 실행**:

    ```sh
    $ git clone [https://github.com/mit-pdos/xv6-public.git](https://github.com/mit-pdos/xv6-public.git)
    $ cd xv6-public
    $ make qemu-nox
    # 사용자 프로그램 실행: getpgid_test
    ```

## 코드 상세 분석

### 1. `sysproc.c` 내 주요 코드 흐름

* `sys_getpgid` 함수에서 현재 프로세스의 `pgid` 멤버 변수 값 반환
* `proc` 구조체의 `pgid` 멤버 변수를 통해 프로세스 그룹 ID 관리

### 2. `syscall.h`, `syscall.c` 수정

* `syscall.h`에 `SYS_getpgid` 정의 추가하여 시스템 콜 번호 할당
* `syscall.c`의 시스템 콜 테이블에 `sys_getpgid` 함수 포인터 추가하여 시스템 콜 핸들러 등록

### 3. 사용자 프로그램 (`getpgid_test.c`)

* `syscall()` 함수를 사용하여 `getpgid()` 시스템 콜 호출
* 반환된 프로세스 그룹 ID를 `printf()` 함수로 출력

## 제출 방식

* **보고서**: 코드 수정 내용 및 설명 포함하여 PDF 제출
* **코드 제출**: Ji Server에서 `submit` 스크립트를 이용

    ```sh
    $ make clean
    $ ~swe3004/bin/submit pa1 xv6-public
    ```
