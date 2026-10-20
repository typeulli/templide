#pragma once

#include "diagnostic.h"
#include "ir.h"

#include <string>
#include <utility>
#include <vector>

// 접근성 검사. 분석이 끝난 문서(IR)를 보고 화면 낭독기와 색을 잘 보지 못하는 사람에게 문제가 될 것들을 찾는다.
// 모든 결과는 A로 시작하는 코드가 있는 진단이고, 심각한 정도는 Level로 나뉜다 (오류: 대체 텍스트 없음, 경고: 낮은 명암비 등, 정보: 참고).
// sources는 분석기가 읽은 파일의 (경로, 글)이며 원문 위치를 줄과 칸으로 바꾸는 데 쓴다
namespace templide::middleend {
    std::vector<Diagnostic> check_accessibility(const ir::Document& document, const std::vector<std::pair<std::string, std::string>>& sources);
}
