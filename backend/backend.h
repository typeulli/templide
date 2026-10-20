#pragma once

#include "../middleend/diagnostic.h"
#include "../middleend/ir.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace templide::backend {
    /** target이 지원하지 않아 생략하는 기능에 대한 경고. */
    struct Warning {
        std::string where; // 파일:줄:칸
        std::string message;
        std::string code;  // 진단 코드 (middleend/diagnostic_catalog.cpp)
    };

    /** 경고를 진단으로. where가 "파일:줄:칸" 모양이 아니면 위치가 없는 진단이다. */
    middleend::Diagnostic to_diagnostic(const Warning& warning);

    /** target 하나를 출력한다. 실패한 작업은 반환값에, 지원되지 않아 생략한 작업은 warnings에 기록한다. */
    std::vector<std::string> write_target(const ir::Document& document, const ir::Target& target, const std::filesystem::path& base_dir,
                                          const std::filesystem::path& libs_dir, std::vector<Warning>& warnings, bool dry_run = false);

    /** target에서 지원되지 않는 기능의 경고를 원문 순서로 반환한다. */
    std::vector<Warning> target_warnings(const ir::Document& document, const ir::Target& target);

    // ---- backend들이 함께 쓰는 도우미

    // UTF-8 문자열과 경로
    std::filesystem::path utf8_path(const std::string& text);
    std::string display(const std::filesystem::path& path);

    // 파일 내용. 'x.tasset/이름'처럼 묶음(.tasset) 안의 파일을 가리키면 묶음에서 읽는다
    std::optional<std::string> read_file(const std::filesystem::path& file);

    // 디스크에 있는 파일의 경로. 묶음 안의 파일이면 임시 폴더에 풀어 그 경로를 준다. 없으면 nullopt
    std::optional<std::filesystem::path> disk_file(const std::filesystem::path& file);

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
