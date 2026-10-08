#include "pptx_model.h"
#include "../backend/pptx_animations.h"

#include <miniz.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <set>
#include <sstream>

namespace templide::importer {
    namespace {
        std::FILE* open_file(const std::filesystem::path& path, const char* mode) {
#ifdef _WIN32
            const std::string narrow(mode);
            return _wfopen(path.c_str(), std::wstring(narrow.begin(), narrow.end()).c_str());
#else
            return std::fopen(path.c_str(), mode);
#endif
        }

        std::string lower(std::string text) {
            std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return text;
        }

        std::string percent_decode(const std::string& text) {
            std::string result;
            for (std::size_t i = 0; i < text.size(); ++i) {
                if (text[i] == '%' && i + 2 < text.size() && std::isxdigit(static_cast<unsigned char>(text[i + 1])) && std::isxdigit(static_cast<unsigned char>(text[i + 2]))) {
                    result += static_cast<char>(std::stoi(text.substr(i + 1, 2), nullptr, 16));
                    i += 2;
                } else {
                    result += text[i];
                }
            }
            return result;
        }

        std::string display(const std::filesystem::path& path) {
            const std::u8string text = path.u8string();
            return std::string(text.begin(), text.end());
        }

        // UTF-8 글에 한글이 있는지
        bool has_hangul(const std::string& text) {
            for (std::size_t i = 0; i + 2 < text.size(); ++i) {
                const auto lead = static_cast<unsigned char>(text[i]);
                if (lead == 0xEA || lead == 0xEB || lead == 0xEC || (lead == 0xED && static_cast<unsigned char>(text[i + 1]) < 0x9E)) {
                    return true; // U+AC00 ~ U+D7A3
                }
            }
            return false;
        }
    }

    // ---- 파트

    const Relationship* Part::rel(const std::string& id) const {
        const auto it = rels.find(id);
        return it == rels.end() ? nullptr : &it->second;
    }

    const Relationship* Part::first(const std::string& type) const {
        for (const auto& [id, relationship] : rels) {
            if (relationship.type == type) {
                return &relationship;
            }
        }
        return nullptr;
    }

    std::string resolve_target(const std::string& part, const std::string& target) {
        std::string path = percent_decode(target);
        std::vector<std::string> pieces;
        if (!path.empty() && path.front() == '/') {
            path.erase(path.begin());
        } else {
            const auto slash = part.find_last_of('/');
            const std::string folder = slash == std::string::npos ? "" : part.substr(0, slash);
            std::stringstream stream(folder);
            for (std::string piece; std::getline(stream, piece, '/');) {
                if (!piece.empty()) {
                    pieces.push_back(piece);
                }
            }
        }
        std::stringstream stream(path);
        for (std::string piece; std::getline(stream, piece, '/');) {
            if (piece.empty() || piece == ".") {
                continue;
            }
            if (piece == "..") {
                if (!pieces.empty()) {
                    pieces.pop_back();
                }
                continue;
            }
            pieces.push_back(piece);
        }
        std::string result;
        for (const auto& piece : pieces) {
            result += (result.empty() ? "" : "/") + piece;
        }
        return result;
    }

    struct Package::State {
        std::FILE* file = nullptr;
        mz_zip_archive zip{};
        bool open = false;
        std::map<std::string, mz_uint> names; // 소문자 이름 -> 번호 (OPC의 파트 이름은 대소문자를 가리지 않는다)
    };

    Package::~Package() {
        if (state_ != nullptr) {
            if (state_->open) {
                mz_zip_reader_end(&state_->zip);
            }
            if (state_->file != nullptr) {
                std::fclose(state_->file);
            }
            delete state_;
        }
    }

    bool Package::open(const std::filesystem::path& file, std::string& error) {
        state_ = new State;
        std::error_code code;
        const auto size = std::filesystem::file_size(file, code);
        if (code) {
            error = "파일을 열 수 없습니다: " + display(file);
            return false;
        }
        state_->file = open_file(file, "rb");
        if (state_->file == nullptr) {
            error = "파일을 열 수 없습니다: " + display(file);
            return false;
        }
        if (!mz_zip_reader_init_cfile(&state_->zip, state_->file, size, 0)) {
            error = "PowerPoint 파일(pptx)이 아닙니다: " + display(file);
            return false;
        }
        state_->open = true;
        const mz_uint count = mz_zip_reader_get_num_files(&state_->zip);
        for (mz_uint i = 0; i < count; ++i) {
            mz_zip_archive_file_stat stat;
            if (mz_zip_reader_file_stat(&state_->zip, i, &stat)) {
                state_->names.emplace(lower(stat.m_filename), i);
            }
        }
        return true;
    }

    bool Package::exists(const std::string& name) const {
        return state_ != nullptr && state_->names.contains(lower(name));
    }

    std::optional<std::string> Package::read(const std::string& name) const {
        if (state_ == nullptr || !state_->open) {
            return std::nullopt;
        }
        const auto it = state_->names.find(lower(name));
        if (it == state_->names.end()) {
            return std::nullopt;
        }
        mz_zip_archive_file_stat stat;
        if (!mz_zip_reader_file_stat(&state_->zip, it->second, &stat)) {
            return std::nullopt;
        }
        std::string bytes(static_cast<std::size_t>(stat.m_uncomp_size), '\0');
        if (!mz_zip_reader_extract_to_mem(&state_->zip, it->second, bytes.data(), bytes.size(), 0)) {
            return std::nullopt;
        }
        return bytes;
    }

