#pragma once

#include "json.hpp"

#include <filesystem>
#include <set>
#include <string>
#include <vector>

// pptx(.pptx, .pptm, .ppsx, .potx)를 .tlide로 바꾼다. 편집기의 불러오기 창이 templide --serve를 거쳐 쓴다.
//
// tree는 불러오기 창이 보여 주는 목록이다. 뿌리는 파일이고, 그 아래에 슬라이드 마스터(마스터 도형, 레이아웃과 그 도형)와
// 슬라이드(요소와 그 애니메이션, 배경, 화면 전환, 메모, 검토 메모)가 있다. 노드마다 id가 있고, 고른 노드의 id로 convert를 부른다.
// templide로 바꿀 수 없는 것(표, 차트, SmartArt 등)은 supported가 false다
namespace templide::importer {
    // {tree, slideWidth, slideHeight} 또는 {error}
    nlohmann::json tree(const std::filesystem::path& pptx);

    struct ImportResult {
        std::string error;                 // 비어 있지 않으면 아무것도 쓰지 않았다
        std::filesystem::path tlide;
        std::filesystem::path tasset;      // 그림이나 미디어가 없으면 비어 있다
        std::vector<std::string> warnings; // 빼거나 비슷하게 바꾼 것
        std::vector<std::string> errors;   // 만든 코드를 컴파일하며 찾은 오류 (파일은 썼다)
    };

    // selection의 노드만 output(.tlide)으로 바꾼다. 그림과 미디어는 output과 같은 이름의 .tasset에 넣는다.
    // packages_dir는 만든 코드를 검사할 때 #include <std/stddef>를 찾는 곳이다
    ImportResult convert(const std::filesystem::path& pptx, const std::set<std::string>& selection, const std::filesystem::path& output,
                         const std::filesystem::path& packages_dir);
}
