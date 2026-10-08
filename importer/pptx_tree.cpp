#include "pptx_model.h"
#include "pptx_import.h"
#include "../backend/pptx.h"

#include <functional>

// 개체 틀의 짝, 화면 전환 읽기, 불러오기 창의 목록
namespace templide::importer {
    // ---- 개체 틀

    std::string role_of(const std::string& type) {
        if (type == "title" || type == "ctrTitle") {
            return "title";
        }
        if (type == "subTitle") {
            return "subtitle";
        }
        if (type == "body" || type == "obj") {
            return "body";
        }
        return "";
    }

    std::map<std::string, int> layout_roles(const Layout& layout) {
        std::map<std::string, int> roles;
        for (const auto& shape : layout.shapes) {
            if (shape.object != "placeholder" || !shape.reason.empty()) {
                continue;
            }
            const std::string role = role_of(shape.ph_type);
            if (!role.empty() && !roles.contains(role)) {
                roles[role] = shape.id;
            }
        }
        return roles;
    }

    const Shape* match_placeholder(const std::vector<Shape>& shapes, const Shape& placeholder) {
        if (placeholder.ph_idx && *placeholder.ph_idx != 0) {
            for (const auto& shape : shapes) {
                if (!shape.ph_type.empty() && shape.ph_idx == placeholder.ph_idx) {
                    return &shape;
                }
            }
        }
        for (const auto& shape : shapes) {
            if (!shape.ph_type.empty() && (shape.ph_type == placeholder.ph_type || (role_of(shape.ph_type) == "title" && role_of(placeholder.ph_type) == "title"))) {
                return &shape;
            }
        }
        // 부제목과 내용 개체 틀은 마스터의 본문 개체 틀을 따른다
        const std::string role = role_of(placeholder.ph_type);
        if (!role.empty()) {
            for (const auto& shape : shapes) {
                if (!shape.ph_type.empty() && (role_of(shape.ph_type) == role || (role == "subtitle" && role_of(shape.ph_type) == "body"))) {
                    return &shape;
                }
            }
        }
        return nullptr;
    }

    namespace {
        void collect_connected(const std::vector<Shape>& shapes, std::set<int>& ids) {
            for (const auto& shape : shapes) {
                if (shape.object == "connector") {
                    if (const auto* properties = shape.node->path({"p:nvCxnSpPr", "p:cNvCxnSpPr"})) {
                        for (const char* end : {"a:stCxn", "a:endCxn"}) {
                            if (const auto* connection = properties->child(end)) {
                                ids.insert(static_cast<int>(connection->integer("id").value_or(0)));
                            }
                        }
                    }
                }
                collect_connected(shape.children, ids);
            }
        }
    }

    std::set<int> connected_ids(const std::vector<Shape>& shapes) {
        std::set<int> ids;
        collect_connected(shapes, ids);
        return ids;
    }

    std::string slide_role(const Presentation& presentation, const Slide& slide, const Shape& shape) {
        if (shape.object != "placeholder" || !shape.reason.empty() || !shape.has_text || slide.layout < 0) {
            return "";
        }
        const Layout& layout = presentation.layouts[static_cast<std::size_t>(slide.layout)];
        const Shape* match = match_placeholder(layout.shapes, shape);
        if (match == nullptr) {
            return "";
        }
        const auto roles = layout_roles(layout);
        const std::string role = role_of(match->ph_type);
        if (role.empty() || !roles.contains(role) || roles.at(role) != match->id) {
            return "";
        }
        // 슬라이드에서 위치나 모양을 바꿨으면 글상자로 옮긴다
        if (const auto* properties = shape.node->child("p:spPr")) {
            for (const auto& child : properties->children) {
                if (child.name != "a:extLst") {
                    return "";
                }
            }
        }
        // 애니메이션이나 연결선이 가리키는 개체는 put ... 문장이 있어야 한다
        for (const auto& animation : slide.animations) {
            if (animation.target == shape.id) {
                return "";
            }
        }
        if (connected_ids(slide.shapes).contains(shape.id)) {
            return "";
        }
        // 같은 개체 틀을 앞의 개체 틀이 이미 썼으면 글상자로
        for (const auto& other : slide.shapes) {
            if (&other == &shape) {
                break;
            }
            if (other.object == "placeholder" && other.has_text && match_placeholder(layout.shapes, other) == match) {
                return "";
            }
        }
        return role;
    }