    std::unique_ptr<Part> Package::part(const std::string& name) const {
        const auto text = read(name);
        if (!text) {
            return nullptr;
        }
        std::string error;
        auto root = xml::parse(*text, error);
        if (!root) {
            return nullptr;
        }
        auto result = std::make_unique<Part>();
        result->name = name;
        result->root = std::move(*root);
        const auto slash = name.find_last_of('/');
        const std::string rels_name = (slash == std::string::npos ? "" : name.substr(0, slash + 1)) + "_rels/" + name.substr(slash == std::string::npos ? 0 : slash + 1) + ".rels";
        if (const auto rels_text = read(rels_name)) {
            if (auto rels = xml::parse(*rels_text, error)) {
                for (const auto* relationship : rels->all("Relationship")) {
                    std::string type = relationship->get("Type");
                    type = type.substr(type.find_last_of('/') + 1);
                    const bool external = relationship->get("TargetMode") == "External";
                    const std::string target = relationship->get("Target");
                    result->rels[relationship->get("Id")] = {type, external ? target : resolve_target(name, target), external};
                }
            }
        }
        return result;
    }

    // ---- 도우미

    const xml::Node* alternate(const xml::Node& node) {
        static const std::set<std::string> known = {"p14", "p15", "p159", "p188", "a14", "a16", "asvg", "v", "pc"};
        for (const auto* choice : node.all("mc:Choice")) {
            std::stringstream required(choice->get("Requires"));
            bool ok = true;
            for (std::string prefix; required >> prefix;) {
                ok = ok && known.contains(prefix);
            }
            if (ok && !choice->children.empty()) {
                return &choice->children.front();
            }
        }
        if (const auto* fallback = node.child("mc:Fallback"); fallback != nullptr && !fallback->children.empty()) {
            return &fallback->children.front();
        }
        return nullptr;
    }

    namespace {
        const xml::Node* non_visual(const xml::Node& shape) {
            for (const char* name : {"p:nvSpPr", "p:nvPicPr", "p:nvCxnSpPr", "p:nvGrpSpPr", "p:nvGraphicFramePr", "p:nvContentPartPr"}) {
                if (const auto* found = shape.child(name)) {
                    return found;
                }
            }
            return nullptr;
        }
    }

    const xml::Node* placeholder_of(const xml::Node& shape) {
        const auto* nv = non_visual(shape);
        return nv != nullptr ? nv->path({"p:nvPr", "p:ph"}) : nullptr;
    }

    const xml::Node* shape_properties(const xml::Node& shape) {
        if (const auto* found = shape.child("p:spPr")) {
            return found;
        }
        return shape.child("p:grpSpPr");
    }

    const xml::Node* text_body(const xml::Node& shape) {
        return shape.child("p:txBody");
    }

    namespace {
        // 문단 하나의 글. 줄바꿈은 공백으로
        void paragraph_text(const xml::Node& paragraph, std::string& out) {
            for (const auto& child : paragraph.children) {
                if (child.name == "a:r" || child.name == "a:fld") {
                    if (const auto* t = child.child("a:t")) {
                        out += t->text;
                    }
                } else if (child.name == "a:br") {
                    out += ' ';
                } else if (child.name == "mc:AlternateContent") {
                    if (const auto* fallback = child.child("mc:Fallback")) {
                        paragraph_text(*fallback, out);
                    }
                }
            }
        }

        // UTF-8 글자 단위로 자른다
        std::string cut(const std::string& text, std::size_t limit) {
            std::size_t count = 0;
            for (std::size_t i = 0; i < text.size(); ++count) {
                if (count == limit) {
                    return text.substr(0, i) + "…";
                }
                const auto lead = static_cast<unsigned char>(text[i]);
                i += lead < 0x80 ? 1 : lead < 0xE0 ? 2 : lead < 0xF0 ? 3 : 4;
            }
            return text;
        }
    }

    std::string plain_text(const xml::Node* body, std::size_t limit) {
        if (body == nullptr) {
            return "";
        }
        std::string text;
        for (const auto* paragraph : body->all("a:p")) {
            std::string line;
            paragraph_text(*paragraph, line);
            if (!line.empty()) {
                text += (text.empty() ? "" : " / ") + line;
            }
        }
        return cut(text, limit);
    }

    std::string master_id(std::size_t master) {
        return "m" + std::to_string(master + 1);
    }

    std::string layout_id(const Presentation& presentation, std::size_t layout) {
        const Layout& item = presentation.layouts[layout];
        const auto& list = presentation.masters[static_cast<std::size_t>(item.master)].layouts;
        const auto position = std::find(list.begin(), list.end(), static_cast<int>(layout)) - list.begin();
        return master_id(static_cast<std::size_t>(item.master)) + "/l" + std::to_string(position + 1);
    }

    std::string slide_id(std::size_t slide) {
        return "s" + std::to_string(slide + 1);
    }

    // ---- 읽기

    namespace {
        Color rgb_of(const std::string& hex) {
            Color color;
            if (hex.size() >= 6) {
                try {
                    color.r = std::stoi(hex.substr(0, 2), nullptr, 16);
                    color.g = std::stoi(hex.substr(2, 2), nullptr, 16);
                    color.b = std::stoi(hex.substr(4, 2), nullptr, 16);
                } catch (...) {
                }
            }
            return color;
        }

