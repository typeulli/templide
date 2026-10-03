#pragma once

#include "../middleend/ir.h"

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <vector>

// 편집기가 화면에서 바꾼 값을 원문 수정으로 바꾼다. 원문은 고친 곳만 잘라 바꾸므로 주석과 줄 맞춤이 남는다
namespace templide::server {
    // 원문 한 곳을 바꾸는 것. begin, end는 바이트 위치이고 같으면 끼워 넣기다
    struct TextEdit {
        std::string path;
        std::size_t begin;
        std::size_t end;
        std::string text;
    };

    // 편집 결과. 바꿀 수 없으면 edits가 비어 있고 error에 이유가 있다
    struct EditResult {
        std::vector<TextEdit> edits;
        std::string error;
    };

    // 새 값. LENGTH는 px로 받아 원문에 적힌 단위로 바꿔 쓰고, NUMBER는 원문의 단위를 그대로 붙인다. CODE는 편집기가 만든 식을 그대로 쓴다
    struct NewValue {
        enum class Kind { LENGTH, NUMBER, STRING, COLOR, NAME, BOOL, CODE } kind;
        double number = 0;
        std::string text; // STRING의 내용, COLOR의 "#RRGGBB" 또는 "#RRGGBBAA", NAME의 enum 값 이름, CODE의 식
        bool flag = false;
        std::string unit; // NUMBER를 새로 쓸 때 붙일 단위 (pt 등). 리터럴을 바꿀 때는 원래 단위를 쓴다
        bool unset = false; // 서식을 끈다. 감싼 인라인 style에 이 속성이 있으면 지우고, 없으면 이 값을 넣는다
    };

    struct SlideSize {
        double width;
        double height;
    };

    // 파일 경로 -> 내용. Origin의 range가 가리키는 원문이다
    using Sources = std::map<std::string, std::string>;

    class Editor {
    public:
        Editor(const Sources& sources, SlideSize size) : sources_(sources), size_(size) {}

        // origin이 가리키는 값을 value로 바꾼다. property는 %를 슬라이드의 가로와 세로 중 무엇에 대해 풀지 정한다
        EditResult set(const ir::Origin& origin, const std::string& property, const NewValue& value) const;
        // element의 속성 하나. element에 없는 속성은 put 블록에 넣는다
        EditResult set_property(const ir::Element& element, const std::string& property, const NewValue& value) const;
        // element를 dx, dy(px)만큼 옮긴다. template이 만든 element는 template을 넣은 put의 x, y를 바꾼다
        EditResult move(const ir::Element& element, double dx, double dy) const;
        // 문자열 리터럴로 적은 run의 글자
        EditResult set_text(const ir::Run& run, const std::string& text) const;
        // 문자열 리터럴로 적은 run의 [begin, end) 바이트를 (style(...) "...")로 감싼다
        EditResult style(const ir::Run& run, std::size_t begin, std::size_t end, const std::vector<std::pair<std::string, NewValue>>& properties) const;
        // run을 감싼 (style(...) "글자")를 풀어 리터럴만 남긴다
        EditResult unstyle(const ir::Run& run) const;
        // element를 만든 put 문장의 그리는 순서. direction은 forward, backward, front, back
        EditResult reorder(const std::vector<ir::Element>& siblings, const ir::Element& element, const std::string& direction) const;
        // slide 문장 다루기. slides는 문서의 모든 slide, page는 1부터. main은 slide가 없을 때 새 slide를 넣을 파일이다
        EditResult add_slide(const std::vector<ir::Slide>& slides, std::size_t after, const std::string& main) const;
        EditResult remove_slide(const ir::Slide& slide) const;
        EditResult duplicate_slide(const ir::Slide& slide) const;
        EditResult move_slide(const std::vector<ir::Slide>& slides, std::size_t page, int step) const;
        // page의 slide를 빼서 to번째 자리에 둔다 (둘 다 1부터)
        EditResult move_slide_to(const std::vector<ir::Slide>& slides, std::size_t page, std::size_t to) const;
        // element를 만든 put 문장을 slide 블록 끝에 하나 더 넣는다. 그 문장에 적힌 x, y 리터럴은 dx, dy(px)만큼 옮긴다
        EditResult copy(const ir::Element& element, const ir::Slide& slide, double dx, double dy) const;
        // 잘라 낸 문장의 원문을 slide 블록 끝에 넣는다
        EditResult paste(const ir::Slide& slide, const std::string& statement) const;
        // slide 블록 끝에 put object { ... }를 넣는다
        EditResult insert(const ir::Slide& slide, const std::string& object, const std::vector<std::pair<std::string, NewValue>>& properties) const;
        // element를 만든 put 문장을 지운다. template이 만든 element는 그 template을 넣은 put 전체를 지운다
        EditResult remove(const ir::Element& element) const;
        // 'name = 값;' 대입을 지워 기본값으로 되돌린다. origin은 그 값의 출처(LITERAL이고 statement가 있어야 한다)
        EditResult unset(const ir::Origin& origin) const;
        // element의 속성 하나를 지운다. 적지 않은 속성이면 할 일이 없다
        EditResult unset_property(const ir::Element& element, const std::string& property) const;
        // slide, target 블록의 속성. origins에 없으면 block에 넣는다
        EditResult set_named(const std::map<std::string, ir::Origin>& origins, const ir::Origin& block, const std::string& name, const NewValue& value) const;
        EditResult unset_named(const std::map<std::string, ir::Origin>& origins, const std::string& name) const;
        // 문장 하나(BLOCK)를 text로 바꾸거나 지운다
        EditResult replace_statement(const ir::Origin& statement, const std::string& text) const;
        EditResult remove_statement(const ir::Origin& statement) const;
        // 블록(BLOCK)의 닫는 '}' 앞에 문장을 넣는다
        EditResult insert_into(const ir::Origin& block, const std::string& statement) const;
        // element를 만든 put(또는 group) 문장에 'as name'을 붙이거나 바꾼다. template이 만든 element는 바깥쪽 put이다
        EditResult rename(const ir::Element& element, const std::string& name) const;
        // 원문 내용. 없으면 nullptr
        const std::string* source_text(const std::string& path) const { return text_of(path); }

    private:
        const Sources& sources_;
        SlideSize size_;

        const std::string* text_of(const std::string& path) const;
        std::string code(const ir::Origin& origin, const std::string& property, const NewValue& value, std::string& error) const;
        EditResult insert_statement(const ir::Origin& block, const std::string& statement) const;
        EditResult merge_style(const ir::Origin& origin, const std::vector<std::pair<std::string, NewValue>>& properties) const;
        // range가 줄 전체를 차지하면 그 줄들(끝의 줄바꿈 포함)
        std::optional<std::pair<std::size_t, std::size_t>> whole_lines(const ir::SourceRange& range) const;
        // a와 b의 원문을 서로 바꾼다
        EditResult swap(const ir::SourceRange& a, const ir::SourceRange& b) const;
        // statement를 지우고 target 앞이나 뒤의 줄로 옮긴다
        EditResult move_lines(const ir::SourceRange& statement, const ir::SourceRange& target, bool after) const;
    };

    // 문자열 리터럴. 따옴표와 이스케이프를 붙인다
    std::string quote_string(const std::string& text);

    // 잠긴 이유(Origin::reason)를 사람이 읽는 말로
    std::string lock_message(const std::string& reason);
}