    // ---- 화면 전환

    namespace {
        struct ParsedElement {
            std::string tag;
            std::map<std::string, std::string> attributes;
        };

        // 전환 표의 요소 (<p14:vortex dir="u"/>)
        ParsedElement parse_element(const std::string& text) {
            ParsedElement result;
            std::size_t at = 1;
            while (at < text.size() && text[at] != ' ' && text[at] != '/' && text[at] != '>') {
                result.tag += text[at++];
            }
            while (at < text.size()) {
                while (at < text.size() && (text[at] == ' ' || text[at] == '/' || text[at] == '>')) {
                    ++at;
                }
                const auto equal = text.find('=', at);
                if (equal == std::string::npos) {
                    break;
                }
                const std::string name = text.substr(at, equal - at);
                const auto open = text.find('"', equal);
                const auto close = open == std::string::npos ? std::string::npos : text.find('"', open + 1);
                if (close == std::string::npos) {
                    break;
                }
                result.attributes[name] = text.substr(open + 1, close - open - 1);
                at = close + 1;
            }
            return result;
        }

        // 표에 없는 속성이 기본값인지
        bool default_attribute(const std::string& name, const std::string& value) {
            return value == "0" || value == "false" || (name == "dir" && (value == "l" || value == "out" || value == "horz")) || (name == "orient" && value == "horz")
                || (name == "spokes" && value == "4") || (name == "pattern" && value == "diamond") || (name == "option" && value == "byObject");
        }

        std::pair<std::string, std::string> match_transition(const xml::Node& effect) {
            std::pair<std::string, std::string> best;
            int best_extra = 1000;
            for (const auto& [key, xml] : backend::pptx::transition_table()) {
                const ParsedElement entry = parse_element(xml.element);
                if (entry.tag != effect.name) {
                    continue;
                }
                bool ok = true;
                for (const auto& [name, value] : entry.attributes) {
                    ok = ok && effect.get(name) == value;
                }
                if (!ok) {
                    continue;
                }
                int extra = 0;
                for (const auto& [name, value] : effect.attributes) {
                    if (!entry.attributes.contains(name) && !default_attribute(name, value)) {
                        ++extra;
                    }
                }
                if (extra < best_extra) {
                    best_extra = extra;
                    const auto dot = key.find('.');
                    best = {key.substr(0, dot), dot == std::string::npos ? "" : key.substr(dot + 1)};
                }
            }
            return best;
        }
    }

    TransitionInfo read_transition(const Slide& slide) {
        TransitionInfo info;
        const xml::Node* transition = slide.part->root.child("p:transition");
        for (const auto& child : slide.part->root.children) {
            if (child.name == "mc:AlternateContent") {
                if (const auto* chosen = alternate(child); chosen != nullptr && chosen->name == "p:transition") {
                    transition = chosen;
                }
            }
        }
        if (transition == nullptr) {
            return info;
        }
        if (const auto advance = transition->integer("advTm")) {
            info.advance = advance;
        }
        if (const auto* sound = transition->path({"p:sndAc", "p:stSnd", "p:snd"})) {
            if (const auto* relationship = slide.part->rel(sound->get("r:embed")); relationship != nullptr && !relationship->external) {
                info.sound = relationship->target;
            }
        }
        const xml::Node* effect = nullptr;
        for (const auto& child : transition->children) {
            if (child.name != "p:sndAc" && child.name != "p:extLst") {
                effect = &child;
                break;
            }
        }
        info.present = effect != nullptr || info.advance || !info.sound.empty();
        if (effect == nullptr) {
            return info;
        }
        if (const auto duration = transition->integer("p14:dur")) {
            info.duration = duration;
        } else if (const std::string speed = transition->get("spd"); !speed.empty()) {
            info.duration = speed == "fast" ? 500 : speed == "med" ? 750 : 1000;
        }
        const auto [kind, option] = match_transition(*effect);
        if (kind.empty()) {
            info.reason = "templide가 모르는 화면 전환입니다 (" + effect->name + ")";
        }
        info.kind = kind;
        info.option = option;
        return info;
    }