        void read_theme(Theme& theme, std::unique_ptr<Part> part) {
            theme.part = std::move(part);
            if (!theme.part) {
                return;
            }
            const xml::Node& root = theme.part->root;
            theme.name = root.get("name");
            const auto* elements = root.child("a:themeElements");
            if (elements == nullptr) {
                return;
            }
            if (const auto* scheme = elements->child("a:clrScheme")) {
                for (const auto& child : scheme->children) {
                    const std::string key = child.name.substr(child.name.find(':') + 1);
                    if (const auto* srgb = child.child("a:srgbClr")) {
                        theme.colors[key] = rgb_of(srgb->get("val"));
                    } else if (const auto* system = child.child("a:sysClr")) {
                        theme.colors[key] = rgb_of(system->get("lastClr", system->get("val") == "window" ? "FFFFFF" : "000000"));
                    }
                }
            }
            if (const auto* fonts = elements->child("a:fontScheme")) {
                if (const auto* major = fonts->child("a:majorFont")) {
                    theme.major_latin = major->child("a:latin") != nullptr ? major->child("a:latin")->get("typeface") : "";
                    theme.major_ea = major->child("a:ea") != nullptr ? major->child("a:ea")->get("typeface") : "";
                    // ea가 비어 있으면 한글 글꼴(Hang)을 쓴다
                    for (const auto* font : major->all("a:font")) {
                        if (theme.major_ea.empty() && font->get("script") == "Hang") {
                            theme.major_ea = font->get("typeface");
                        }
                    }
                }
                if (const auto* minor = fonts->child("a:minorFont")) {
                    theme.minor_latin = minor->child("a:latin") != nullptr ? minor->child("a:latin")->get("typeface") : "";
                    theme.minor_ea = minor->child("a:ea") != nullptr ? minor->child("a:ea")->get("typeface") : "";
                    for (const auto* font : minor->all("a:font")) {
                        if (theme.minor_ea.empty() && font->get("script") == "Hang") {
                            theme.minor_ea = font->get("typeface");
                        }
                    }
                }
            }
            if (const auto* formats = elements->child("a:fmtScheme")) {
                const auto collect = [](const xml::Node* list, std::vector<const xml::Node*>& out) {
                    if (list != nullptr) {
                        for (const auto& child : list->children) {
                            out.push_back(&child);
                        }
                    }
                };
                collect(formats->child("a:fillStyleLst"), theme.fills);
                collect(formats->child("a:lnStyleLst"), theme.lines);
                collect(formats->child("a:effectStyleLst"), theme.effects);
                collect(formats->child("a:bgFillStyleLst"), theme.backgrounds);
            }
        }

        bool supported_image(const std::string& target) {
            const std::string name = lower(target);
            for (const char* extension : {".png", ".jpg", ".jpeg", ".gif", ".bmp"}) {
                if (name.ends_with(extension)) {
                    return true;
                }
            }
            return false;
        }

        std::string extension_of(const std::string& target) {
            const auto dot = target.find_last_of('.');
            return dot == std::string::npos ? "" : lower(target.substr(dot));
        }

        // 알고 있는 도형 모양 (stddef의 shape_kind)
        const std::set<std::string>& shape_kinds() {
            static const std::set<std::string> kinds = {
                "accentBorderCallout1", "accentBorderCallout2", "accentBorderCallout3", "accentCallout1", "accentCallout2", "accentCallout3", "actionButtonBackPrevious",
                "actionButtonBeginning", "actionButtonBlank", "actionButtonDocument", "actionButtonEnd", "actionButtonForwardNext", "actionButtonHelp", "actionButtonHome",
                "actionButtonInformation", "actionButtonMovie", "actionButtonReturn", "actionButtonSound", "arc", "bentArrow", "bentConnector2", "bentConnector3",
                "bentConnector4", "bentConnector5", "bentUpArrow", "bevel", "blockArc", "borderCallout1", "borderCallout2", "borderCallout3", "bracePair", "bracketPair",
                "callout1", "callout2", "callout3", "can", "chartPlus", "chartStar", "chartX", "chevron", "chord", "circularArrow", "cloud", "cloudCallout", "corner",
                "cornerTabs", "cube", "curvedConnector2", "curvedConnector3", "curvedConnector4", "curvedConnector5", "curvedDownArrow", "curvedLeftArrow",
                "curvedRightArrow", "curvedUpArrow", "decagon", "diagStripe", "diamond", "dodecagon", "donut", "doubleWave", "downArrow", "downArrowCallout", "ellipse",
                "ellipseRibbon", "ellipseRibbon2", "flowChartAlternateProcess", "flowChartCollate", "flowChartConnector", "flowChartDecision", "flowChartDelay",
                "flowChartDisplay", "flowChartDocument", "flowChartExtract", "flowChartInputOutput", "flowChartInternalStorage", "flowChartMagneticDisk",
                "flowChartMagneticDrum", "flowChartMagneticTape", "flowChartManualInput", "flowChartManualOperation", "flowChartMerge", "flowChartMultidocument",
                "flowChartOfflineStorage", "flowChartOffpageConnector", "flowChartOnlineStorage", "flowChartOr", "flowChartPredefinedProcess", "flowChartPreparation",
                "flowChartProcess", "flowChartPunchedCard", "flowChartPunchedTape", "flowChartSort", "flowChartSummingJunction", "flowChartTerminator", "foldedCorner",
                "frame", "funnel", "gear6", "gear9", "halfFrame", "heart", "heptagon", "hexagon", "homePlate", "horizontalScroll", "irregularSeal1", "irregularSeal2",
                "leftArrow", "leftArrowCallout", "leftBrace", "leftBracket", "leftCircularArrow", "leftRightArrow", "leftRightArrowCallout", "leftRightCircularArrow",
                "leftRightRibbon", "leftRightUpArrow", "leftUpArrow", "lightningBolt", "line", "lineInv", "mathDivide", "mathEqual", "mathMinus", "mathMultiply",
                "mathNotEqual", "mathPlus", "moon", "nonIsoscelesTrapezoid", "noSmoking", "notchedRightArrow", "octagon", "parallelogram", "pentagon", "pie", "pieWedge",
                "plaque", "plaqueTabs", "plus", "quadArrow", "quadArrowCallout", "rect", "ribbon", "ribbon2", "rightArrow", "rightArrowCallout", "rightBrace",
                "rightBracket", "round1Rect", "round2DiagRect", "round2SameRect", "roundRect", "rtTriangle", "smileyFace", "snip1Rect", "snip2DiagRect",
                "snip2SameRect", "snipRoundRect", "squareTabs", "star10", "star12", "star16", "star24", "star32", "star4", "star5", "star6", "star7", "star8",
                "straightConnector1", "stripedRightArrow", "sun", "swooshArrow", "teardrop", "trapezoid", "triangle", "upArrow", "upArrowCallout", "upDownArrow",
                "upDownArrowCallout", "uturnArrow", "verticalScroll", "wave", "wedgeEllipseCallout", "wedgeRectCallout", "wedgeRoundRectCallout",
            };
            return kinds;
        }

