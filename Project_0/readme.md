# 프로젝트 0: xv6 부팅 메시지 수정  

## 과제 요구사항  
- xv6 운영체제를 부팅할 때 학번, 이름, 그리고 추가 메시지를 출력하도록 수정  
- init 프로세스를 분석하여 shell 실행 전 메시지를 출력하는 코드 변경  
- Ji Server 환경에서 개발 및 실행  
- 변경된 코드와 설명을 포함한 보고서 제출  

## 변경 사항  
### 1. 초기 코드 분석  
- xv6를 `make qemu-nox`로 실행하여 기본 부팅 메시지 확인  
- 출력 로그에서 `init` 관련 메시지 식별  
- `ls | grep init`을 사용하여 관련 파일 찾기  
- `grep "init: starting sh" init.c`로 `init.c` 내 출력 코드 위치 확인  

### 2. 코드 수정  
- `init.c`에서 `printf(1, "init: starting sh\n");` 부분을 찾아 원하는 메시지를 추가  
- 학번, 이름, 추가 메시지를 출력하도록 수정  

### 3. 코드 실행 및 결과 확인  
- 수정된 코드를 `make qemu-nox`로 실행하여 정상 출력 여부 확인  
- 출력 결과:  
  ```sh  
  2018314827 차승일 - xv6 부팅 완료!  
  init: starting sh  
  ```  

## 개발 환경  
- **서버**: Ji Server  
- **OS**: Unix-like 환경  
- **빌드 및 실행**:  
  ```sh  
  $ git clone https://github.com/mit-pdos/xv6-public.git  
  $ cd xv6-public  
  $ make qemu-nox  
  ```  

## 코드 상세 분석  
### 1. `init.c` 내 주요 코드 흐름  
- `fork()`를 통해 `init` 프로세스가 자식 프로세스를 생성  
- `exec("sh", argv);`를 호출하여 shell 실행  
- 부모 프로세스는 `wait()`를 사용하여 자식 프로세스 종료 대기  
- `printf(1, "init: starting sh\n");`을 수정하여 메시지 출력  

### 2. `exec()`와 `wait()` 동작 분석  
- `exec()`는 새로운 프로그램을 로드하여 실행 (이전 프로세스 상태 삭제)  
- `wait()`는 자식 프로세스 종료를 대기하여 부모가 먼저 종료되지 않도록 방지  

## 제출 방식  
- **보고서**: 코드 수정 내용 및 설명 포함하여 PDF 제출  
- **코드 제출**: Ji Server에서 `submit` 스크립트를 이용  
  ```sh  
  $ make clean  
  $ ~swe3004/bin/submit pa0 xv6-public  
  ```  

## 주의사항  
- 기한 내 제출 필수 (지연 시 감점)  
- 코드 표절 금지 (검사 프로그램 실행됨)  
- 질문은 iCampus의 Q&A 게시판 활용  

## 참고 자료  
- [xv6 공식 저장소](https://github.com/mit-pdos/xv6-public)  
- [xv6 커멘터리](http://csl.skku.edu/uploads/SSE3044S20/book-rev11.pdf)  

