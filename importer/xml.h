#pragma once

#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// pptx 안의 XML을 읽는 작은 DOM 파서. 요소 이름의 namespace 접두사는 namespace 주소를 보고 PowerPoint이 쓰는 이름(p, a, r, p14 등)으로 바꾼다.
// 그래서 다른 프로그램이 다른 접두사로 저장한 파일도 같은 이름으로 찾을 수 있다
namespace templide::importer::xml {
    struct Node {
        std::string name; // p:sp처럼 접두사가 붙은 이름. 기본 namespace의 요소는 접두사가 없다
        std::vector<std::pair<std::string, std::string>> attributes;
        std::vector<Node> children;
        std::string text; // 바로 안의 글자 (자식 요소의 글자는 빠진다)

        // 이름이 name인 첫 자식. 없으면 nullptr
        const Node* child(std::string_view name) const;
        // 이름이 name인 자식들
        std::vector<const Node*> all(std::string_view name) const;
        // 자식, 그 자식의 자식...을 차례로 따라간다. 하나라도 없으면 nullptr
        const Node* path(std::initializer_list<std::string_view> names) const;
        // 이름이 name인 첫 자손 (깊이 우선)
        const Node* find(std::string_view name) const;

        const std::string* attribute(std::string_view name) const;
        std::string get(std::string_view name, const std::string& fallback = "") const;
        std::optional<long long> integer(std::string_view name) const;
        bool flag(std::string_view name, bool fallback = false) const; // "1", "true"면 참
    };

    // 문서의 뿌리 요소. 잘못된 XML이면 nullopt이고 error에 까닭이 있다
    std::optional<Node> parse(const std::string& text, std::string& error);
}