        bool is_connector_preset(const std::string& preset) {
            return preset == "line" || preset == "straightConnector1" || preset.starts_with("bentConnector") || preset.starts_with("curvedConnector");
        }

        void unsupported(Shape& shape, const std::string& object, const std::string& reason) {
            shape.object = object;
            shape.reason = reason;
        }

        struct Reader {
            Presentation& presentation;

            void note_text(const std::string& text) {
                if (!presentation.hangul && has_hangul(text)) {
                    presentation.hangul = true;
                }
            }

            // 쓸 수 있는 그림인지. 아니면 까닭
            std::string image_problem(const Part& part, const xml::Node* blip) {
                if (blip == nullptr) {
                    return "그림 파일이 없습니다";
                }
                if (blip->attribute("r:link") != nullptr && blip->attribute("r:embed") == nullptr) {
                    return "바깥 파일에 연결된 그림은 가져올 수 없습니다";
                }
                const auto* relationship = part.rel(blip->get("r:embed"));
                if (relationship == nullptr || relationship->external || !presentation.package.exists(relationship->target)) {
                    return "그림 파일을 찾을 수 없습니다";
                }
                if (!supported_image(relationship->target)) {
                    const std::string extension = extension_of(relationship->target);
                    return "templide는 " + (extension.empty() ? std::string("이") : extension.substr(1)) + " 형식의 그림을 쓸 수 없습니다 (png, jpg, gif, bmp)";
                }
                return "";
            }

            Shape classify(const xml::Node& raw, const Part& part) {
                Shape shape;
                const xml::Node* node = &raw;
                if (raw.name == "mc:AlternateContent") {
                    // 수식(a14:m), 3D 모델, 확대/축소, 잉크는 Choice 안에 있다
                    for (const auto* choice : raw.all("mc:Choice")) {
                        const std::string required = choice->get("Requires");
                        if (choice->find("a14:m") != nullptr) {
                            const auto* inner = choice->children.empty() ? nullptr : &choice->children.front();
                            shape = inner != nullptr ? classify(*inner, part) : Shape{};
                            unsupported(shape, "equation", "수식은 templide에서 지원하지 않습니다");
                            return shape;
                        }
                        if (choice->find("am3d:model3d") != nullptr || required.find("am3d") != std::string::npos) {
                            unsupported(shape, "model3d", "3D 모델은 templide에서 지원하지 않습니다");
                            shape.node = &raw;
                            return shape;
                        }
                        if (required.find("pslz") != std::string::npos || required.find("psez") != std::string::npos) {
                            unsupported(shape, "zoom", "요약/구역/슬라이드 확대·축소는 templide에서 지원하지 않습니다");
                            shape.node = &raw;
                            return shape;
                        }
                        if (required.find("aink") != std::string::npos || choice->find("p14:contentPart") != nullptr || choice->find("p:contentPart") != nullptr) {
                            unsupported(shape, "ink", "잉크(펜으로 그린 것)는 templide에서 지원하지 않습니다");
                            shape.node = &raw;
                            return shape;
                        }
                    }
                    node = alternate(raw);
                    if (node == nullptr) {
                        unsupported(shape, "unknown", "templide가 알 수 없는 개체입니다");
                        shape.node = &raw;
                        return shape;
                    }
                }
                shape.node = node;
                const xml::Node* nv = non_visual(*node);
                const xml::Node* properties = nv != nullptr ? nv->child("p:cNvPr") : nullptr;
                if (properties != nullptr) {
                    shape.id = static_cast<int>(properties->integer("id").value_or(0));
                    shape.name = properties->get("name");
                }
                if (const auto* placeholder = placeholder_of(*node)) {
                    shape.ph_type = placeholder->get("type", "obj");
                    if (const auto idx = placeholder->integer("idx")) {
                        shape.ph_idx = idx;
                    }
                }
                if (const auto* body = text_body(*node)) {
                    shape.excerpt = plain_text(body, 40);
                    const std::string all = plain_text(body, 100000);
                    shape.has_text = !all.empty();
                    note_text(all);
                    if (body->find("a14:m") != nullptr) {
                        unsupported(shape, "equation", "수식은 templide에서 지원하지 않습니다");
                        return shape;
                    }
                }
                if (properties != nullptr && properties->flag("hidden")) {
                    unsupported(shape, node->name == "p:grpSp" ? "group" : "shape", "숨긴 개체는 가져오지 않습니다");
                    return shape;
                }
                if (node->name == "p:sp") {
                    const auto* spPr = node->child("p:spPr");
                    const auto* preset = spPr != nullptr ? spPr->child("a:prstGeom") : nullptr;
                    const auto* custom = spPr != nullptr ? spPr->child("a:custGeom") : nullptr;
                    const bool text_box = nv != nullptr && nv->child("p:cNvSpPr") != nullptr && nv->child("p:cNvSpPr")->flag("txBox");
                    if (!shape.ph_type.empty()) {
                        shape.object = "placeholder";
                    } else if (custom != nullptr) {
                        shape.object = "freeform";
                    } else if (preset != nullptr && is_connector_preset(preset->get("prst"))) {
                        shape.object = "line";
                    } else if (preset != nullptr && !shape_kinds().contains(preset->get("prst"))) {
                        unsupported(shape, "shape", "templide가 모르는 도형 모양입니다 (" + preset->get("prst") + ")");
                    } else if (preset == nullptr || (text_box && preset->get("prst") == "rect")) {
                        shape.object = "text_box";
                    } else {
                        shape.object = "shape";
                    }
                } else if (node->name == "p:pic") {
                    const auto* nvPr = nv != nullptr ? nv->child("p:nvPr") : nullptr;
                    const xml::Node* media = nullptr;
                    bool video = false;
                    if (nvPr != nullptr) {
                        for (const char* tag : {"a:videoFile", "a:quickTimeFile"}) {
                            if (nvPr->child(tag) != nullptr) {
                                video = true;
                            }
                        }
                        media = video ? nvPr : (nvPr->child("a:audioFile") != nullptr || nvPr->child("a:wavAudioFile") != nullptr ? nvPr : nullptr);
                    }
                    if (media != nullptr) {
                        shape.object = video ? "video" : "audio";
                        const auto* embedded = nvPr->find("p14:media");
                        const auto* relationship = embedded != nullptr ? part.rel(embedded->get("r:embed")) : nullptr;
                        if (relationship == nullptr || relationship->external || !presentation.package.exists(relationship->target)) {
                            shape.reason = "바깥 파일에 연결된 " + std::string(video ? "비디오" : "오디오") + "는 가져올 수 없습니다";
                        } else {
                            const std::string extension = extension_of(relationship->target);
                            const bool ok = video ? (extension == ".mp4" || extension == ".webm") : (extension == ".mp3" || extension == ".wav" || extension == ".m4a");
                            if (!ok) {
                                shape.reason = "templide는 " + (extension.empty() ? std::string("이") : extension.substr(1)) + " 형식의 " + (video ? "비디오를 쓸 수 없습니다 (mp4, webm)" : "오디오를 쓸 수 없습니다 (mp3, wav, m4a)");
                            }
                        }
                    } else {
                        shape.object = "image";
                        const auto* blip = node->path({"p:blipFill", "a:blip"});
                        shape.reason = image_problem(part, blip);
                    }
                } else if (node->name == "p:cxnSp") {
                    shape.object = "connector";
                } else if (node->name == "p:grpSp") {
                    shape.object = "group";
                    for (const auto& child : node->children) {
                        if (child.name == "p:nvGrpSpPr" || child.name == "p:grpSpPr" || child.name == "p:extLst") {
                            continue;
                        }
                        shape.children.push_back(classify(child, part));
                    }
                } else if (node->name == "p:graphicFrame") {
                    const auto* data = node->path({"a:graphic", "a:graphicData"});
                    const std::string uri = data != nullptr ? data->get("uri") : "";
                    if (uri.ends_with("/table")) {
                        unsupported(shape, "table", "표는 templide에서 지원하지 않습니다");
                    } else if (uri.find("chart") != std::string::npos) {
                        unsupported(shape, "chart", "차트는 templide에서 지원하지 않습니다");
                    } else if (uri.ends_with("/diagram")) {
                        unsupported(shape, "smartart", "SmartArt는 templide에서 지원하지 않습니다");
                    } else if (uri.ends_with("/ole")) {
                        unsupported(shape, "ole", "OLE 개체(다른 프로그램의 문서)는 templide에서 지원하지 않습니다");
                    } else {
                        unsupported(shape, "unknown", "templide가 알 수 없는 개체입니다");
                    }
                } else if (node->name == "p:contentPart") {
                    unsupported(shape, "ink", "잉크(펜으로 그린 것)는 templide에서 지원하지 않습니다");
                } else {
                    unsupported(shape, "unknown", "templide가 알 수 없는 개체입니다");
                }
                return shape;
            }