    // ---- 목록

    namespace {
        using nlohmann::json;

        std::string object_label(const std::string& object) {
            static const std::map<std::string, std::string> names = {
                {"text_box", "글상자"}, {"shape", "도형"}, {"image", "그림"}, {"line", "선"}, {"connector", "연결선"}, {"freeform", "자유형"}, {"group", "그룹"},
                {"placeholder", "개체 틀"}, {"video", "비디오"}, {"audio", "오디오"}, {"table", "표"}, {"chart", "차트"}, {"smartart", "SmartArt"}, {"ole", "OLE 개체"},
                {"ink", "잉크"}, {"equation", "수식"}, {"model3d", "3D 모델"}, {"zoom", "확대/축소"},
            };
            const auto it = names.find(object);
            return it == names.end() ? "개체" : it->second;
        }

        std::string notes_text(const Slide& slide) {
            if (!slide.notes) {
                return "";
            }
            const auto* tree = slide.notes->root.path({"p:cSld", "p:spTree"});
            if (tree == nullptr) {
                return "";
            }
            for (const auto& shape : tree->children) {
                const auto* placeholder = placeholder_of(shape);
                if (placeholder != nullptr && placeholder->get("type", "obj") == "body") {
                    return plain_text(text_body(shape), 40);
                }
            }
            return "";
        }

        struct TreeBuilder {
            const Presentation& presentation;
            // 레이아웃 개체 틀의 노드 id -> 그 개체 틀을 title = ...로 쓰는 슬라이드 개체 id들
            std::map<std::string, std::vector<std::string>> used_by;

            static json animation_node(const Animation& animation, const std::string& id) {
                json node = {{"id", id}, {"kind", "animation"}, {"label", animation.category + "." + animation.effect + (animation.option.empty() ? "" : "." + animation.option)},
                             {"animation", {{"category", animation.category}, {"effect", animation.effect}, {"option", animation.option}, {"start", animation.start}}},
                             {"supported", animation.reason.empty()}};
                if (!animation.reason.empty()) {
                    node["reason"] = animation.reason;
                }
                if (!animation.notes.empty()) {
                    std::string detail;
                    for (const auto& note : animation.notes) {
                        detail += (detail.empty() ? "" : ", ") + note;
                    }
                    node["detail"] = detail;
                }
                return node;
            }

            json element(const Shape& shape, const std::string& prefix, const Slide* slide) {
                json node = {{"id", prefix + "/e" + std::to_string(shape.id)}, {"kind", "element"}, {"object", shape.object},
                             {"label", shape.name.empty() ? object_label(shape.object) : shape.name}, {"supported", shape.reason.empty()}};
                if (!shape.reason.empty()) {
                    node["reason"] = shape.reason;
                }
                if (!shape.excerpt.empty()) {
                    node["detail"] = shape.excerpt; // 종류는 화면이 object로 보인다
                }
                json children = json::array();
                for (const auto& child : shape.children) {
                    if (child.object == "placeholder" && !child.has_text) {
                        continue;
                    }
                    children.push_back(element(child, prefix, slide));
                }
                if (slide != nullptr) {
                    for (std::size_t i = 0; i < slide->animations.size(); ++i) {
                        if (slide->animations[i].target == shape.id) {
                            children.push_back(animation_node(slide->animations[i], prefix + "/a" + std::to_string(i + 1)));
                        }
                    }
                }
                if (!children.empty()) {
                    node["children"] = children;
                }
                return node;
            }

