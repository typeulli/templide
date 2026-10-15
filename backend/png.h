#pragma once

#include "../middleend/ir.h"

#include <filesystem>
#include <string>
#include <vector>

/**
 * 렌더링한 슬라이드를 PNG 파일로 저장한다.
 * 숨기지 않은 슬라이드마다 파일 하나를 만들며, 전환과 애니메이션은 적용하지 않는다.
 * @param target PNG 출력 설정
 * @param base_dir 상대 경로의 기준 디렉터리
 * @param libs_dir 렌더링에 필요한 라이브러리 디렉터리
 * @param dry_run true이면 파일과 브라우저를 만들지 않고 오류만 검사한다.
 */
namespace templide::backend::png {
    std::vector<std::string> write(const ir::Document& document, const ir::Target& target, const std::filesystem::path& base_dir,
                                   const std::filesystem::path& libs_dir, bool dry_run = false);

    /** n번째 슬라이드의 출력 경로를 만든다. 번호는 1부터 센다. */
    std::filesystem::path numbered_path(const std::filesystem::path& path, std::size_t number);
}