            std::vector<Shape> shapes_of(const Part& part) {
                std::vector<Shape> result;
                const auto* tree = part.root.path({"p:cSld", "p:spTree"});
                if (tree == nullptr) {
                    return result;
                }
                for (const auto& child : tree->children) {
                    if (child.name == "p:nvGrpSpPr" || child.name == "p:grpSpPr" || child.name == "p:extLst") {
                        continue;
                    }
                    result.push_back(classify(child, part));
                }
                return result;
            }
        };

        // ---- 애니메이션

        struct PresetKey {
            std::string key;  // 종류.효과[.옵션]
            std::string path; // move 효과의 경로
        };

        std::string attribute_in(const std::string& xml, const std::string& name) {
            const std::string marker = " " + name + "=\"";
            const auto at = xml.find(marker);
            if (at == std::string::npos) {
                return "";
            }
            const auto start = at + marker.size();
            return xml.substr(start, xml.find('"', start) - start);
        }

        // (presetClass, presetID, presetSubtype) -> 효과들. PowerPoint에 효과를 하나씩 넣어 저장한 표를 거꾸로 찾는다
        const std::map<std::tuple<std::string, std::string, std::string>, std::vector<PresetKey>>& preset_index() {
            static const auto index = [] {
                std::map<std::tuple<std::string, std::string, std::string>, std::vector<PresetKey>> result;
                for (const auto& [key, preset] : backend::pptx::animation_presets()) {
                    const std::string xml = preset.xml;
                    result[{attribute_in(xml, "presetClass"), attribute_in(xml, "presetID"), attribute_in(xml, "presetSubtype")}].push_back({key, attribute_in(xml, "path")});
                }
                return result;
            }();
            return index;
        }

        // 효과 안의 시간 중 가장 긴 것(ms). 효과 자신의 cTn은 뺀다
        long long longest(const xml::Node& node, bool top) {
            long long result = 0;
            if (node.name == "p:cTn" && !top) {
                if (const auto duration = node.integer("dur")) {
                    long long delay = 0;
                    if (const auto* condition = node.path({"p:stCondLst", "p:cond"})) {
                        delay = condition->integer("delay").value_or(0);
                    }
                    result = *duration + std::max(0LL, delay);
                }
            }
            for (const auto& child : node.children) {
                result = std::max(result, longest(child, false));
            }
            return result;
        }