            json layout_node(std::size_t index) {
                const Layout& layout = presentation.layouts[index];
                const std::string id = layout_id(presentation, index);
                const auto roles = layout_roles(layout);
                std::size_t users = 0;
                for (const auto& slide : presentation.slides) {
                    users += slide.layout == static_cast<int>(index) ? 1 : 0;
                }
                json node = {{"id", id}, {"kind", "layout"}, {"label", layout.name.empty() ? "레이아웃" : layout.name}, {"supported", true},
                             {"detail", users == 0 ? std::string("쓰는 슬라이드 없음") : "슬라이드 " + std::to_string(users) + "장이 사용"}};
                json children = json::array();
                for (const auto& shape : layout.shapes) {
                    json child = element(shape, id, nullptr);
                    if (shape.object == "placeholder") {
                        const std::string role = role_of(shape.ph_type);
                        const bool exported = !role.empty() && roles.contains(role) && roles.at(role) == shape.id;
                        if (!exported && shape.reason.empty()) {
                            child["supported"] = false;
                            child["reason"] = shape.ph_type == "dt" || shape.ph_type == "ftr" || shape.ph_type == "sldNum" || shape.ph_type == "hdr"
                                ? "날짜, 바닥글, 슬라이드 번호 개체 틀은 슬라이드에 넣은 것만 가져옵니다"
                                : "templide의 레이아웃은 제목, 부제목, 본문 개체 틀을 하나씩만 가집니다";
                        }
                        if (exported) {
                            static const std::map<std::string, std::string> names = {{"title", "제목"}, {"subtitle", "부제목"}, {"body", "본문"}};
                            child["detail"] = names.at(role) + (shape.excerpt.empty() ? "" : " · " + shape.excerpt);
                            if (const auto users_of = used_by.find(child["id"].get<std::string>()); users_of != used_by.end()) {
                                child["usedBy"] = users_of->second;
                            }
                        }
                    }
                    children.push_back(child);
                }
                if (!children.empty()) {
                    node["children"] = children;
                }
                return node;
            }

            json slide_node(std::size_t index) {
                const Slide& slide = presentation.slides[index];
                const std::string id = slide_id(index);
                std::string detail = slide.title;
                if (slide.hidden) {
                    detail = "숨김" + std::string(detail.empty() ? "" : " · ") + detail;
                }
                json node = {{"id", id}, {"kind", "slide"}, {"label", "슬라이드 " + std::to_string(index + 1)}, {"supported", true}, {"detail", detail},
                             {"requires", json::array({layout_id(presentation, static_cast<std::size_t>(slide.layout))})}};
                std::set<int> ids;
                const std::function<void(const std::vector<Shape>&)> gather = [&](const std::vector<Shape>& shapes) {
                    for (const auto& shape : shapes) {
                        ids.insert(shape.id);
                        gather(shape.children);
                    }
                };
                gather(slide.shapes);
                json children = json::array();
                for (const auto& shape : slide.shapes) {
                    if (shape.object == "placeholder" && !shape.has_text) {
                        continue; // 빈 개체 틀은 슬라이드 쇼에서 보이지 않는다
                    }
                    children.push_back(element(shape, id, &slide));
                }
                for (std::size_t i = 0; i < slide.animations.size(); ++i) {
                    if (!ids.contains(slide.animations[i].target)) {
                        json orphan = animation_node(slide.animations[i], id + "/a" + std::to_string(i + 1));
                        orphan["supported"] = false;
                        orphan["reason"] = "애니메이션의 대상 개체를 찾을 수 없습니다";
                        children.push_back(orphan);
                    }
                }
                if (slide.part->root.path({"p:cSld", "p:bg"}) != nullptr) {
                    children.push_back({{"id", id + "/bg"}, {"kind", "background"}, {"label", "배경"}, {"supported", true}});
                }
                const TransitionInfo transition = read_transition(slide);
                if (transition.present) {
                    json item = {{"id", id + "/tr"}, {"kind", "transition"}, {"label", "화면 전환"}, {"supported", transition.reason.empty() || transition.advance.has_value()}};
                    if (!transition.kind.empty()) {
                        item["detail"] = transition.kind + (transition.option.empty() ? "" : "." + transition.option);
                    }
                    if (!transition.reason.empty()) {
                        item[transition.advance ? "detail" : "reason"] = transition.reason;
                    }
                    children.push_back(item);
                }
                if (const std::string text = notes_text(slide); !text.empty()) {
                    children.push_back({{"id", id + "/notes"}, {"kind", "notes"}, {"label", "발표자 메모"}, {"detail", text}, {"supported", true}});
                }
                if (!slide.reviews.empty()) {
                    children.push_back({{"id", id + "/rv"}, {"kind", "comments"}, {"label", "검토 메모"}, {"detail", std::to_string(slide.reviews.size()) + "개"}, {"supported", true}});
                }
                if (!children.empty()) {
                    node["children"] = children;
                }
                return node;
            }

