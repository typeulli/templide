#pragma once

#include "diagnostic.h"

#include <cstddef>
#include <string>
#include <vector>

namespace templide::middleend {
    // --check 같은 CLI가 진단을 보여 주는 형식
    enum class ReportFormat { Text, Json, Github };

    std::optional<ReportFormat> report_format_from_name(std::string_view name); // "text", "json", "github"

    // "파일:줄:칸: error[E2001]: 메시지". 고치는 방법이 있으면 다음 줄에 "  help: ..."를 붙인다. 끝 줄바꿈은 없다.
    // 위치가 없으면(줄이 0) "파일: error[...]: 메시지", 파일도 없으면 "error[...]: 메시지"다. 코드가 없으면 "[...]"를 뺀다
    std::string format_text(const Diagnostic& diagnostic);

    // GitHub Actions의 워크플로 명령. "::error file=...,line=...,col=...,title=E2001::메시지"
    std::string format_github(const Diagnostic& diagnostic);

    // 진단 하나를 담은 JSON 개체의 글. 줄과 칸은 위치가 있을 때만 있다
    std::string format_json_entry(const Diagnostic& diagnostic);

    // {"version":1,"diagnostics":[...],"summary":{"errors":n,"warnings":n,"infos":n}}
    std::string format_json(const std::vector<Diagnostic>& diagnostics);
}
