#!/usr/bin/env bash
# 빌드 스크립트 (Git Bash / MSYS2에서 실행)
#   ./build.sh          컴파일
#   ./build.sh run      컴파일 후 실행
#   ./build.sh clean    산출물 삭제
set -e

# --- 컴파일러 찾기 -----------------------------------------------------
# MSYS2의 g++는 자기 DLL을 같은 폴더에서 찾기 때문에, 이 경로가 PATH에
# 없으면 cc1plus가 조용히 실패한다(에러 메시지 없이 종료 코드 1).
for d in /c/msys64/ucrt64/bin /c/msys64/mingw64/bin; do
    [ -d "$d" ] && export PATH="$d:$PATH"
done

if ! command -v g++ >/dev/null 2>&1; then
    echo "g++를 찾을 수 없습니다. MSYS2 설치 경로를 확인하세요." >&2
    exit 1
fi

# --- 설정 -------------------------------------------------------------
CXX=g++
CXXFLAGS="-std=c++14 -Wall -Wextra -I."
OUT=bin
SRCS="format.c++ domains.c++ user.c++ view.c++ input.c++"
# intitial.c++는 아직 빈 파일이라 제외한다.

if [ "$1" = "clean" ]; then
    rm -rf "$OUT"
    echo "삭제 완료: $OUT/"
    exit 0
fi

mkdir -p "$OUT"

# --- 빌드 -------------------------------------------------------------
if [ -f main.c++ ]; then
    echo "빌드 중... ($($CXX --version | head -1))"
    $CXX $CXXFLAGS -o "$OUT/app.exe" main.c++ $SRCS
    echo "완료: $OUT/app.exe"
    [ "$1" = "run" ] && { echo "----"; "$OUT/app.exe"; }
else
    # main.c++이 없으면 실행 파일을 만들 수 없으므로 문법 검사만 한다.
    echo "main.c++이 없어 문법 검사만 수행합니다. ($($CXX --version | head -1))"
    $CXX $CXXFLAGS -fsyntax-only $SRCS
    echo "문법 검사 통과 (경고 없음)"
fi
