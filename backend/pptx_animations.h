#pragma once

#include <map>
#include <string>

namespace templide::backend::pptx {
    struct AnimationPreset {
        long long duration;      // PowerPoint의 기본 길이(ms). -1이면 끝나는 시간이 없는 효과다
        bool builds;             // bldLst에 bldP를 써야 하는지
        bool animate_background; // bldP의 animBg
        const char* xml;
    };

    // "종류.효과" 또는 "종류.효과.옵션" -> 효과 노드
    const std::map<std::string, AnimationPreset>& animation_presets();
}