        long long default_length(const std::string& key) {
            const auto& table = backend::pptx::animation_presets();
            const auto it = table.find(key);
            if (it == table.end()) {
                return 0;
            }
            // 표의 효과 안에서 가장 긴 시간이 기본 길이다
            std::string error;
            if (const auto node = xml::parse(std::string("<root xmlns:p=\"http://schemas.openxmlformats.org/presentationml/2006/main\">") + it->second.xml + "</root>", error)) {
                if (!node->children.empty()) {
                    return longest(node->children.front(), true);
                }
            }
            return it->second.duration;
        }

        // move 경로(슬라이드 크기에 대한 비율, 끝의 E)를 px의 SVG path로
        std::string motion_to_svg(const std::string& path, double width, double height) {
            std::stringstream stream(path);
            std::string result;
            std::vector<std::string> tokens;
            for (std::string token; stream >> token;) {
                tokens.push_back(token);
            }
            bool x_next = true;
            for (const auto& token : tokens) {
                if (token.size() == 1 && std::isalpha(static_cast<unsigned char>(token[0]))) {
                    if (token == "E" || token == "e") {
                        break;
                    }
                    result += (result.empty() ? "" : " ") + std::string(1, static_cast<char>(std::toupper(static_cast<unsigned char>(token[0]))));
                    x_next = true;
                    continue;
                }
                try {
                    const double value = std::stod(token) * (x_next ? width : height);
                    char text[32];
                    std::snprintf(text, sizeof text, "%.2f", value);
                    std::string number = text;
                    while (number.find('.') != std::string::npos && (number.back() == '0' || number.back() == '.')) {
                        const bool dot = number.back() == '.';
                        number.pop_back();
                        if (dot) {
                            break;
                        }
                    }
                    if (number == "-0") {
                        number = "0";
                    }
                    result += " " + number;
                    x_next = !x_next;
                } catch (...) {
                }
            }
            return result;
        }

        Animation read_effect(const xml::Node& par, const Presentation& presentation) {
            Animation animation;
            const auto* time = par.child("p:cTn");
            if (time == nullptr) {
                animation.reason = "애니메이션을 읽을 수 없습니다";
                return animation;
            }
            const std::string node_type = time->get("nodeType");
            animation.start = node_type == "withEffect" ? "with_previous" : node_type == "afterEffect" ? "after_previous" : "on_click";
            if (const auto* condition = time->path({"p:stCondLst", "p:cond"})) {
                if (const auto delay = condition->integer("delay"); delay && *delay > 0) {
                    animation.delay = *delay;
                }
            }
            if (const auto* target = par.find("p:spTgt")) {
                animation.target = static_cast<int>(target->integer("spid").value_or(0));
                if (target->child("p:txEl") != nullptr) {
                    animation.notes.push_back("문단별 애니메이션을 개체 전체의 애니메이션으로 바꿨습니다");
                }
            }
            if (time->attribute("repeatCount") != nullptr || time->flag("autoRev")) {
                animation.notes.push_back("반복과 되감기는 빠졌습니다");
            }
            const std::string preset_class = time->get("presetClass");
            const std::string preset_id = time->get("presetID");
            const std::string subtype = time->get("presetSubtype", "0");
            if (preset_class == "mediacall") {
                animation.category = "media";
                animation.effect = preset_id == "2" ? "pause" : preset_id == "3" ? "stop" : "play";
                return animation;
            }
            static const std::map<std::string, std::string> categories = {{"entr", "enter"}, {"exit", "exit"}, {"emph", "emphasis"}, {"path", "move"}};
            const auto category = categories.find(preset_class);
            if (category == categories.end()) {
                animation.reason = "templide가 모르는 애니메이션입니다";
                return animation;
            }
            animation.category = category->second;
            std::string key;
            const auto* motion = par.find("p:animMotion");
            const auto& index = preset_index();
            if (const auto found = index.find({preset_class, preset_id, subtype}); found != index.end()) {
                key = found->second.front().key;
                if (preset_class == "path" && motion != nullptr) {
                    // 같은 경로면 그 효과이고, 고친 경로면 직접 그린 경로다
                    const auto same = std::find_if(found->second.begin(), found->second.end(), [&](const PresetKey& each) { return each.path == motion->get("path"); });
                    key = same != found->second.end() ? same->key : "";
                }
            } else if (preset_class != "path") {
                // 옵션만 다르면 첫 옵션으로
                for (const auto& [triple, keys] : index) {
                    if (std::get<0>(triple) == preset_class && std::get<1>(triple) == preset_id) {
                        key = keys.front().key;
                        animation.notes.push_back("효과의 방향(옵션)을 기본값으로 바꿨습니다");
                        break;
                    }
                }
            }
            // 직접 만든 효과(presetID 0)라도 이동 경로가 있으면 경로 이동으로 옮긴다
            if (key.empty() && motion != nullptr) {
                animation.category = "move";
                animation.effect = "path";
                animation.path = motion_to_svg(motion->get("path"), static_cast<double>(presentation.width) / emu_per_px, static_cast<double>(presentation.height) / emu_per_px);
                if (animation.path.empty()) {
                    animation.reason = "이동 경로를 읽을 수 없습니다";
                }
            } else if (key.empty()) {
                animation.reason = "templide가 모르는 애니메이션입니다";
                return animation;
            } else {
                const auto first = key.find('.');
                const auto second = key.find('.', first + 1);
                animation.effect = key.substr(first + 1, second == std::string::npos ? std::string::npos : second - first - 1);
                animation.option = second == std::string::npos ? "" : key.substr(second + 1);
            }
            const long long length = longest(par, true);
            const long long base = animation.effect == "path" ? 2000 : default_length(key);
            const auto& table = backend::pptx::animation_presets();
            const bool endless = !key.empty() && table.contains(key) && table.at(key).duration < 0;
            if (!endless && length > 0 && std::llabs(length - base) > 1) {
                animation.duration = length;
            }
            return animation;
        }

