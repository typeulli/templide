#pragma once

#include "../middleend/ir.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace templide::backend {
    // target 하나의 결과 파일을 쓴다. 상대 경로는 base_dir 기준이다. libs_dir에는 html 출력에 넣는 templide.js와 reveal.js가 있다.
    // 실패하면 에러 메시지를 돌려준다. 그 target에서 할 수 없어 빼고 만든 것은 warnings에 넣는다
    std::vector<std::string> write_target(const ir::Document& document, const ir::Target& target, const std::filesystem::path& base_dir,
                                          const std::filesystem::path& libs_dir, std::vector<std::string>& warnings);

    // target이 할 수 없어 빼고 만드는 것. where는 원문 위치(파일:줄:칸)다
    struct Warning {
        std::string where;
        std::string message;
    };

    // pptx는 run(...) 동작과 script를, html과 web은 program(...)과 macro(...) 동작을 뺀다. 원문 순서이고 같은 경고는 한 번만 있다
    std::vector<Warning> target_warnings(const ir::Document& document, const ir::Target& target);

    // ---- backend들이 함께 쓰는 도우미

    // UTF-8 문자열과 경로
    std::filesystem::path utf8_path(const std::string& text);
    std::string display(const std::filesystem::path& path);

    std::optional<std::string> read_file(const std::filesystem::path& file);

    // 비디오, 오디오 파일의 재생 길이(ms). name의 확장자로 형식을 본다 (mp4, m4a, wav, mp3). 알 수 없으면 0
    long long media_duration(const std::string& bytes, const std::string& name);

    // element의 속성 값. 없으면 nullptr
    const ir::Value* find_property(const ir::Element& element, const std::string& name);

    // enum 속성의 값 이름. 없으면 빈 문자열
    std::string enum_member(const ir::Element& element, const std::string& name);

    // 그라데이션 색 정지점의 위치(0 ~ 1). 적지 않은 위치는 앞뒤 사이에 고르게 놓고, 앞 위치보다 작으면 앞 위치로 올린다
    std::vector<double> gradient_stops(const ir::Gradient& gradient);

    // sRGB 채널(0 ~ 1)과 선형광(linear-light) 값. DrawingML의 shade와 PowerPoint의 두 색 그라데이션은 선형광에서 계산한다
    double srgb_to_linear(double channel);
    double linear_to_srgb(double value);

    // PowerPoint은 두 색 그라데이션(0%와 100%의 두 색, 또는 처음과 끝이 같은 색인 세 색)을 선형광 공간에서
    // S자 곡선으로 섞고, 나머지는 sRGB에서 곧게 섞는다. PowerPoint 그림에서 잰 규칙이다
    bool smooth_gradient(const ir::Gradient& gradient);

    // 두 색 그라데이션에서 정지점 사이 ratio(0 ~ 1)의 섞는 비율. 정규분포 누적 곡선(σ = 0.255)이다
    double gradient_curve(double ratio);
}