            json build() {
                for (std::size_t i = 0; i < presentation.slides.size(); ++i) {
                    const Slide& slide = presentation.slides[i];
                    const Layout& layout = presentation.layouts[static_cast<std::size_t>(slide.layout)];
                    for (const auto& shape : slide.shapes) {
                        if (slide_role(presentation, slide, shape).empty()) {
                            continue;
                        }
                        const Shape* match = match_placeholder(layout.shapes, shape);
                        used_by[layout_id(presentation, static_cast<std::size_t>(slide.layout)) + "/e" + std::to_string(match->id)].push_back(slide_id(i) + "/e" + std::to_string(shape.id));
                    }
                }
                json masters = json::array();
                for (std::size_t m = 0; m < presentation.masters.size(); ++m) {
                    const Master& master = presentation.masters[m];
                    json node = {{"id", master_id(m)}, {"kind", "master"}, {"label", master.name.empty() ? "슬라이드 마스터" : master.name}, {"supported", true},
                                 {"detail", "레이아웃 " + std::to_string(master.layouts.size()) + "개"}};
                    json children = json::array();
                    for (const auto& shape : master.shapes) {
                        if (shape.object != "placeholder") {
                            children.push_back(element(shape, master_id(m), nullptr));
                        }
                    }
                    for (const int layout : master.layouts) {
                        children.push_back(layout_node(static_cast<std::size_t>(layout)));
                    }
                    node["children"] = children;
                    masters.push_back(node);
                }
                json slides = json::array();
                for (std::size_t i = 0; i < presentation.slides.size(); ++i) {
                    slides.push_back(slide_node(i));
                }
                const std::u8string name = presentation.file.filename().u8string();
                return {{"id", "file"}, {"kind", "file"}, {"label", std::string(name.begin(), name.end())}, {"supported", true},
                        {"detail", "슬라이드 " + std::to_string(presentation.slides.size()) + "장"},
                        {"children", json::array({
                            {{"id", "masters"}, {"kind", "masters"}, {"label", "슬라이드 마스터"}, {"supported", true}, {"children", masters}},
                            {{"id", "slides"}, {"kind", "slides"}, {"label", "슬라이드"}, {"supported", true}, {"children", slides}},
                        })}};
            }
        };
    }

    nlohmann::json tree(const std::filesystem::path& pptx) {
        std::string error;
        const auto presentation = load(pptx, error);
        if (!presentation) {
            return {{"error", error}};
        }
        TreeBuilder builder{*presentation, {}};
        return {{"tree", builder.build()}, {"slideWidth", static_cast<double>(presentation->width) / emu_per_px},
                {"slideHeight", static_cast<double>(presentation->height) / emu_per_px}};
    }
}