        void read_timing(Slide& slide, const Presentation& presentation) {
            const auto* timing = slide.part->root.child("p:timing");
            if (timing == nullptr) {
                return;
            }
            const auto* root = timing->path({"p:tnLst", "p:par", "p:cTn", "p:childTnLst"});
            if (root == nullptr) {
                return;
            }
            std::set<int> clicked_media;
            for (const auto& sequence : root->children) {
                if (sequence.name != "p:seq") {
                    continue;
                }
                const auto* time = sequence.child("p:cTn");
                if (time == nullptr) {
                    continue;
                }
                const std::string type = time->get("nodeType");
                const auto* clicks = time->child("p:childTnLst");
                if (clicks == nullptr) {
                    continue;
                }
                if (type == "interactiveSeq") {
                    // 개체를 누를 때 실행하는 순서. 비디오, 오디오를 누르면 재생/일시 중지하는 것만 옮긴다
                    const auto* trigger = time->find("p:spTgt");
                    const int spid = trigger != nullptr ? static_cast<int>(trigger->integer("spid").value_or(0)) : 0;
                    const auto* command = time->find("p:cmd");
                    if (command != nullptr && (command->get("cmd") == "togglePause" || command->get("cmd").starts_with("playFrom"))) {
                        clicked_media.insert(spid);
                        continue;
                    }
                    for (const auto& click : clicks->children) {
                        for (const auto* step : click.path({"p:cTn", "p:childTnLst"}) != nullptr ? click.path({"p:cTn", "p:childTnLst"})->all("p:par") : std::vector<const xml::Node*>{}) {
                            for (const auto* effect : step->path({"p:cTn", "p:childTnLst"}) != nullptr ? step->path({"p:cTn", "p:childTnLst"})->all("p:par") : std::vector<const xml::Node*>{}) {
                                Animation animation = read_effect(*effect, presentation);
                                animation.reason = "다른 개체를 누를 때 시작하는 애니메이션(트리거)은 templide에서 지원하지 않습니다";
                                slide.animations.push_back(animation);
                            }
                        }
                    }
                    continue;
                }
                if (type != "mainSeq") {
                    continue;
                }
                for (const auto& click : clicks->children) {
                    const auto* steps = click.path({"p:cTn", "p:childTnLst"});
                    if (steps == nullptr) {
                        continue;
                    }
                    for (const auto* step : steps->all("p:par")) {
                        const auto* effects = step->path({"p:cTn", "p:childTnLst"});
                        if (effects == nullptr) {
                            continue;
                        }
                        for (const auto& effect : effects->children) {
                            if (effect.name == "p:par") {
                                slide.animations.push_back(read_effect(effect, presentation));
                            }
                        }
                    }
                }
            }
            // 비디오, 오디오가 재생을 시작하는 때
            for (const auto& animation : slide.animations) {
                if (animation.category == "media" && animation.effect == "play" && !slide.media_start.contains(animation.target)) {
                    slide.media_start[animation.target] = animation.start == "on_click" ? "click_sequence" : "auto";
                }
            }
            for (const int spid : clicked_media) {
                if (!slide.media_start.contains(spid)) {
                    slide.media_start[spid] = "when_clicked";
                }
            }
        }

        // ---- 검토 메모

        std::string comment_text(const xml::Node* body) {
            if (body == nullptr) {
                return "";
            }
            std::string text;
            for (const auto* paragraph : body->all("a:p")) {
                std::string line;
                paragraph_text(*paragraph, line);
                text += (text.empty() ? "" : "\n") + line;
            }
            return text;
        }

        void read_reviews(Slide& slide, const Presentation& presentation, const std::map<std::string, std::string>& modern_authors, const std::map<std::string, std::string>& legacy_authors) {
            for (const auto& [id, relationship] : slide.part->rels) {
                if (relationship.type != "comments" || relationship.external) {
                    continue;
                }
                const auto part = presentation.package.part(relationship.target);
                if (!part) {
                    continue;
                }
                if (part->root.name == "p188:cmLst") {
                    for (const auto* comment : part->root.all("p188:cm")) {
                        Review review;
                        const auto author = modern_authors.find(comment->get("authorId"));
                        review.author = author != modern_authors.end() ? author->second : "";
                        review.text = comment_text(comment->child("p188:txBody"));
                        if (const auto* position = comment->child("p188:pos")) {
                            review.x = position->integer("x").value_or(0);
                            review.y = position->integer("y").value_or(0);
                        }
                        // 답글은 '이름: 글'로 이어 붙인다
                        if (const auto* replies = comment->child("p188:replyLst")) {
                            for (const auto* reply : replies->all("p188:reply")) {
                                const auto reply_author = modern_authors.find(reply->get("authorId"));
                                review.text += "\n" + (reply_author != modern_authors.end() ? reply_author->second + ": " : std::string()) + comment_text(reply->child("p188:txBody"));
                            }
                        }
                        slide.reviews.push_back(review);
                    }
                } else if (part->root.name == "p:cmLst") {
                    for (const auto* comment : part->root.all("p:cm")) {
                        Review review;
                        const auto author = legacy_authors.find(comment->get("authorId"));
                        review.author = author != legacy_authors.end() ? author->second : "";
                        if (const auto* text = comment->child("p:text")) {
                            review.text = text->text;
                        }
                        // 예전 검토 메모의 위치는 1/8pt 단위다
                        if (const auto* position = comment->child("p:pos")) {
                            review.x = static_cast<Emu>(static_cast<double>(position->integer("x").value_or(0)) * emu_per_pt / 8);
                            review.y = static_cast<Emu>(static_cast<double>(position->integer("y").value_or(0)) * emu_per_pt / 8);
                        }
                        slide.reviews.push_back(review);
                    }
                }
            }
        }
    }

    std::unique_ptr<Presentation> load(const std::filesystem::path& file, std::string& error) {
        auto presentation = std::make_unique<Presentation>();
        presentation->file = file;
        if (!presentation->package.open(file, error)) {
            return nullptr;
        }
        Package& package = presentation->package;
        std::string main = "ppt/presentation.xml";
        // 패키지의 첫 관계가 문서 파트를 가리킨다
        if (const auto rels = package.read("_rels/.rels")) {
            std::string message;
            if (const auto node = xml::parse(*rels, message)) {
                for (const auto* relationship : node->all("Relationship")) {
                    if (relationship->get("Type").ends_with("/officeDocument")) {
                        main = resolve_target("", relationship->get("Target"));
                    }
                }
            }
        }
        presentation->presentation = package.part(main);
        if (!presentation->presentation) {
            error = "PowerPoint 프레젠테이션이 아닙니다: " + display(file);
            return nullptr;
        }
        const Part& document = *presentation->presentation;
        if (const auto* size = document.root.child("p:sldSz")) {
            presentation->width = size->integer("cx").value_or(presentation->width);
            presentation->height = size->integer("cy").value_or(presentation->height);
        }
        Reader reader{*presentation};

        // 슬라이드 마스터와 레이아웃
        std::map<std::string, int> layout_index; // 파트 이름 -> Presentation::layouts의 index
        if (const auto* masters = document.root.child("p:sldMasterIdLst")) {
            for (const auto* entry : masters->all("p:sldMasterId")) {
                const auto* relationship = document.rel(entry->get("r:id"));
                if (relationship == nullptr) {
                    continue;
                }
                Master master;
                master.part = package.part(relationship->target);
                if (!master.part) {
                    continue;
                }
                if (const auto* theme = master.part->first("theme")) {
                    read_theme(master.theme, package.part(theme->target));
                }
                master.name = master.part->root.path({"p:cSld"}) != nullptr ? master.part->root.child("p:cSld")->get("name") : "";
                if (master.name.empty()) {
                    master.name = master.theme.name;
                }
                if (const auto* map = master.part->root.child("p:clrMap")) {
                    for (const auto& [key, value] : map->attributes) {
                        master.color_map[key] = value;
                    }
                }
                master.shapes = reader.shapes_of(*master.part);
                const int master_number = static_cast<int>(presentation->masters.size());
                if (const auto* layouts = master.part->root.child("p:sldLayoutIdLst")) {
                    for (const auto* layout_entry : layouts->all("p:sldLayoutId")) {
                        const auto* layout_relationship = master.part->rel(layout_entry->get("r:id"));
                        if (layout_relationship == nullptr) {
                            continue;
                        }
                        Layout layout;
                        layout.part = package.part(layout_relationship->target);
                        if (!layout.part) {
                            continue;
                        }
                        layout.master = master_number;
                        layout.name = layout.part->root.child("p:cSld") != nullptr ? layout.part->root.child("p:cSld")->get("name") : "";
                        layout.type = layout.part->root.get("type", "cust");
                        layout.show_master_shapes = layout.part->root.flag("showMasterSp", true);
                        layout.shapes = reader.shapes_of(*layout.part);
                        layout_index[layout.part->name] = static_cast<int>(presentation->layouts.size());
                        master.layouts.push_back(static_cast<int>(presentation->layouts.size()));
                        presentation->layouts.push_back(std::move(layout));
                    }
                }
                presentation->masters.push_back(std::move(master));
            }
        }
        if (presentation->masters.empty()) {
            error = "슬라이드 마스터가 없는 파일입니다: " + display(file);
            return nullptr;
        }

        // 구역: sldId의 id -> 구역 이름
        std::map<std::string, std::string> sections;
        if (const auto* list = document.root.find("p14:sectionLst")) {
            for (const auto* section : list->all("p14:section")) {
                if (const auto* ids = section->child("p14:sldIdLst")) {
                    for (const auto* id : ids->all("p14:sldId")) {
                        sections[id->get("id")] = section->get("name");
                    }
                }
            }
        }
        // 검토 메모의 작성자
        std::map<std::string, std::string> modern_authors;
        std::map<std::string, std::string> legacy_authors;
        for (const auto& [id, relationship] : document.rels) {
            if (relationship.type == "authors") {
                if (const auto part = package.part(relationship.target)) {
                    for (const auto* author : part->root.all("p188:author")) {
                        modern_authors[author->get("id")] = author->get("name");
                    }
                }
            } else if (relationship.type == "commentAuthors") {
                if (const auto part = package.part(relationship.target)) {
                    for (const auto* author : part->root.all("p:cmAuthor")) {
                        legacy_authors[author->get("id")] = author->get("name");
                    }
                }
            }
        }

        if (const auto* slides = document.root.child("p:sldIdLst")) {
            for (const auto* entry : slides->all("p:sldId")) {
                const auto* relationship = document.rel(entry->get("r:id"));
                if (relationship == nullptr) {
                    continue;
                }
                Slide slide;
                slide.part = package.part(relationship->target);
                if (!slide.part) {
                    continue;
                }
                if (const auto* layout = slide.part->first("slideLayout")) {
                    if (const auto it = layout_index.find(layout->target); it != layout_index.end()) {
                        slide.layout = it->second;
                    }
                }
                if (slide.layout < 0) {
                    continue; // 레이아웃이 없는 슬라이드는 PowerPoint도 열지 못한다
                }
                slide.hidden = !slide.part->root.flag("show", true);
                if (const auto it = sections.find(entry->get("id")); it != sections.end()) {
                    slide.section = it->second;
                }
                slide.shapes = reader.shapes_of(*slide.part);
                for (const auto& shape : slide.shapes) {
                    if ((shape.ph_type == "title" || shape.ph_type == "ctrTitle") && slide.title.empty()) {
                        slide.title = shape.excerpt;
                    }
                }
                read_timing(slide, *presentation);
                if (const auto* notes = slide.part->first("notesSlide")) {
                    slide.notes = package.part(notes->target);
                }
                read_reviews(slide, *presentation, modern_authors, legacy_authors);
                presentation->slides.push_back(std::move(slide));
            }
        }
        return presentation;
    }
}
