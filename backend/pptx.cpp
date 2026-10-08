#include "pptx.h"
#include "backend.h"
#include "pptx_animations.h"
#include "raster.h"
#include "../middleend/geometry.h"

#include <miniz.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <functional>
#include <map>
#include <numbers>
#include <optional>
#include <set>
#include <system_error>
#include <utility>

namespace templide::backend::pptx {
    // pattern(...)의 종류 -> PowerPoint의 무늬 이름과 앞색이 차지하는 비율(흐린 배경에서 두 색을 섞을 때 쓴다)
    const std::map<std::string, std::pair<std::string, double>>& pattern_presets() {
        static const std::map<std::string, std::pair<std::string, double>> table = {
            {"percent_5", {"pct5", 0.05}}, {"percent_10", {"pct10", 0.1}}, {"percent_20", {"pct20", 0.2}}, {"percent_25", {"pct25", 0.25}},
            {"percent_30", {"pct30", 0.3}}, {"percent_40", {"pct40", 0.4}}, {"percent_50", {"pct50", 0.5}}, {"percent_60", {"pct60", 0.6}},
            {"percent_70", {"pct70", 0.7}}, {"percent_75", {"pct75", 0.75}}, {"percent_80", {"pct80", 0.8}}, {"percent_90", {"pct90", 0.9}},
            {"horizontal", {"horz", 0.25}}, {"vertical", {"vert", 0.25}}, {"light_horizontal", {"ltHorz", 0.25}}, {"light_vertical", {"ltVert", 0.25}},
            {"dark_horizontal", {"dkHorz", 0.5}}, {"dark_vertical", {"dkVert", 0.5}}, {"narrow_horizontal", {"narHorz", 0.5}}, {"narrow_vertical", {"narVert", 0.5}},
            {"dashed_horizontal", {"dashHorz", 0.125}}, {"dashed_vertical", {"dashVert", 0.125}}, {"cross", {"cross", 0.25}},
            {"downward_diagonal", {"dnDiag", 0.25}}, {"upward_diagonal", {"upDiag", 0.25}}, {"light_downward_diagonal", {"ltDnDiag", 0.25}},
            {"light_upward_diagonal", {"ltUpDiag", 0.25}}, {"dark_downward_diagonal", {"dkDnDiag", 0.5}}, {"dark_upward_diagonal", {"dkUpDiag", 0.5}},
            {"wide_downward_diagonal", {"wdDnDiag", 0.375}}, {"wide_upward_diagonal", {"wdUpDiag", 0.375}}, {"dashed_downward_diagonal", {"dashDnDiag", 0.125}},
            {"dashed_upward_diagonal", {"dashUpDiag", 0.125}}, {"diagonal_cross", {"diagCross", 0.25}}, {"small_checker", {"smCheck", 0.5}},
            {"large_checker", {"lgCheck", 0.5}}, {"small_grid", {"smGrid", 0.25}}, {"large_grid", {"lgGrid", 0.125}}, {"dotted_grid", {"dotGrid", 0.125}},
            {"small_confetti", {"smConfetti", 0.25}}, {"large_confetti", {"lgConfetti", 0.25}}, {"horizontal_brick", {"horzBrick", 0.25}},
            {"diagonal_brick", {"diagBrick", 0.25}}, {"solid_diamond", {"solidDmnd", 0.5}}, {"outlined_diamond", {"openDmnd", 0.25}},
            {"dotted_diamond", {"dotDmnd", 0.125}}, {"plaid", {"plaid", 0.5}}, {"sphere", {"sphere", 0.5}}, {"weave", {"weave", 0.5}},
            {"divot", {"divot", 0.25}}, {"shingle", {"shingle", 0.25}}, {"wave", {"wave", 0.25}}, {"trellis", {"trellis", 0.5}}, {"zigzag", {"zigZag", 0.25}},
        };
        return table;
    }

    // "종류.옵션" -> 전환 요소. PowerPoint에 각 효과를 적용해 저장한 결과를 옮긴 것이다
    const std::map<std::string, TransitionXml>& transition_table() {
        static const std::map<std::string, TransitionXml> table = [] {
            std::map<std::string, TransitionXml> result;
            using Options = std::vector<std::pair<std::string, std::string>>; // (옵션, 속성)
            const auto add = [&](const std::string& kind, const std::string& ns, const std::string& tag, const Options& options) {
                for (const auto& [option, attributes] : options) {
                    result[kind + "." + option] = {ns, "<" + ns + ":" + tag + attributes + "/>"};
                }
            };
            const auto directions = [](const std::string& extra, bool corners) {
                Options options = {{"left", extra}, {"up", " dir=\"u\"" + extra}, {"right", " dir=\"r\"" + extra}, {"down", " dir=\"d\"" + extra}};
                if (corners) {
                    for (const auto& [option, dir] : std::vector<std::pair<std::string, std::string>>{{"left_up", "lu"}, {"right_up", "ru"}, {"left_down", "ld"}, {"right_down", "rd"}}) {
                        options.emplace_back(option, " dir=\"" + dir + "\"" + extra);
                    }
                }
                return options;
            };
            const Options none = {{"", ""}};
            const Options orientations = {{"horizontal", ""}, {"vertical", " dir=\"vert\""}};
            const Options left_right = {{"left", " dir=\"l\""}, {"right", " dir=\"r\""}};
            const Options inverted_right = {{"left", ""}, {"right", " invX=\"1\""}};
            const Options inverted_left = {{"right", ""}, {"left", " invX=\"1\""}};
            const Options black = {{"smoothly", ""}, {"through_black", " thruBlk=\"1\""}};

            add("cut", "p", "cut", black);
            add("fade", "p", "fade", black);
            add("random", "p", "random", none);
            add("blinds", "p", "blinds", orientations);
            add("checkerboard", "p", "checker", {{"across", ""}, {"down", " dir=\"vert\""}});
            add("cover", "p", "cover", directions("", true));
            add("uncover", "p", "pull", directions("", true));
            add("dissolve", "p", "dissolve", none);
            add("randomBars", "p", "randomBar", orientations);
            add("strips", "p", "strips", {{"left_up", ""}, {"right_up", " dir=\"ru\""}, {"left_down", " dir=\"ld\""}, {"right_down", " dir=\"rd\""}});
            add("wipe", "p", "wipe", directions("", false));
            add("push", "p", "push", directions("", false));
            add("box", "p", "zoom", {{"out", ""}, {"in", " dir=\"in\""}});
            add("box", "p14", "prism", directions(" isInverted=\"1\"", false));
            add("split", "p", "split", {{"horizontal_out", ""}, {"horizontal_in", " dir=\"in\""}, {"vertical_out", " orient=\"vert\""}, {"vertical_in", " orient=\"vert\" dir=\"in\""}});
            add("circle", "p", "circle", {{"out", ""}});
            add("diamond", "p", "diamond", {{"out", ""}});
            add("plus", "p", "plus", {{"out", ""}});
            add("comb", "p", "comb", orientations);
            add("newsflash", "p", "newsflash", none);
            add("wedge", "p", "wedge", none);
            add("wheel", "p", "wheel", {{"spokes4", ""}, {"spokes1", " spokes=\"1\""}, {"spokes2", " spokes=\"2\""}, {"spokes3", " spokes=\"3\""}, {"spokes8", " spokes=\"8\""}});
            add("wheelReverse", "p14", "wheelReverse", {{"spokes1", " spokes=\"1\""}});
            add("vortex", "p14", "vortex", directions("", false));
            add("ripple", "p14", "ripple", {{"center", ""}, {"left_up", " dir=\"lu\""}, {"right_up", " dir=\"ru\""}, {"left_down", " dir=\"ld\""}, {"right_down", " dir=\"rd\""}});
            // glitter는 옵션 이름과 dir 값이 반대다
            add("glitter", "p14", "glitter", {
                {"diamond_right", ""}, {"diamond_left", " dir=\"r\""}, {"diamond_up", " dir=\"d\""}, {"diamond_down", " dir=\"u\""},
                {"hexagon_right", " pattern=\"hexagon\""}, {"hexagon_left", " dir=\"r\" pattern=\"hexagon\""},
                {"hexagon_up", " dir=\"d\" pattern=\"hexagon\""}, {"hexagon_down", " dir=\"u\" pattern=\"hexagon\""},
            });
            add("gallery", "p14", "gallery", left_right);
            add("conveyor", "p14", "conveyor", left_right);
            add("doors", "p14", "doors", orientations);
            add("window", "p14", "window", orientations);
            add("warp", "p14", "warp", {{"out", ""}, {"in", " dir=\"in\""}});
            add("flyThrough", "p14", "flythrough", {{"in", ""}, {"out", " dir=\"out\""}, {"in_bounce", " hasBounce=\"1\""}, {"out_bounce", " dir=\"out\" hasBounce=\"1\""}});
            add("reveal", "p14", "reveal", {{"smooth_left", ""}, {"smooth_right", " dir=\"r\""}, {"black_left", " thruBlk=\"1\""}, {"black_right", " thruBlk=\"1\" dir=\"r\""}});
            add("honeycomb", "p14", "honeycomb", none);
            add("ferrisWheel", "p14", "ferris", left_right);
            add("switch", "p14", "switch", left_right);
            add("flip", "p14", "flip", left_right);
            add("flashbulb", "p14", "flash", none);
            add("shred", "p14", "shred", {{"strips_in", ""}, {"strips_out", " dir=\"out\""}, {"rectangle_in", " pattern=\"rectangle\""}, {"rectangle_out", " pattern=\"rectangle\" dir=\"out\""}});
            add("cube", "p14", "prism", directions("", false));
            add("rotate", "p14", "prism", directions(" isContent=\"1\"", false));
            add("orbit", "p14", "prism", directions(" isContent=\"1\" isInverted=\"1\"", false));
            add("pan", "p14", "pan", directions("", false));
            for (const std::string preset : {"fallOver", "drape", "peelOff", "pageCurlSingle", "pageCurlDouble"}) {
                add(preset, "p15", "prstTrans", {{"left", " prst=\"" + preset + "\""}, {"right", " prst=\"" + preset + "\" invX=\"1\""}});
            }
            for (const std::string preset : {"wind", "airplane", "origami"}) {
                add(preset, "p15", "prstTrans", {{"right", " prst=\"" + preset + "\""}, {"left", " prst=\"" + preset + "\" invX=\"1\""}});
            }
            for (const std::string preset : {"curtains", "prestige", "fracture", "crush"}) {
                add(preset, "p15", "prstTrans", {{"", " prst=\"" + preset + "\""}});
            }
            add("morph", "p159", "morph", {{"by_object", " option=\"byObject\""}, {"by_word", " option=\"byWord\""}, {"by_char", " option=\"byChar\""}});
            return result;
        }();
        return table;
    }

    namespace {
        using namespace templide::geometry;

        using Emu = std::int64_t;

        constexpr double emu_per_px = 9525; // 1px = 1/96in
        constexpr Emu default_slide_width = 1280 * 9525; // 16:9
        constexpr Emu default_slide_height = 720 * 9525;
        constexpr Emu min_slide_size = 914400; // PowerPoint이 허용하는 슬라이드 크기는 1in ~ 56in
        constexpr Emu max_slide_size = 51206400;
        constexpr Emu list_indent = 342900;
        constexpr Emu full_circle = 21600000; // 360도. 각도는 60000분의 1도 단위
        constexpr Emu connector_margin = 228600; // 꺾인 연결선이 되돌아갈 때 도형에서 떨어지는 거리 (PowerPoint과 같은 0.25in)

        const std::string xml_declaration = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n";
        const std::string namespaces =
            " xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\""
            " xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\""
            " xmlns:p=\"http://schemas.openxmlformats.org/presentationml/2006/main\"";

        const std::string relationship_type = "http://schemas.openxmlformats.org/officeDocument/2006/relationships/";
        const std::string content_type = "application/vnd.openxmlformats-officedocument.";

        // 전환 효과에 쓰는 확장 namespace
        const std::map<std::string, std::string> extension_namespaces = {
            {"p14", "http://schemas.microsoft.com/office/powerpoint/2010/main"},
            {"p15", "http://schemas.microsoft.com/office/powerpoint/2012/main"},
            {"p159", "http://schemas.microsoft.com/office/powerpoint/2015/09/main"},
        };
        // 검토 메모(modern comments)
        const std::string comments_namespace = "http://schemas.microsoft.com/office/powerpoint/2018/8/main";

        // spTree 맨 앞에 와야 하는 그룹 속성
        const std::string group_properties =
            "<p:nvGrpSpPr><p:cNvPr id=\"1\" name=\"\"/><p:cNvGrpSpPr/><p:nvPr/></p:nvGrpSpPr>"
            "<p:grpSpPr><a:xfrm><a:off x=\"0\" y=\"0\"/><a:ext cx=\"0\" cy=\"0\"/><a:chOff x=\"0\" y=\"0\"/><a:chExt cx=\"0\" cy=\"0\"/></a:xfrm></p:grpSpPr>";

        // PowerPoint에서 도형과 선을 새로 넣을 때 붙는 기본 모양. 채우기나 선을 지정하지 않으면 이 모양이 된다
        const std::string shape_style =
            "<p:style><a:lnRef idx=\"2\"><a:schemeClr val=\"accent1\"><a:shade val=\"50000\"/></a:schemeClr></a:lnRef>"
            "<a:fillRef idx=\"1\"><a:schemeClr val=\"accent1\"/></a:fillRef><a:effectRef idx=\"0\"><a:schemeClr val=\"accent1\"/></a:effectRef>"
            "<a:fontRef idx=\"minor\"><a:schemeClr val=\"lt1\"/></a:fontRef></p:style>";
        const std::string line_style =
            "<p:style><a:lnRef idx=\"1\"><a:schemeClr val=\"accent1\"/></a:lnRef>"
            "<a:fillRef idx=\"0\"><a:schemeClr val=\"accent1\"/></a:fillRef><a:effectRef idx=\"0\"><a:schemeClr val=\"accent1\"/></a:effectRef>"
            "<a:fontRef idx=\"minor\"><a:schemeClr val=\"tx1\"/></a:fontRef></p:style>";

        struct Relationship {
            std::string type; // 전체 URI
            std::string target;
            bool external = false; // 파일 밖의 주소(하이퍼링크)
        };

        struct Rect {
            Emu x;
            Emu y;
            Emu cx;
            Emu cy;
        };

        // 지정하지 않은 값은 오른쪽 아래로 3pt 떨어지고 4pt 퍼지는 그림자
        struct Shadow {
            ir::Color color;
            Emu blur = 50800;
            Emu distance = 38100;
            Emu angle = 2700000; // 60000분의 1도. 0이 오른쪽이고 시계 방향
        };

        // 모든 object에 있는 속성
        struct Common {
            std::optional<Emu> rotation; // 60000분의 1도
            bool flip_h = false;
            bool flip_v = false;
            std::string fill;            // a:solidFill, a:gradFill, a:pattFill, a:blipFill. 없으면 빈 문자열
            std::optional<ir::Color> line_color;
            std::optional<Emu> line_width;
            std::string line_dash;       // prstDash 값. 실선이면 빈 문자열
            std::string line_cap;        // flat, rnd, sq
            std::string line_join;       // round, bevel, miter
            std::string line_compound;   // sng, dbl, thickThin, thinThick, tri
            std::optional<Shadow> shadow;
            std::optional<Shadow> inner_shadow;
            std::optional<ir::Color> glow;
            Emu glow_size = 101600;      // 8pt
            std::optional<Emu> soft_edge;
            std::optional<double> reflection; // 반사가 시작할 때의 불투명도
            double reflection_size = 0.35;
            Emu reflection_distance = 0;
            Emu reflection_blur = 6350;
            std::string bevel;           // bevelT의 prst
            Emu bevel_width = 76200;
            Emu bevel_height = 76200;
            std::optional<Emu> depth;
            std::optional<ir::Color> depth_color;
            std::optional<Emu> rotation_x; // 60000분의 1도
            std::optional<Emu> rotation_y;
            std::optional<Emu> perspective;
            std::optional<ir::Link> link;
            // 실행 설정. 누를 때(click)와 마우스를 올릴 때(hover)
            std::optional<ir::Action> action;
            std::optional<ir::Action> hover_action;
            std::string action_sound;
            std::string hover_sound;
            bool action_highlight = false;
            bool hover_highlight = false;
            std::string where; // 에러에 쓰는 개체 위치
            std::optional<Emu> radius;
            std::array<std::optional<double>, 8> adjust; // adj1 ~ adj8. 1이 PowerPoint의 100000
        };

        // radius를 받는 도형. 첫 조정값이 둥근 모서리다
        const std::set<std::string> rounded_kinds = {"roundRect", "round1Rect", "round2SameRect", "round2DiagRect", "snipRoundRect"};

        std::string escape(const std::string& text) {
            std::string result;
            for (const char c : text) {
                switch (c) {
                    case '&': result += "&amp;"; break;
                    case '<': result += "&lt;"; break;
                    case '>': result += "&gt;"; break;
                    case '"': result += "&quot;"; break;
                    case '\'': result += "&apos;"; break;
                    default: result += c; break;
                }
            }
            return result;
        }

        std::string attribute(const std::string& name, const std::string& value) {
            return " " + name + "=\"" + escape(value) + "\"";
        }

        std::string attribute(const std::string& name, Emu value) {
            return attribute(name, std::to_string(value));
        }

        std::string relationships_xml(const std::vector<Relationship>& relationships) {
            std::string xml = xml_declaration + "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">";
            for (std::size_t i = 0; i < relationships.size(); ++i) {
                xml += "<Relationship" + attribute("Id", "rId" + std::to_string(i + 1)) + attribute("Type", relationships[i].type) + attribute("Target", relationships[i].target)
                    + (relationships[i].external ? " TargetMode=\"External\"" : "") + "/>";
            }
            return xml + "</Relationships>";
        }

        std::string xfrm_xml(const Rect& rect, const Common& common, bool flip_h = false, bool flip_v = false) {
            std::string attributes;
            if (common.rotation && *common.rotation != 0) {
                attributes += attribute("rot", *common.rotation);
            }
            if (flip_h || common.flip_h) {
                attributes += " flipH=\"1\"";
            }
            if (flip_v || common.flip_v) {
                attributes += " flipV=\"1\"";
            }
            return "<a:xfrm" + attributes + "><a:off" + attribute("x", rect.x) + attribute("y", rect.y) + "/><a:ext" + attribute("cx", rect.cx) + attribute("cy", rect.cy) + "/></a:xfrm>";
        }

        std::string shadow_attributes(const Shadow& shadow) {
            return attribute("blurRad", shadow.blur) + attribute("dist", shadow.distance) + attribute("dir", shadow.angle);
        }

        // 1~9 단계의 기본 글자 모양. font는 +mn(본문) 또는 +mj(제목) 테마 글꼴
        std::string level_styles_xml(int size, const std::string& font) {
            std::string xml;
            for (int level = 1; level <= 9; ++level) {
                const std::string tag = "a:lvl" + std::to_string(level) + "pPr";
                xml += "<" + tag + "><a:defRPr" + attribute("sz", std::to_string(size)) + ">"
                    "<a:solidFill><a:schemeClr val=\"tx1\"/></a:solidFill>"
                    "<a:latin typeface=\"" + font + "-lt\"/><a:ea typeface=\"" + font + "-ea\"/><a:cs typeface=\"" + font + "-cs\"/>"
                    "</a:defRPr></" + tag + ">";
            }
            return xml;
        }

        // 파일 안에서 겹치지 않으면 되는 GUID
        std::string guid(std::size_t number) {
            char text[48];
            std::snprintf(text, sizeof text, "{7E3A1C00-0000-4000-8000-%012llX}", static_cast<unsigned long long>(number));
            return text;
        }

        std::string format_decimal(double value) {
            char text[32];
            std::snprintf(text, sizeof text, "%.6g", value);
            return text;
        }

        // 지금 시각 (UTC, 2026-09-30T10:07:23.810 모양)
        std::string timestamp() {
            const std::time_t now = std::time(nullptr);
            char text[32];
            std::strftime(text, sizeof text, "%Y-%m-%dT%H:%M:%S.000", std::gmtime(&now));
            return text;
        }

        class Writer {
        public:
            Writer(const ir::Document& document, const ir::Target& target, std::filesystem::path base_dir)
                : document_(document), target_(target), base_dir_(std::move(base_dir)) {}

            std::vector<std::string> run() {
                read_slide_size();

                // target의 master마다 slide master가 하나씩 생기고, 그 layout들이 slide layout이 된다
                std::map<std::pair<std::size_t, std::size_t>, std::size_t> layout_numbers; // (master, layout) -> slideLayout 번호
                for (const std::size_t index : target_.masters) {
                    const ir::Master& master = document_.masters.at(index);
                    master_parts_.push_back({master.name, {}, 0, master.theme ? &*master.theme : nullptr});
                    theme_ = master_parts_.back().theme;
                    for (std::size_t i = 0; i < master.layouts.size(); ++i) {
                        const ir::Layout& layout = master.layouts[i];
                        layout_numbers[{index, i}] = add_layout(layout.name, &layout.elements, layout.background ? &*layout.background : nullptr, master_parts_.size());
                    }
                }
                theme_ = nullptr;
                if (master_parts_.empty()) {
                    master_parts_.push_back({"Blank", {}, 0, nullptr});
                }
                // layout이 없는 slide를 위한 빈 layout은 첫 master에 둔다
                std::size_t blank_layout = 0;
                const bool needs_blank = layout_count_ == 0 || std::any_of(document_.slides.begin(), document_.slides.end(), [](const ir::Slide& slide) { return !slide.layout; });
                if (needs_blank) {
                    blank_layout = add_layout("Blank", nullptr, nullptr, 1);
                }
                for (std::size_t i = 0; i < master_parts_.size(); ++i) {
                    add_master(i + 1);
                }

                has_notes_ = std::any_of(document_.slides.begin(), document_.slides.end(), [](const ir::Slide& slide) { return !slide.notes.paragraphs.empty(); });
                if (has_notes_) {
                    add_notes_master();
                }
                for (std::size_t i = 0; i < document_.slides.size(); ++i) {
                    const ir::Slide& slide = document_.slides[i];
                    std::size_t layout = blank_layout;
                    // layout이 없는 slide는 첫 master의 빈 layout을 쓰므로 그 테마를 따른다
                    theme_ = master_parts_.front().theme;
                    if (slide.layout) {
                        const auto it = layout_numbers.find({slide.layout->master, slide.layout->layout});
                        if (it == layout_numbers.end()) {
                            error("slide " + std::to_string(slide.page) + ": its layout does not belong to the masters of this target");
                            continue;
                        }
                        layout = it->second;
                        const ir::Master& master = document_.masters.at(slide.layout->master);
                        theme_ = master.theme ? &*master.theme : nullptr;
                    }
                    add_slide(i + 1, slide, layout);
                }
                theme_ = nullptr;
                add_presentation();
                add_common_parts();

                if (errors_.empty()) {
                    save();
                }
                return errors_;
            }

        private:
            const ir::Document& document_;
            const ir::Target& target_;
            std::filesystem::path base_dir_;
            Emu slide_width_ = default_slide_width;
            Emu slide_height_ = default_slide_height;
            bool has_notes_ = false;
            std::vector<std::string> errors_;

            struct MasterPart {
                std::string name;
                std::vector<std::size_t> layouts; // slideLayout 번호
                long long id = 0;                 // presentation.xml의 sldMasterId
                const ir::Theme* theme = nullptr;
            };
            std::vector<MasterPart> master_parts_; // slideMaster1부터 차례대로
            std::size_t layout_count_ = 0;
            // master와 layout의 id는 2147483648부터 겹치지 않게 쓴다
            long long next_master_id_ = 2147483648LL;

            std::vector<std::pair<std::string, std::string>> parts_;     // (이름, 내용)
            std::vector<std::pair<std::string, std::string>> overrides_; // (이름, content type)
            std::map<std::string, std::string> defaults_;                // 확장자 -> content type
            std::map<std::filesystem::path, std::string> media_;         // 이미지와 소리 파일 -> ppt/media 안의 이름
            std::size_t media_count_ = 0;                                // ppt/media에 넣은 파일 수. 만든 그림도 센다
            // 슬라이드의 video, audio. timing_xml이 재생 명령과 cMediaNode를 만드는 데 쓴다
            struct Media {
                const ir::Element* element;
                int spid;
                long long duration; // 잘라 낸 뒤의 재생 길이(ms). 알 수 없으면 0
            };
            std::vector<Media> media_elements_;                          // 지금 slide의 video, audio
            std::size_t guid_count_ = 0;
            std::map<std::string, std::string> authors_;                 // 검토 메모 작성자 이름 -> GUID

            // backdrop object에 쓰는 흐린 배경. 슬라이드 전체를 step px 간격으로 찍은 그림이다
            struct Backdrop {
                raster::Raster raster;
                double step;
            };
            std::map<std::string, Backdrop> backdrops_;                                 // (배경, 흐림) -> 흐린 배경
            std::map<std::filesystem::path, std::optional<raster::Raster>> decoded_; // 읽지 못한 그림은 nullopt

            // 지금 만드는 slide나 layout
            std::vector<Relationship>* relationships_ = nullptr;
            bool in_layout_ = false;
            const ir::Value* background_ = nullptr;         // backdrop이 흐리게 할 배경. 없으면 흰 바탕
            const ir::Theme* theme_ = nullptr;              // 테마 색을 RGB로 바꿀 때 쓴다. 없으면 기본 테마
            std::map<const ir::Element*, int> ids_;         // element -> cNvPr id
            std::map<std::string, const ir::Element*> named_; // put ... as NAME의 이름 -> element
            // 지금 만드는 element
            double opacity_ = 1;                            // 모든 색의 불투명도에 곱한다
            std::string default_text_color_ = "tx1";        // opacity가 있을 때 색이 없는 글자에 쓰는 테마 색

            void error(const std::string& message) {
                errors_.push_back(message);
            }

            void add_part(const std::string& name, std::string content, const std::string& type = "") {
                parts_.emplace_back(name, std::move(content));
                if (!type.empty()) {
                    overrides_.emplace_back(name, type);
                }
            }

            std::filesystem::path resolve(const std::string& path) const {
                const std::filesystem::path result = utf8_path(path);
                return result.is_absolute() ? result : base_dir_ / result;
            }

            // relationships_에 넣고 그 rId를 돌려준다
            std::string relate(const std::string& type, const std::string& target, bool external = false) {
                relationships_->push_back({type, target, external});
                return "rId" + std::to_string(relationships_->size());
            }

            // ---- 길이와 각도

            // 단위와 범위는 미들 엔드가 검사했고 절대 단위는 px로 바뀌어 있다.
            // %는 reference에 대한 비율이고, 단위가 없으면 px로 본다. int 값의 %는 px 정수로 자른다
            static Emu to_emu(const ir::Number& number, Emu reference = 0) {
                double total = 0;
                for (const auto& [unit, value] : number.terms) {
                    if (unit == "%") {
                        const double emu = value / 100 * static_cast<double>(reference);
                        total += number.is_float ? emu : std::trunc(emu / emu_per_px) * emu_per_px;
                    } else {
                        total += value * emu_per_px;
                    }
                }
                return std::llround(total);
            }

            // 단위가 없거나 deg인 각도를 PowerPoint의 60000분의 1도로 바꾼다
            static Emu to_angle(const ir::Number& number) {
                const Emu angle = std::llround(number_sum(number) * 60000) % full_circle;
                return angle < 0 ? angle + full_circle : angle;
            }

            // 그라데이션과 그림자의 방향. angles = css면 0이 위쪽이므로 PowerPoint의 0(오른쪽)에 맞춰 90도 돌린다
            Emu to_direction(const ir::Number& number) const {
                const Emu angle = to_angle(number);
                return target_.angles == "css" ? (angle + full_circle - full_circle / 4) % full_circle : angle;
            }

            // s나 ms인 시간. 결과는 ms
            static Emu to_milliseconds(const ir::Number& number) {
                double total = 0;
                for (const auto& [unit, value] : number.terms) {
                    total += unit == "s" ? value * 1000 : value;
                }
                return std::llround(total);
            }

            // 단위가 없는 수, 또는 %의 값
            static double number_sum(const ir::Number& number) {
                double total = 0;
                for (const auto& [unit, value] : number.terms) {
                    total += value;
                }
                return total;
            }

            void read_slide_size() {
                if (!target_.width || !target_.height) {
                    return;
                }
                const Emu width = to_emu(*target_.width);
                const Emu height = to_emu(*target_.height);
                for (const auto& [size, name] : {std::pair{width, "width"}, std::pair{height, "height"}}) {
                    if (size < min_slide_size || size > max_slide_size) {
                        error(std::string("slide ") + name + " must be between 1in and 56in");
                    }
                }
                slide_width_ = width;
                slide_height_ = height;
            }

            Emu length(const ir::Element& element, const std::string& name, Emu reference) const {
                const auto* number = std::get_if<ir::Number>(find_property(element, name));
                return number != nullptr ? to_emu(*number, reference) : 0;
            }

            // %를 슬라이드 크기로 푼 뒤에야 알 수 있는 음수 크기는 여기서 알린다
            std::optional<Rect> element_rect(const ir::Element& element, const std::string& where) {
                const Rect rect{length(element, "x", slide_width_), length(element, "y", slide_height_), length(element, "width", slide_width_), length(element, "height", slide_height_)};
                if (rect.cx < 0 || rect.cy < 0) {
                    error(where + ": width and height must not be negative");
                    return std::nullopt;
                }
                return rect;
            }

            std::optional<Emu> optional_length(const ir::Element& element, const std::string& name) const {
                if (const auto* number = std::get_if<ir::Number>(find_property(element, name))) {
                    return to_emu(*number);
                }
                return std::nullopt;
            }

            std::optional<Emu> optional_angle(const ir::Element& element, const std::string& name) const {
                if (const auto* number = std::get_if<ir::Number>(find_property(element, name))) {
                    return to_angle(*number);
                }
                return std::nullopt;
            }

            // 단위 없는 수
            std::optional<double> optional_scalar(const ir::Element& element, const std::string& name) const {
                if (const auto* number = std::get_if<ir::Number>(find_property(element, name))) {
                    return number_sum(*number);
                }
                return std::nullopt;
            }

            Common read_common(const ir::Element& element, const std::string& where) {
                Common common;
                common.rotation = optional_angle(element, "rotation");
                const std::string flip = enum_member(element, "flip");
                common.flip_h = flip == "horizontal" || flip == "both";
                common.flip_v = flip == "vertical" || flip == "both";
                if (const auto* fill = find_property(element, "fill")) {
                    common.fill = fill_xml(*fill, where + ", fill");
                }
                if (const auto* color = std::get_if<ir::Color>(find_property(element, "line_color"))) {
                    common.line_color = *color;
                }
                common.line_width = optional_length(element, "line_width");
                const std::string dash = enum_member(element, "line_dash");
                common.line_dash = dash == "solid" ? "" : dash;
                static const std::map<std::string, std::string> caps = {{"flat", "flat"}, {"round", "rnd"}, {"square", "sq"}};
                static const std::map<std::string, std::string> compounds = {{"single", "sng"}, {"double", "dbl"}, {"thick_thin", "thickThin"}, {"thin_thick", "thinThick"}, {"triple", "tri"}};
                if (const auto it = caps.find(enum_member(element, "line_cap")); it != caps.end()) {
                    common.line_cap = it->second;
                }
                common.line_join = enum_member(element, "line_join");
                if (const auto it = compounds.find(enum_member(element, "line_compound")); it != compounds.end()) {
                    common.line_compound = it->second;
                }
                common.shadow = read_shadow(element, "shadow");
                common.inner_shadow = read_shadow(element, "inner_shadow");
                if (const auto* glow = std::get_if<ir::Color>(find_property(element, "glow"))) {
                    common.glow = *glow;
                }
                common.glow_size = optional_length(element, "glow_size").value_or(common.glow_size);
                common.soft_edge = optional_length(element, "soft_edge");
                common.reflection = optional_scalar(element, "reflection");
                common.reflection_size = optional_scalar(element, "reflection_size").value_or(common.reflection_size);
                common.reflection_distance = optional_length(element, "reflection_distance").value_or(common.reflection_distance);
                common.reflection_blur = optional_length(element, "reflection_blur").value_or(common.reflection_blur);
                static const std::map<std::string, std::string> bevels = {
                    {"circle", "circle"}, {"relaxed_inset", "relaxedInset"}, {"cross", "cross"}, {"cool_slant", "coolSlant"}, {"angle", "angle"}, {"soft_round", "softRound"},
                    {"convex", "convex"}, {"slope", "slope"}, {"divot", "divot"}, {"riblet", "riblet"}, {"hard_edge", "hardEdge"}, {"art_deco", "artDeco"},
                };
                if (const auto it = bevels.find(enum_member(element, "bevel")); it != bevels.end()) {
                    common.bevel = it->second;
                }
                common.bevel_width = optional_length(element, "bevel_width").value_or(common.bevel_width);
                common.bevel_height = optional_length(element, "bevel_height").value_or(common.bevel_height);
                common.depth = optional_length(element, "depth");
                if (const auto* color = std::get_if<ir::Color>(find_property(element, "depth_color"))) {
                    common.depth_color = *color;
                }
                read_rotation_3d(element, common);
                if (const auto degrees = optional_scalar(element, "perspective")) {
                    common.perspective = std::llround(*degrees * 60000);
                }
                if (const auto* link = std::get_if<ir::Link>(find_property(element, "link"))) {
                    common.link = *link;
                }
                common.where = where;
                if (const auto* action = std::get_if<ir::Action>(find_property(element, "action"))) {
                    common.action = *action;
                }
                if (const auto* action = std::get_if<ir::Action>(find_property(element, "hover_action"))) {
                    common.hover_action = *action;
                }
                if (const auto* sound = std::get_if<std::string>(find_property(element, "action_sound"))) {
                    common.action_sound = *sound;
                }
                if (const auto* sound = std::get_if<std::string>(find_property(element, "hover_sound"))) {
                    common.hover_sound = *sound;
                }
                const auto* highlight = std::get_if<bool>(find_property(element, "action_highlight"));
                common.action_highlight = highlight != nullptr && *highlight;
                highlight = std::get_if<bool>(find_property(element, "hover_highlight"));
                common.hover_highlight = highlight != nullptr && *highlight;
                common.radius = optional_length(element, "radius");
                for (std::size_t i = 0; i < common.adjust.size(); ++i) {
                    common.adjust[i] = optional_scalar(element, "adj" + std::to_string(i + 1));
                }
                return common;
            }

            // rotation_x, rotation_y. angles = css면 CSS의 rotateX(위아래로 기울기), rotateY(좌우로 돌리기)이므로
            // PowerPoint의 Y 회전, X 회전으로 바꾼다
            void read_rotation_3d(const ir::Element& element, Common& common) const {
                const auto x = optional_angle(element, "rotation_x");
                const auto y = optional_angle(element, "rotation_y");
                if (target_.angles != "css") {
                    common.rotation_x = x;
                    common.rotation_y = y;
                    return;
                }
                const auto negate = [](std::optional<Emu> angle) { return angle ? std::optional<Emu>((full_circle - *angle) % full_circle) : std::nullopt; };
                common.rotation_x = css_rotate_y_sign > 0 ? y : negate(y);
                common.rotation_y = css_rotate_x_sign > 0 ? x : negate(x);
            }

            // prefix(shadow, inner_shadow)의 색과 _blur, _distance, _angle. 색이 없으면 그림자도 없다
            std::optional<Shadow> read_shadow(const ir::Element& element, const std::string& prefix) const {
                const auto* color = std::get_if<ir::Color>(find_property(element, prefix));
                if (color == nullptr) {
                    return std::nullopt;
                }
                Shadow shadow;
                shadow.color = *color;
                shadow.blur = optional_length(element, prefix + "_blur").value_or(shadow.blur);
                shadow.distance = optional_length(element, prefix + "_distance").value_or(shadow.distance);
                if (const auto* angle = std::get_if<ir::Number>(find_property(element, prefix + "_angle"))) {
                    shadow.angle = to_direction(*angle);
                }
                return shadow;
            }

            // ---- 색과 채우기

            // 테마 색이면 schemeClr이다. 지금 element의 opacity만큼 더 투명해진다
            std::string color_xml(const ir::Color& color, const std::string& modifiers = "") const {
                const double alpha = color.a * opacity_;
                std::string tag = "a:srgbClr";
                std::string value = color.scheme;
                if (value.empty()) {
                    char hex[8];
                    std::snprintf(hex, sizeof hex, "%02X%02X%02X", color.r, color.g, color.b);
                    value = hex;
                } else {
                    tag = "a:schemeClr";
                }
                const std::string children = modifiers + (alpha < 1 ? "<a:alpha" + attribute("val", std::llround(alpha * 100000)) + "/>" : "");
                return "<" + tag + attribute("val", value) + (children.empty() ? "/>" : ">" + children + "</" + tag + ">");
            }

            std::string solid_fill_xml(const ir::Color& color) const {
                return "<a:solidFill>" + color_xml(color) + "</a:solidFill>";
            }

            static ir::Color scheme(const std::string& name) {
                return ir::Color{0, 0, 0, 1, name};
            }

            // 불투명한 RGB. 테마 색은 지금 테마의 값을 쓰고, 투명한 만큼 흰 바탕에 섞는다
            std::array<float, 3> opaque(ir::Color color) const {
                if (!color.scheme.empty() && theme_ != nullptr) {
                    if (const auto it = theme_->colors.find(color.scheme); it != theme_->colors.end()) {
                        color.r = it->second.r;
                        color.g = it->second.g;
                        color.b = it->second.b;
                    }
                }
                const auto mix = [&](int channel) { return static_cast<float>(channel * color.a + 255 * (1 - color.a)); };
                return {mix(color.r), mix(color.g), mix(color.b)};
            }

            // 색, linear(...), radial(...), pattern(...), image(...)
            std::string fill_xml(const ir::Value& value, const std::string& where) {
                if (const auto* color = std::get_if<ir::Color>(&value)) {
                    return solid_fill_xml(*color);
                }
                if (const auto* pattern = std::get_if<ir::Pattern>(&value)) {
                    const auto it = pattern_presets().find(pattern->kind);
                    return "<a:pattFill" + attribute("prst", it != pattern_presets().end() ? it->second.first : "pct50") + "><a:fgClr>" + color_xml(pattern->foreground)
                        + "</a:fgClr><a:bgClr>" + color_xml(pattern->background) + "</a:bgClr></a:pattFill>";
                }
                if (const auto* image = std::get_if<ir::Image>(&value)) {
                    const auto media = add_media(image->path, where);
                    if (!media) {
                        return "";
                    }
                    const std::string id = relate(relationship_type + "image", "../media/" + *media);
                    return "<a:blipFill dpi=\"0\" rotWithShape=\"1\"><a:blip" + attribute("r:embed", id) + alpha_modifier() + "<a:srcRect/><a:stretch><a:fillRect/></a:stretch></a:blipFill>";
                }
                const auto* gradient = std::get_if<ir::Gradient>(&value);
                if (gradient == nullptr || gradient->colors.size() < 2) {
                    return "";
                }
                const auto positions = gradient_stops(*gradient);
                std::string stops;
                for (std::size_t i = 0; i < gradient->colors.size(); ++i) {
                    stops += "<a:gs" + attribute("pos", std::llround(positions[i] * 100000)) + ">" + color_xml(gradient->colors[i]) + "</a:gs>";
                }
                if (gradient->radial) {
                    return "<a:gradFill rotWithShape=\"1\"><a:gsLst>" + stops + "</a:gsLst>"
                           "<a:path path=\"circle\"><a:fillToRect l=\"50000\" t=\"50000\" r=\"50000\" b=\"50000\"/></a:path></a:gradFill>";
                }
                return "<a:gradFill rotWithShape=\"1\"><a:gsLst>" + stops + "</a:gsLst><a:lin" + attribute("ang", to_direction(gradient->angle)) + " scaled=\"0\"/></a:gradFill>";
            }

            // 그림의 투명도. a:blip 여는 태그를 닫는 부분까지 만든다
            std::string alpha_modifier() const {
                if (opacity_ >= 1) {
                    return "/>";
                }
                return "><a:alphaModFix" + attribute("amt", std::llround(opacity_ * 100000)) + "/></a:blip>";
            }

            // 도형 모양. radius는 짧은 변에 대한 비율로 첫 조정값이 되고, 반을 넘으면 반으로 줄인다.
            // 조정값을 하나라도 바꾸면 나머지도 기본값으로 모두 적는다
            std::string geometry_xml(const std::string& kind, const Rect& rect, const Common& common) const {
                std::string adjust;
                const auto defaults = shape_adjustments().find(kind);
                const bool rounded = common.radius && rounded_kinds.contains(kind);
                const bool changed = rounded || std::any_of(common.adjust.begin(), common.adjust.end(), [](const auto& value) { return value.has_value(); });
                if (changed && defaults != shape_adjustments().end()) {
                    for (std::size_t i = 0; i < defaults->second.size(); ++i) {
                        long long value = defaults->second[i].second;
                        if (i == 0 && rounded) {
                            const Emu side = std::min(rect.cx, rect.cy);
                            value = side <= 0 ? 0 : std::min<Emu>(50000, std::llround(100000.0 * static_cast<double>(*common.radius) / static_cast<double>(side)));
                        }
                        if (i < common.adjust.size() && common.adjust[i]) {
                            value = std::llround(*common.adjust[i] * 100000);
                        }
                        adjust += "<a:gd" + attribute("name", defaults->second[i].first) + attribute("fmla", "val " + std::to_string(value)) + "/>";
                    }
                }
                return "<a:prstGeom" + attribute("prst", kind) + ">" + (adjust.empty() ? "<a:avLst/>" : "<a:avLst>" + adjust + "</a:avLst>") + "</a:prstGeom>";
            }

            // 네온, 안쪽 그림자, 바깥 그림자, 반사, 부드러운 가장자리. spPr에서 ln 다음에 온다
            std::string effect_xml(const Common& common) const {
                std::string effects;
                if (common.glow) {
                    effects += "<a:glow" + attribute("rad", common.glow_size) + ">" + color_xml(*common.glow) + "</a:glow>";
                }
                if (common.inner_shadow) {
                    effects += "<a:innerShdw" + shadow_attributes(*common.inner_shadow) + ">" + color_xml(common.inner_shadow->color) + "</a:innerShdw>";
                }
                if (common.shadow) {
                    effects += "<a:outerShdw" + shadow_attributes(*common.shadow) + " algn=\"tl\" rotWithShape=\"0\">" + color_xml(common.shadow->color) + "</a:outerShdw>";
                }
                if (common.reflection) {
                    effects += "<a:reflection" + attribute("blurRad", common.reflection_blur) + attribute("stA", std::llround(*common.reflection * opacity_ * 100000))
                        + " endA=\"300\"" + attribute("endPos", std::llround(common.reflection_size * 100000)) + attribute("dist", common.reflection_distance)
                        + " dir=\"5400000\" sy=\"-100000\" algn=\"bl\" rotWithShape=\"0\"/>";
                }
                if (common.soft_edge) {
                    effects += "<a:softEdge" + attribute("rad", *common.soft_edge) + "/>";
                }
                return (effects.empty() ? "" : "<a:effectLst>" + effects + "</a:effectLst>") + three_d_xml(common);
            }

            // 3차원 회전과 입체. PowerPoint의 X 회전은 카메라의 경도를 반대로, Y 회전은 위도를 돌린다
            std::string three_d_xml(const Common& common) const {
                const bool rotated = common.rotation_x || common.rotation_y || common.perspective;
                const bool solid = !common.bevel.empty() || common.depth;
                if (!rotated && !solid) {
                    return "";
                }
                std::string camera = common.perspective && *common.perspective > 0
                    ? "<a:camera prst=\"perspectiveFront\"" + attribute("fov", *common.perspective)
                    : std::string("<a:camera prst=\"orthographicFront\"");
                if (common.rotation_x || common.rotation_y) {
                    const Emu longitude = (full_circle - common.rotation_x.value_or(0)) % full_circle;
                    camera += "><a:rot" + attribute("lat", common.rotation_y.value_or(0)) + attribute("lon", longitude) + " rev=\"0\"/></a:camera>";
                } else {
                    camera += "/>";
                }
                std::string xml = "<a:scene3d>" + camera + "<a:lightRig rig=\"threePt\" dir=\"t\"/></a:scene3d>";
                if (solid) {
                    xml += "<a:sp3d" + (common.depth ? attribute("extrusionH", *common.depth) : "") + ">";
                    if (!common.bevel.empty()) {
                        xml += "<a:bevelT" + attribute("w", common.bevel_width) + attribute("h", common.bevel_height) + attribute("prst", common.bevel) + "/>";
                    }
                    if (common.depth_color) {
                        xml += "<a:extrusionClr>" + color_xml(*common.depth_color) + "</a:extrusionClr>";
                    }
                    xml += "</a:sp3d>";
                }
                return xml;
            }

            // 테두리나 선. styled면 지정하지 않은 값은 p:style의 기본 모양을 따르고, 아니면 검은 선이 된다.
            // arrows는 선 끝 모양(headEnd, tailEnd)
            std::string ln_xml(const Common& common, bool styled, const std::string& arrows = "") const {
                const bool plain = !common.line_color && !common.line_width && common.line_dash.empty() && common.line_cap.empty()
                    && common.line_join.empty() && common.line_compound.empty() && arrows.empty();
                // 투명도가 있으면 기본 선 색에도 투명도를 적어야 한다
                if (plain && !(styled && opacity_ < 1)) {
                    return "";
                }
                std::string xml = "<a:ln" + (common.line_width ? attribute("w", *common.line_width) : "")
                    + (common.line_cap.empty() ? "" : attribute("cap", common.line_cap)) + (common.line_compound.empty() ? "" : attribute("cmpd", common.line_compound)) + ">";
                if (common.line_color) {
                    xml += solid_fill_xml(*common.line_color);
                } else if (!styled) {
                    xml += "<a:solidFill>" + color_xml(scheme("tx1")) + "</a:solidFill>";
                } else if (opacity_ < 1) {
                    xml += "<a:solidFill>" + color_xml(scheme("accent1"), "<a:shade val=\"50000\"/>") + "</a:solidFill>";
                }
                if (!common.line_dash.empty()) {
                    xml += "<a:prstDash" + attribute("val", common.line_dash) + "/>";
                }
                if (common.line_join == "round") {
                    xml += "<a:round/>";
                } else if (common.line_join == "bevel") {
                    xml += "<a:bevel/>";
                } else if (common.line_join == "miter") {
                    xml += "<a:miter lim=\"800000\"/>";
                }
                return xml + arrows + "</a:ln>";
            }

            // p:style이 주는 기본 채우기에 투명도를 적어야 할 때 쓰는 채우기
            std::string styled_fill_xml(const Common& common) const {
                if (!common.fill.empty() || opacity_ >= 1) {
                    return common.fill;
                }
                return "<a:solidFill>" + color_xml(scheme("accent1")) + "</a:solidFill>";
            }

            // 선 끝 모양
            std::string arrows_xml(const ir::Element& element) {
                std::string xml;
                for (const auto& [property, tag] : {std::pair{"start_arrow", "a:headEnd"}, std::pair{"end_arrow", "a:tailEnd"}}) {
                    const std::string arrow = enum_member(element, property);
                    if (!arrow.empty() && arrow != "none") {
                        xml += std::string("<") + tag + attribute("type", arrow) + "/>";
                    }
                }
                return xml;
            }

            // text_anchor 값을 bodyPr의 anchor로
            std::string anchor(const ir::Element& element, const std::string& fallback) {
                const std::string value = enum_member(element, "anchor");
                if (value == "top") {
                    return "t";
                }
                if (value == "middle") {
                    return "ctr";
                }
                if (value == "bottom") {
                    return "b";
                }
                return fallback;
            }

            // ---- 도형

            // slide나 layout 하나를 만들기 시작한다. element마다 id를 정하고 다음 id를 돌려준다
            int begin_tree(std::vector<Relationship>& relationships, bool layout, const ir::Value* background, const std::vector<ir::Element>* elements) {
                relationships_ = &relationships;
                in_layout_ = layout;
                background_ = background;
                ids_.clear();
                named_.clear();
                media_elements_.clear();
                opacity_ = 1;
                return elements != nullptr ? assign_ids(*elements, 2) : 2; // 1은 spTree 자신
            }

            // 그룹은 자기 id 다음에 자식들의 id가 온다
            int assign_ids(const std::vector<ir::Element>& elements, int next) {
                for (const auto& element : elements) {
                    ids_[&element] = next++;
                    if (!element.name.empty()) {
                        named_[element.name] = &element;
                    }
                    next = assign_ids(element.children, next);
                }
                return next;
            }

            std::string shapes_xml(const std::vector<ir::Element>& elements, const std::string& where) {
                std::string xml;
                for (const auto& element : elements) {
                    xml += element_xml(element, where);
                }
                return xml;
            }

            std::string element_xml(const ir::Element& element, const std::string& where) {
                const std::string element_where = where + ", " + element.object + (element.name.empty() || element.generated_name ? "" : " " + element.name);
                const int id = ids_.at(&element);
                if (element.object == "group") {
                    return group_xml(id, element, element_where);
                }
                opacity_ = optional_scalar(element, "opacity").value_or(1);
                default_text_color_ = element.object == "shape" ? "lt1" : "tx1";
                const Common common = read_common(element, element_where);
                std::string xml;
                if (element.object == "text_box") {
                    xml = text_box_xml(id, element, common, element_where);
                } else if (element.object == "image") {
                    xml = picture_xml(id, element, common, element_where);
                } else if (element.object == "shape") {
                    xml = shape_xml(id, element, common, element_where);
                } else if (element.object == "line") {
                    xml = line_xml(id, element, common);
                } else if (element.object == "backdrop") {
                    xml = backdrop_xml(id, element, common, element_where);
                } else if (element.object == "connector") {
                    xml = connector_xml(id, element, common);
                } else if (element.object == "freeform") {
                    xml = freeform_xml(id, element, common);
                } else if (element.object == "placeholder") {
                    xml = placeholder_xml(id, element, common, element_where);
                } else if (element.object == "video" || element.object == "audio") {
                    xml = media_xml(id, element, common, element_where);
                } else {
                    error(element_where + ": the pptx backend does not support this object");
                }
                opacity_ = 1;
                return xml;
            }

            // cNvPr. 이름을 붙이지 않았으면 PowerPoint처럼 "종류 번호"로 짓는다. 링크나 실행 설정이 있으면 누르거나 마우스를 올릴 때 그 일을 한다
            std::string cnvpr_xml(int id, const ir::Element& element, const std::string& label, const Common& common) {
                const std::string name = element.name.empty() || element.generated_name ? label + " " + std::to_string(id - 1) : element.name;
                const std::optional<ir::Action> click = common.link ? std::optional<ir::Action>(ir::Action{"link", *common.link, "", {}, ""}) : common.action;
                const std::string link = interaction_xml("a:hlinkClick", click, common.action_sound, common.action_highlight, common.where + ", action_sound")
                    + interaction_xml("a:hlinkHover", common.hover_action, common.hover_sound, common.hover_highlight, common.where + ", hover_sound");
                return "<p:cNvPr" + attribute("id", id) + attribute("name", name) + (link.empty() ? "/>" : ">" + link + "</p:cNvPr>");
            }

            // bodyPr. text_box는 PowerPoint의 글상자처럼 줄을 바꾸고 크기를 맞추지 않는다
            std::string body_properties_xml(const ir::Element& element, bool text_box, const std::string& default_anchor) {
                std::string attributes;
                const auto all = optional_length(element, "padding");
                for (const auto& [name, inset] : {std::pair{"padding_left", "lIns"}, std::pair{"padding_top", "tIns"}, std::pair{"padding_right", "rIns"}, std::pair{"padding_bottom", "bIns"}}) {
                    const auto value = optional_length(element, name);
                    if (value || all) {
                        attributes += attribute(inset, value ? *value : *all);
                    }
                }
                if (const auto* wrap = std::get_if<bool>(find_property(element, "wrap"))) {
                    attributes += *wrap ? " wrap=\"square\"" : " wrap=\"none\"";
                } else if (text_box) {
                    attributes += " wrap=\"square\"";
                }
                static const std::map<std::string, std::string> directions = {
                    {"horizontal", "horz"}, {"vertical", "vert"}, {"vertical270", "vert270"}, {"stacked", "wordArtVert"}, {"east_asian", "eaVert"},
                };
                if (const auto it = directions.find(enum_member(element, "text_direction")); it != directions.end()) {
                    attributes += attribute("vert", it->second);
                }
                if (const auto count = optional_scalar(element, "columns")) {
                    attributes += attribute("numCol", std::llround(*count));
                }
                if (const auto gap = optional_length(element, "column_gap")) {
                    attributes += attribute("spcCol", *gap);
                }
                attributes += " rtlCol=\"0\"" + attribute("anchor", anchor(element, default_anchor));
                const std::string autofit = enum_member(element, "autofit");
                std::string child = text_box ? "<a:noAutofit/>" : "";
                if (autofit == "none") {
                    child = "<a:noAutofit/>";
                } else if (autofit == "shrink") {
                    child = "<a:normAutofit/>";
                } else if (autofit == "resize") {
                    child = "<a:spAutoFit/>";
                }
                return "<a:bodyPr" + attributes + (child.empty() ? "/>" : ">" + child + "</a:bodyPr>");
            }

            // 위치가 잘못됐어도 text의 에러까지 모으기 위해 끝까지 만들고, 에러가 있으면 버린다
            std::string text_box_xml(int id, const ir::Element& element, const Common& common, const std::string& where) {
                const auto rect = element_rect(element, where);
                const auto* text = std::get_if<ir::Text>(find_property(element, "text"));
                if (text == nullptr) {
                    error(where + ": 'text' must be text");
                    return "";
                }
                const std::string xml = "<p:sp><p:nvSpPr>" + cnvpr_xml(id, element, "TextBox", common) + "<p:cNvSpPr txBox=\"1\"/><p:nvPr/></p:nvSpPr>"
                    "<p:spPr>" + xfrm_xml(rect.value_or(Rect{}), common) + "<a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom>"
                    + (common.fill.empty() ? "<a:noFill/>" : common.fill) + ln_xml(common, false) + effect_xml(common) + "</p:spPr>"
                    + text_body_xml(*text, body_properties_xml(element, true, "t"), "l") + "</p:sp>";
                return rect ? xml : "";
            }

            std::string shape_xml(int id, const ir::Element& element, const Common& common, const std::string& where) {
                const auto rect = element_rect(element, where);
                const std::string kind = enum_member(element, "kind");
                const auto* text = std::get_if<ir::Text>(find_property(element, "text"));
                if (kind.empty() || text == nullptr) {
                    error(where + ": 'kind' must be a shape_kind and 'text' must be text");
                    return "";
                }
                const std::string xml = "<p:sp><p:nvSpPr>" + cnvpr_xml(id, element, "Shape", common) + "<p:cNvSpPr/><p:nvPr/></p:nvSpPr>"
                    "<p:spPr>" + xfrm_xml(rect.value_or(Rect{}), common) + geometry_xml(kind, rect.value_or(Rect{}), common)
                    + styled_fill_xml(common) + ln_xml(common, true) + effect_xml(common) + "</p:spPr>"
                    + shape_style + text_body_xml(*text, body_properties_xml(element, false, "ctr"), "ctr") + "</p:sp>";
                return rect ? xml : "";
            }

            // title, subtitle, body 개체 틀. idx는 slide의 개체 틀과 짝을 맞추는 번호다
            static std::string placeholder_type(const std::string& role) {
                if (role == "title") {
                    return "<p:ph type=\"title\"/>";
                }
                return role == "subtitle" ? "<p:ph type=\"subTitle\" idx=\"2\"/>" : "<p:ph type=\"body\" idx=\"1\"/>";
            }

            static std::string placeholder_label(const std::string& role) {
                return role == "title" ? "Title" : role == "subtitle" ? "Subtitle" : "Body";
            }

            // layout의 개체 틀. 안내 글의 첫 글자 모양을 lstStyle에 적어서 slide의 글자가 그 모양을 물려받게 한다
            std::string placeholder_xml(int id, const ir::Element& element, const Common& common, const std::string& where) {
                const auto rect = element_rect(element, where);
                const auto* text = std::get_if<ir::Text>(find_property(element, "text"));
                const std::string role = enum_member(element, "role");
                if (text == nullptr) {
                    error(where + ": 'text' must be text");
                    return "";
                }
                std::string list_style = "<a:lstStyle/>";
                if (!text->paragraphs.empty() && !text->paragraphs.front().runs.empty()) {
                    const ir::TextStyle& style = text->paragraphs.front().runs.front().style;
                    const std::string align = alignment(style);
                    list_style = "<a:lstStyle><a:lvl1pPr" + (align.empty() ? "" : attribute("algn", align)) + ">" + run_properties_xml(style, "a:defRPr") + "</a:lvl1pPr></a:lstStyle>";
                }
                const std::string xml = "<p:sp><p:nvSpPr>" + cnvpr_xml(id, element, placeholder_label(role), common)
                    + "<p:cNvSpPr><a:spLocks noGrp=\"1\"/></p:cNvSpPr><p:nvPr>" + placeholder_type(role) + "</p:nvPr></p:nvSpPr>"
                    "<p:spPr>" + xfrm_xml(rect.value_or(Rect{}), common) + "<a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom>"
                    + common.fill + ln_xml(common, false) + effect_xml(common) + "</p:spPr>"
                    + text_body_xml(*text, body_properties_xml(element, false, "t"), "", list_style) + "</p:sp>";
                return rect ? xml : "";
            }

            // slide의 title, subtitle, body. layout에서 같은 역할의 개체 틀을 찾아 내용만 채운다
            std::string slide_placeholders_xml(const ir::Slide& slide, int id) {
                std::string xml;
                for (const auto& [role, text] : slide.placeholders) {
                    default_text_color_ = "tx1";
                    xml += "<p:sp><p:nvSpPr><p:cNvPr" + attribute("id", id) + attribute("name", placeholder_label(role) + " " + std::to_string(id - 1)) + "/>"
                        "<p:cNvSpPr><a:spLocks noGrp=\"1\"/></p:cNvSpPr><p:nvPr>" + placeholder_type(role) + "</p:nvPr></p:nvSpPr><p:spPr/>"
                        + text_body_xml(text, "<a:bodyPr/>", "") + "</p:sp>";
                    ++id;
                }
                return xml;
            }

            // 두 점을 감싸는 상자
            Rect line_rect(const ir::Element& element) const {
                const Emu x1 = length(element, "x1", slide_width_);
                const Emu y1 = length(element, "y1", slide_height_);
                const Emu x2 = length(element, "x2", slide_width_);
                const Emu y2 = length(element, "y2", slide_height_);
                return Rect{std::min(x1, x2), std::min(y1, y2), std::abs(x2 - x1), std::abs(y2 - y1)};
            }

            // 두 점을 잇는 선. 방향은 flipH, flipV로 나타낸다
            std::string line_xml(int id, const ir::Element& element, const Common& common) {
                const Rect rect = line_rect(element);
                const bool flip_h = length(element, "x2", slide_width_) < length(element, "x1", slide_width_);
                const bool flip_v = length(element, "y2", slide_height_) < length(element, "y1", slide_height_);
                return "<p:cxnSp><p:nvCxnSpPr>" + cnvpr_xml(id, element, "Connector", common) + "<p:cNvCxnSpPr/><p:nvPr/></p:nvCxnSpPr>"
                    "<p:spPr>" + xfrm_xml(rect, common, flip_h, flip_v) + "<a:prstGeom prst=\"line\"><a:avLst/></a:prstGeom>"
                    + ln_xml(common, true, arrows_xml(element)) + effect_xml(common) + "</p:spPr>" + line_style + "</p:cxnSp>";
            }

            // fit과 crop_*을 srcRect와 그림 틀로 바꾼다. cover는 넘치는 쪽을 가운데 기준으로 자르고, contain은 틀을 줄인다
            std::string picture_xml(int id, const ir::Element& element, const Common& common, const std::string& where) {
                const auto rect = element_rect(element, where);
                const auto* image = std::get_if<ir::Image>(find_property(element, "data"));
                if (image == nullptr) {
                    error(where + ": 'data' must be a picture");
                    return "";
                }
                const std::string* path = &image->path;
                std::array<double, 4> crop = {0, 0, 0, 0}; // 왼쪽, 위, 오른쪽, 아래
                const std::array<const char*, 4> crop_names = {"crop_left", "crop_top", "crop_right", "crop_bottom"};
                for (std::size_t i = 0; i < crop.size(); ++i) {
                    crop[i] = optional_scalar(element, crop_names[i]).value_or(0) / 100;
                }
                const auto media = add_media(*path, where);
                if (!rect || !media) {
                    return "";
                }
                const std::string fit = enum_member(element, "fit");
                const ImageFit fitted = fit_image({static_cast<double>(rect->x), static_cast<double>(rect->y), static_cast<double>(rect->cx), static_cast<double>(rect->cy), crop},
                                                  fit, fit == "cover" || fit == "contain" ? image_size(*path) : std::nullopt);
                const Rect frame{std::llround(fitted.x), std::llround(fitted.y), std::llround(fitted.width), std::llround(fitted.height)};
                crop = fitted.crop;
                std::string source_rect;
                const std::array<const char*, 4> sides = {"l", "t", "r", "b"};
                for (std::size_t i = 0; i < crop.size(); ++i) {
                    if (crop[i] > 0) {
                        source_rect += attribute(sides[i], std::llround(crop[i] * 100000));
                    }
                }
                const std::string kind = enum_member(element, "kind");
                const std::string image_id = relate(relationship_type + "image", "../media/" + *media);
                return "<p:pic><p:nvPicPr>" + cnvpr_xml(id, element, "Picture", common) + "<p:cNvPicPr><a:picLocks noChangeAspect=\"1\"/></p:cNvPicPr><p:nvPr/></p:nvPicPr>"
                    "<p:blipFill><a:blip" + attribute("r:embed", image_id) + alpha_modifier() + (source_rect.empty() ? "" : "<a:srcRect" + source_rect + "/>")
                    + "<a:stretch><a:fillRect/></a:stretch></p:blipFill>"
                    "<p:spPr>" + xfrm_xml(frame, common) + geometry_xml(kind.empty() ? "rect" : kind, frame, common)
                    + common.fill + ln_xml(common, false) + effect_xml(common) + "</p:spPr></p:pic>";
            }

            // ---- 비디오와 오디오

            // PowerPoint가 비디오와 오디오를 넣을 때처럼 그림(포스터) 개체에 파일을 잇는다.
            // 포스터가 없으면 회색 판에 재생 단추(오디오는 스피커)를 그린 그림을 쓴다
            std::string media_xml(int id, const ir::Element& element, const Common& common, const std::string& where) {
                const bool video = element.object == "video";
                const auto rect = element_rect(element, where);
                const auto* path = std::get_if<std::string>(find_property(element, "data"));
                if (!rect || path == nullptr) {
                    return "";
                }
                const auto media = video ? add_file(*path, where, "media", {{".mp4", "video/mp4"}, {".webm", "video/webm"}}, "unsupported video format", "mp4, webm")
                                         : add_file(*path, where, "media", {{".mp3", "audio/mpeg"}, {".wav", "audio/x-wav"}, {".m4a", "audio/mp4"}}, "unsupported audio format", "mp3, wav, m4a");
                if (!media) {
                    return "";
                }
                std::string poster;
                if (const auto* file = std::get_if<ir::Image>(find_property(element, "poster")); file != nullptr && !file->path.empty()) {
                    if (const auto image = add_media(file->path, where + ", poster")) {
                        poster = *image;
                    }
                }
                if (poster.empty()) {
                    poster = add_generated_png(media_placeholder_png(rect->cx, rect->cy, video));
                }
                const Emu trim_start = optional_milliseconds(element, "trim_start");
                const Emu trim_end = optional_milliseconds(element, "trim_end");
                const Emu fade_in = optional_milliseconds(element, "fade_in");
                const Emu fade_out = optional_milliseconds(element, "fade_out");
                long long duration = 0;
                if (const auto bytes = read_file(resolve(*path))) {
                    duration = std::max(0LL, media_duration(*bytes, *media) - trim_start - trim_end);
                }
                media_elements_.push_back({&element, id, duration});

                const std::string media_id = relate("http://schemas.microsoft.com/office/2007/relationships/media", "../media/" + *media);
                const std::string link_id = relate(relationship_type + (video ? "video" : "audio"), "../media/" + *media);
                const std::string poster_id = relate(relationship_type + "image", "../media/" + poster);
                std::string extras;
                if (trim_start > 0 || trim_end > 0) {
                    extras += "<p14:trim" + (trim_start > 0 ? attribute("st", trim_start) : "") + (trim_end > 0 ? attribute("end", trim_end) : "") + "/>";
                }
                if (fade_in > 0 || fade_out > 0) {
                    extras += "<p14:fade" + (fade_in > 0 ? attribute("in", fade_in) : "") + (fade_out > 0 ? attribute("out", fade_out) : "") + "/>";
                }
                const std::string name = element.name.empty() || element.generated_name ? std::string(video ? "Video " : "Audio ") + std::to_string(id - 1) : element.name;
                return "<p:pic><p:nvPicPr><p:cNvPr" + attribute("id", id) + attribute("name", name) + "><a:hlinkClick r:id=\"\" action=\"ppaction://media\"/></p:cNvPr>"
                    "<p:cNvPicPr><a:picLocks noChangeAspect=\"1\"/></p:cNvPicPr><p:nvPr>" + (video ? "<a:videoFile" : "<a:audioFile") + attribute("r:link", link_id) + "/>"
                    "<p:extLst><p:ext uri=\"{DAA4B4D4-6D71-4841-9C94-3DE7FCFB9230}\"><p14:media" + attribute("xmlns:p14", extension_namespaces.at("p14")) + attribute("r:embed", media_id)
                    + (extras.empty() ? "/>" : ">" + extras + "</p14:media>") + "</p:ext></p:extLst></p:nvPr></p:nvPicPr>"
                    "<p:blipFill><a:blip" + attribute("r:embed", poster_id) + alpha_modifier() + "<a:stretch><a:fillRect/></a:stretch></p:blipFill>"
                    "<p:spPr>" + xfrm_xml(*rect, common) + geometry_xml("rect", *rect, common) + common.fill + ln_xml(common, false) + effect_xml(common) + "</p:spPr></p:pic>";
            }

            Emu optional_milliseconds(const ir::Element& element, const std::string& name) const {
                const auto* number = std::get_if<ir::Number>(find_property(element, name));
                return number != nullptr ? to_milliseconds(*number) : 0;
            }

            // 포스터가 없는 비디오와 오디오의 그림. 가로가 최대 480px인 회색 판에 흰 원과 재생 단추(오디오는 스피커)를 그린다
            static std::string media_placeholder_png(Emu cx, Emu cy, bool video) {
                const double aspect = cx > 0 && cy > 0 ? static_cast<double>(cy) / static_cast<double>(cx) : 0.5625;
                const int width = 480;
                const int height = std::clamp(static_cast<int>(std::lround(width * aspect)), 8, 1920);
                raster::Raster raster(width, height);
                const double cx0 = width / 2.0;
                const double cy0 = height / 2.0;
                const double radius = std::min(width, height) * 0.22;
                // 점 (x, y)가 그림 안에 있는지. 0은 바탕, 1은 원, 2는 기호
                const auto shade = [&](double x, double y) {
                    const double dx = (x - cx0) / radius;
                    const double dy = (y - cy0) / radius;
                    if (dx * dx + dy * dy > 1) {
                        return 0;
                    }
                    if (video) {
                        // 오른쪽을 가리키는 삼각형
                        const bool inside = dx > -0.32 && dx < 0.48 && std::abs(dy) < (0.48 - dx) * 0.72;
                        return inside ? 2 : 1;
                    }
                    // 스피커: 사각형 + 사다리꼴 + 두 개의 호
                    const bool body = dx > -0.5 && dx < -0.22 && std::abs(dy) < 0.16;
                    const bool cone = dx >= -0.22 && dx < 0.08 && std::abs(dy) < 0.16 + (dx + 0.22) * 1.2;
                    const double distance = std::hypot(dx - 0.02, dy);
                    const bool wave = dx > 0.14 && std::abs(dy) < dx * 1.1 && ((distance > 0.22 && distance < 0.32) || (distance > 0.42 && distance < 0.52));
                    return body || cone || wave ? 2 : 1;
                };
                const float colors[3][3] = {{90, 94, 102}, {255, 255, 255}, {90, 94, 102}};
                for (int y = 0; y < height; ++y) {
                    for (int x = 0; x < width; ++x) {
                        float sum[3] = {0, 0, 0};
                        // 4x4 표본으로 가장자리를 부드럽게
                        for (int sy = 0; sy < 4; ++sy) {
                            for (int sx = 0; sx < 4; ++sx) {
                                const int which = shade(x + (sx + 0.5) / 4, y + (sy + 0.5) / 4);
                                for (int c = 0; c < 3; ++c) {
                                    sum[c] += colors[which][c] / 16;
                                }
                            }
                        }
                        std::copy(sum, sum + 3, raster.at(x, y));
                    }
                }
                return raster::encode_png(raster);
            }

            // freeform의 상자와 path. path의 좌표는 (x, y)에서 잰 px이다
            struct Freeform {
                Rect rect;
                ParsedPath path;
            };

            std::optional<Freeform> freeform_shape(const ir::Element& element) const {
                const Emu x = length(element, "x", slide_width_);
                const Emu y = length(element, "y", slide_height_);
                const auto* text = std::get_if<std::string>(find_property(element, "path"));
                std::string message;
                auto path = text != nullptr ? parse_svg_path(*text, message) : std::nullopt;
                if (!path) {
                    return std::nullopt;
                }
                const Rect rect{x + std::llround(path->min.x * emu_per_px), y + std::llround(path->min.y * emu_per_px),
                                std::max<Emu>(1, std::llround((path->max.x - path->min.x) * emu_per_px)), std::max<Emu>(1, std::llround((path->max.y - path->min.y) * emu_per_px))};
                return Freeform{rect, std::move(*path)};
            }

            // 연결점은 rect처럼 위(0), 왼쪽(1), 아래(2), 오른쪽(3)에 둔다
            std::string freeform_xml(int id, const ir::Element& element, const Common& common) {
                const auto shape = freeform_shape(element);
                if (!shape) {
                    return "";
                }
                const Rect& rect = shape->rect;
                const auto point = [&](const Point& p) {
                    return "<a:pt" + attribute("x", std::llround((p.x - shape->path.min.x) * emu_per_px)) + attribute("y", std::llround((p.y - shape->path.min.y) * emu_per_px)) + "/>";
                };
                std::string path;
                for (const auto& segment : shape->path.segments) {
                    if (segment.command == 'M') {
                        path += "<a:moveTo>" + point(segment.points[0]) + "</a:moveTo>";
                    } else if (segment.command == 'L') {
                        path += "<a:lnTo>" + point(segment.points[0]) + "</a:lnTo>";
                    } else if (segment.command == 'C') {
                        path += "<a:cubicBezTo>" + point(segment.points[0]) + point(segment.points[1]) + point(segment.points[2]) + "</a:cubicBezTo>";
                    } else {
                        path += "<a:close/>";
                    }
                }
                const auto site = [&](Emu angle, Emu x, Emu y) { return "<a:cxn" + attribute("ang", angle) + "><a:pos" + attribute("x", x) + attribute("y", y) + "/></a:cxn>"; };
                const std::string geometry = "<a:custGeom><a:avLst/><a:gdLst/><a:ahLst/><a:cxnLst>"
                    + site(16200000, rect.cx / 2, 0) + site(10800000, 0, rect.cy / 2) + site(5400000, rect.cx / 2, rect.cy) + site(0, rect.cx, rect.cy / 2)
                    + "</a:cxnLst><a:rect l=\"0\" t=\"0\"" + attribute("r", rect.cx) + attribute("b", rect.cy) + "/>"
                    "<a:pathLst><a:path" + attribute("w", rect.cx) + attribute("h", rect.cy) + ">" + path + "</a:path></a:pathLst></a:custGeom>";
                return "<p:sp><p:nvSpPr>" + cnvpr_xml(id, element, "Freeform", common) + "<p:cNvSpPr/><p:nvPr/></p:nvSpPr>"
                    "<p:spPr>" + xfrm_xml(rect, common) + geometry + styled_fill_xml(common) + ln_xml(common, true) + effect_xml(common) + "</p:spPr>" + shape_style + "</p:sp>";
            }

            // ---- 연결선

            // 연결선의 모양과 두 끝이 붙는 개체의 id
            struct PlannedConnector {
                ConnectorPlan plan;
                int start_id;
                int end_id;
            };

            // 대상은 미들 엔드가 검사했다. 대상의 위치 에러는 대상을 만들 때 알린다
            std::optional<std::pair<int, Anchor>> connector_anchor(const ir::Element& connector, const std::string& property) {
                const auto* name = std::get_if<std::string>(find_property(connector, property));
                const auto it = name != nullptr ? named_.find(*name) : named_.end();
                if (it == named_.end()) {
                    return std::nullopt;
                }
                const ir::Element& target = *it->second;
                const std::size_t error_count = errors_.size();
                std::optional<Rect> rect;
                if (target.object == "freeform") {
                    if (const auto shape = freeform_shape(target)) {
                        rect = shape->rect;
                    }
                } else {
                    rect = element_rect(target, "");
                }
                const Emu rotation = optional_angle(target, "rotation").value_or(0);
                errors_.resize(error_count);
                if (!rect) {
                    return std::nullopt;
                }
                const bool has_kind = target.object == "shape" || target.object == "backdrop" || target.object == "image";
                const std::string kind = has_kind ? enum_member(target, "kind") : "rect";
                const std::string flip = enum_member(target, "flip");
                return std::pair{ids_.at(&target), Anchor{kind.empty() ? "rect" : kind, static_cast<double>(rect->x), static_cast<double>(rect->y), static_cast<double>(rect->cx),
                                                          static_cast<double>(rect->cy), static_cast<double>(rotation) / 60000 * std::numbers::pi / 180,
                                                          flip == "horizontal" || flip == "both", flip == "vertical" || flip == "both"}};
            }

            std::optional<PlannedConnector> plan_connector(const ir::Element& element) {
                const auto from = connector_anchor(element, "from");
                const auto to = connector_anchor(element, "to");
                if (!from || !to) {
                    return std::nullopt;
                }
                const auto plan = geometry::plan_connector(from->second, to->second, enum_member(element, "from_side"), enum_member(element, "to_side"),
                                                           enum_member(element, "kind"), static_cast<double>(connector_margin));
                if (!plan) {
                    return std::nullopt;
                }
                return PlannedConnector{*plan, from->first, to->first};
            }

            std::string connector_xml(int id, const ir::Element& element, const Common& common) {
                const auto plan = plan_connector(element);
                if (!plan) {
                    return "";
                }
                const ConnectorGeometry& geometry = plan->plan.geometry;
                std::string attributes;
                if (geometry.rotation != 0) {
                    attributes += attribute("rot", static_cast<Emu>(geometry.rotation) * 60000);
                }
                attributes += geometry.flip_h ? " flipH=\"1\"" : "";
                attributes += geometry.flip_v ? " flipV=\"1\"" : "";
                std::string adjust;
                for (std::size_t i = 0; i < geometry.adjust.size(); ++i) {
                    adjust += "<a:gd" + attribute("name", "adj" + std::to_string(i + 1)) + attribute("fmla", "val " + std::to_string(geometry.adjust[i])) + "/>";
                }
                return "<p:cxnSp><p:nvCxnSpPr>" + cnvpr_xml(id, element, "Connector", common) + "<p:cNvCxnSpPr>"
                    "<a:stCxn" + attribute("id", plan->start_id) + attribute("idx", plan->plan.start.site) + "/>"
                    "<a:endCxn" + attribute("id", plan->end_id) + attribute("idx", plan->plan.end.site) + "/></p:cNvCxnSpPr><p:nvPr/></p:nvCxnSpPr>"
                    "<p:spPr><a:xfrm" + attributes + "><a:off" + attribute("x", std::llround(geometry.x)) + attribute("y", std::llround(geometry.y)) + "/>"
                    "<a:ext" + attribute("cx", std::llround(geometry.width)) + attribute("cy", std::llround(geometry.height)) + "/></a:xfrm>"
                    "<a:prstGeom" + attribute("prst", geometry.preset) + ">" + (adjust.empty() ? "<a:avLst/>" : "<a:avLst>" + adjust + "</a:avLst>") + "</a:prstGeom>"
                    + ln_xml(common, true, arrows_xml(element)) + effect_xml(common) + "</p:spPr>" + line_style + "</p:cxnSp>";
            }

            // ---- 그룹

            // 그룹 상자를 정하려고 재는 element의 상자. 위치 에러는 element를 만들 때 알린다
            std::optional<Rect> element_bounds(const ir::Element& element) {
                const std::size_t error_count = errors_.size();
                std::optional<Rect> result;
                if (element.object == "group") {
                    for (const auto& child : element.children) {
                        const auto rect = element_bounds(child);
                        if (!rect) {
                            continue;
                        }
                        if (!result) {
                            result = rect;
                            continue;
                        }
                        const Emu right = std::max(result->x + result->cx, rect->x + rect->cx);
                        const Emu bottom = std::max(result->y + result->cy, rect->y + rect->cy);
                        result->x = std::min(result->x, rect->x);
                        result->y = std::min(result->y, rect->y);
                        result->cx = right - result->x;
                        result->cy = bottom - result->y;
                    }
                } else if (element.object == "line") {
                    result = line_rect(element);
                } else if (element.object == "freeform") {
                    if (const auto shape = freeform_shape(element)) {
                        result = shape->rect;
                    }
                } else if (element.object == "connector") {
                    if (const auto plan = plan_connector(element)) {
                        const ConnectorGeometry& g = plan->plan.geometry;
                        // 90도나 270도 돌린 연결선은 가로세로가 바뀐다
                        const bool turned = g.rotation % 180 != 0;
                        const double width = turned ? g.height : g.width;
                        const double height = turned ? g.width : g.height;
                        const double center_x = g.x + g.width / 2;
                        const double center_y = g.y + g.height / 2;
                        result = Rect{std::llround(center_x - width / 2), std::llround(center_y - height / 2), std::llround(width), std::llround(height)};
                    }
                } else {
                    result = element_rect(element, "");
                }
                errors_.resize(error_count);
                return result;
            }

            // 자식의 좌표는 슬라이드 좌표 그대로 두고, 그룹 상자를 자식들을 감싸는 크기로 맞춘다
            std::string group_xml(int id, const ir::Element& element, const std::string& where) {
                std::string children;
                for (const auto& child : element.children) {
                    children += element_xml(child, where);
                }
                const auto bounds = element_bounds(element);
                if (!bounds) {
                    return children;
                }
                const std::string name = element.name.empty() || element.generated_name ? "Group " + std::to_string(id - 1) : element.name;
                const std::string offset = attribute("x", bounds->x) + attribute("y", bounds->y);
                const std::string size = attribute("cx", bounds->cx) + attribute("cy", bounds->cy);
                return "<p:grpSp><p:nvGrpSpPr><p:cNvPr" + attribute("id", id) + attribute("name", name) + "/><p:cNvGrpSpPr/><p:nvPr/></p:nvGrpSpPr>"
                    "<p:grpSpPr><a:xfrm><a:off" + offset + "/><a:ext" + size + "/><a:chOff" + offset + "/><a:chExt" + size + "/></a:xfrm></p:grpSpPr>"
                    + children + "</p:grpSp>";
            }

            // ---- 링크

            // URL, 다른 슬라이드, 이전/다음 같은 이동
            std::string hyperlink_xml(const ir::Link& link) {
                if (!link.url.empty()) {
                    return "<a:hlinkClick" + attribute("r:id", relate(relationship_type + "hyperlink", link.url, true)) + "/>";
                }
                if (link.slide > 0) {
                    const std::string target = std::string(in_layout_ ? "../slides/" : "") + "slide" + std::to_string(link.slide) + ".xml";
                    return "<a:hlinkClick" + attribute("r:id", relate(relationship_type + "slide", target)) + " action=\"ppaction://hlinksldjump\"/>";
                }
                const std::string jump = show_jump(link.jump);
                if (jump.empty()) {
                    return "";
                }
                return "<a:hlinkClick r:id=\"\"" + attribute("action", "ppaction://hlinkshowjump?jump=" + jump) + "/>";
            }

            // 실행 설정 하나. tag는 a:hlinkClick, a:hlinkHover(개체) 또는 a:hlinkMouseOver(글자)이다.
            // 동작 없이 소리나 강조만 있으면 PowerPoint처럼 ppaction://noaction이다. run(...)은 PowerPoint에 없으므로 뺀다 (경고는 target_warnings가 낸다)
            std::string interaction_xml(const std::string& tag, const std::optional<ir::Action>& action, const std::string& sound, bool highlight, const std::string& where) {
                std::string id;
                std::string verb;
                bool acts = false;
                if (action && action->kind == "link") {
                    if (sound.empty() && !highlight) {
                        const std::string link = hyperlink_xml(action->link);
                        return link.empty() ? "" : "<" + tag + link.substr(std::string("<a:hlinkClick").size());
                    }
                    if (!action->link.url.empty()) {
                        id = relate(relationship_type + "hyperlink", action->link.url, true);
                        acts = true;
                    } else if (action->link.slide > 0) {
                        id = relate(relationship_type + "slide", std::string(in_layout_ ? "../slides/" : "") + "slide" + std::to_string(action->link.slide) + ".xml");
                        verb = "ppaction://hlinksldjump";
                        acts = true;
                    } else if (const auto jump = show_jump(action->link.jump); !jump.empty()) {
                        verb = "ppaction://hlinkshowjump?jump=" + jump;
                        acts = true;
                    }
                } else if (action && (action->kind == "program" || action->kind == "file")) {
                    id = relate(relationship_type + "hyperlink", file_target(action->target), true);
                    if (action->kind == "program") {
                        verb = "ppaction://program";
                    } else {
                        std::string extension = display(utf8_path(action->target).extension());
                        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                        const bool presentation = extension == ".pptx" || extension == ".ppt" || extension == ".pptm" || extension == ".ppsx" || extension == ".pps" || extension == ".ppsm";
                        verb = presentation ? "ppaction://hlinkpres?slideindex=1&slidetitle=" : "ppaction://hlinkfile";
                    }
                    acts = true;
                } else if (action && action->kind == "macro") {
                    verb = "ppaction://macro?name=" + action->target;
                    acts = true;
                }
                std::string children;
                if (!sound.empty()) {
                    if (const auto media = add_audio(sound, where)) {
                        children = "<a:snd" + attribute("r:embed", relate(relationship_type + "audio", "../media/" + *media)) + attribute("name", display(utf8_path(sound).filename())) + "/>";
                    }
                }
                if (!acts && children.empty() && !highlight) {
                    return "";
                }
                return "<" + tag + " r:id=\"" + id + "\"" + (acts && verb.empty() ? "" : attribute("action", acts ? verb : "ppaction://noaction"))
                    + (highlight ? " highlightClick=\"1\"" : "") + (children.empty() ? "/>" : ">" + children + "</" + tag + ">");
            }

            // slide_jump 값 -> hlinkshowjump의 jump. 모르는 값이면 빈 문자열
            static std::string show_jump(const std::string& jump) {
                static const std::map<std::string, std::string> jumps = {
                    {"next_slide", "nextslide"}, {"previous_slide", "previousslide"}, {"first_slide", "firstslide"}, {"last_slide", "lastslide"},
                    {"last_viewed_slide", "lastslideviewed"}, {"end_show", "endshow"},
                };
                const auto it = jumps.find(jump);
                return it == jumps.end() ? "" : it->second;
            }

            // program(...)과 file(...)의 경로(.tlide 기준)를 pptx 파일에서 본 주소로. PowerPoint은 상대 주소를 프레젠테이션의 폴더에서 찾는다
            std::string file_target(const std::string& path) const {
                const std::filesystem::path file = resolve(path).lexically_normal();
                const std::filesystem::path folder = (base_dir_ / utf8_path(target_.path)).lexically_normal().parent_path();
                // 절대 경로로 적었으면 file:/// 주소로 둔다
                const std::filesystem::path relative = utf8_path(path).is_absolute() ? std::filesystem::path() : file.lexically_relative(folder);
                std::string text = relative.empty() ? "file:///" + display(file.generic_u8string()) : display(relative.generic_u8string());
                std::string result;
                for (const char c : text) {
                    if (c == ' ') {
                        result += "%20";
                    } else if (c == '%') {
                        result += "%25";
                    } else if (c == '#') {
                        result += "%23";
                    } else {
                        result += c;
                    }
                }
                return result;
            }

            // ---- 배경과 backdrop

            // slide나 layout의 배경. 그림은 비율을 지키며 슬라이드를 빈틈없이 채우고, 넘치는 쪽은 가운데를 기준으로 자른다
            std::string background_xml(const ir::Value& background, const std::string& where) {
                const auto* image = std::get_if<ir::Image>(&background);
                if (image == nullptr) {
                    return "<p:bg><p:bgPr>" + fill_xml(background, where + ", background") + "<a:effectLst/></p:bgPr></p:bg>";
                }
                const auto media = add_media(image->path, where + ", background");
                if (!media) {
                    return "";
                }
                const std::string id = relate(relationship_type + "image", "../media/" + *media);
                std::string crop;
                if (const auto size = image_size(image->path)) {
                    const double image_ratio = static_cast<double>(size->first) / size->second;
                    const double slide_ratio = static_cast<double>(slide_width_) / static_cast<double>(slide_height_);
                    if (image_ratio > slide_ratio) {
                        const Emu side = std::llround((1 - slide_ratio / image_ratio) / 2 * 100000);
                        crop = attribute("l", side) + attribute("r", side);
                    } else if (image_ratio < slide_ratio) {
                        const Emu side = std::llround((1 - image_ratio / slide_ratio) / 2 * 100000);
                        crop = attribute("t", side) + attribute("b", side);
                    }
                }
                return "<p:bg><p:bgPr><a:blipFill dpi=\"0\" rotWithShape=\"1\"><a:blip" + attribute("r:embed", id) + "/>"
                       "<a:srcRect" + crop + "/><a:stretch><a:fillRect/></a:stretch></a:blipFill><a:effectLst/></p:bgPr></p:bg>";
            }

            // backdrop object. slide나 layout의 배경에서 자기 자리를 잘라 blur만큼 흐리게 한 그림이다
            std::string backdrop_xml(int id, const ir::Element& element, const Common& common, const std::string& where) {
                const auto rect = element_rect(element, where);
                const auto blur = optional_length(element, "blur");
                if (!rect || !blur) {
                    return "";
                }
                const std::string kind = enum_member(element, "kind");
                const std::string properties = xfrm_xml(*rect, common) + geometry_xml(kind, *rect, common);
                const std::string outline = ln_xml(common, false) + effect_xml(common);

                // 단색 배경은 흐려도 같은 색이다
                const auto* color = background_ != nullptr ? std::get_if<ir::Color>(background_) : nullptr;
                if (background_ == nullptr || color != nullptr) {
                    const auto rgb = opaque(color != nullptr ? *color : ir::Color{255, 255, 255, 1, ""});
                    const ir::Color fill{static_cast<int>(std::lround(rgb[0])), static_cast<int>(std::lround(rgb[1])), static_cast<int>(std::lround(rgb[2])), 1, ""};
                    return "<p:sp><p:nvSpPr>" + cnvpr_xml(id, element, "Backdrop", common) + "<p:cNvSpPr/><p:nvPr/></p:nvSpPr>"
                        "<p:spPr>" + properties + solid_fill_xml(fill) + outline + "</p:spPr></p:sp>";
                }

                const Backdrop* backdrop = blurred_background(*background_, *blur, where);
                if (backdrop == nullptr) {
                    return "";
                }
                // 흐린 배경이라 해상도를 backdrop과 같게 낮춰도 된다. 돌리거나 뒤집은 도형이면 그 자리의 배경을 가져온다
                const double width = static_cast<double>(rect->cx) / emu_per_px;
                const double height = static_cast<double>(rect->cy) / emu_per_px;
                const double center_x = (static_cast<double>(rect->x) + static_cast<double>(rect->cx) / 2) / emu_per_px;
                const double center_y = (static_cast<double>(rect->y) + static_cast<double>(rect->cy) / 2) / emu_per_px;
                const double angle = static_cast<double>(common.rotation.value_or(0)) / 60000 * std::numbers::pi / 180;
                raster::Raster piece(std::max(1, static_cast<int>(std::ceil(width / backdrop->step))), std::max(1, static_cast<int>(std::ceil(height / backdrop->step))));
                for (int y = 0; y < piece.height; ++y) {
                    for (int x = 0; x < piece.width; ++x) {
                        double dx = (x + 0.5) / piece.width * width - width / 2;
                        double dy = (y + 0.5) / piece.height * height - height / 2;
                        if (common.flip_h) {
                            dx = -dx;
                        }
                        if (common.flip_v) {
                            dy = -dy;
                        }
                        const double slide_x = center_x + dx * std::cos(angle) - dy * std::sin(angle);
                        const double slide_y = center_y + dx * std::sin(angle) + dy * std::cos(angle);
                        raster::sample(backdrop->raster, slide_x / backdrop->step - 0.5, slide_y / backdrop->step - 0.5, piece.at(x, y));
                    }
                }
                const std::string media = add_generated_png(raster::encode_png(piece));
                const std::string image_id = relate(relationship_type + "image", "../media/" + media);
                return "<p:pic><p:nvPicPr>" + cnvpr_xml(id, element, "Backdrop", common) + "<p:cNvPicPr/><p:nvPr/></p:nvPicPr>"
                    "<p:blipFill><a:blip" + attribute("r:embed", image_id) + alpha_modifier() + "<a:stretch><a:fillRect/></a:stretch></p:blipFill>"
                    "<p:spPr>" + properties + outline + "</p:spPr></p:pic>";
            }

            // 배경(그라데이션, 무늬, 그림)을 blur만큼 흐린 것. 같은 배경과 흐림이면 한 번만 만든다
            const Backdrop* blurred_background(const ir::Value& background, Emu blur, const std::string& where) {
                const auto* gradient = std::get_if<ir::Gradient>(&background);
                const auto* pattern = std::get_if<ir::Pattern>(&background);
                const auto* image = std::get_if<ir::Image>(&background);
                std::string key;
                if (gradient != nullptr) {
                    key = ir::format_gradient(*gradient);
                } else if (pattern != nullptr) {
                    key = "pattern " + pattern->kind + " " + ir::format_color(pattern->foreground) + " " + ir::format_color(pattern->background);
                } else if (image != nullptr) {
                    key = "image " + display(resolve(image->path));
                } else {
                    return nullptr;
                }
                key += " " + std::to_string(blur) + (theme_ != nullptr ? " " + theme_->name : "");
                if (const auto it = backdrops_.find(key); it != backdrops_.end()) {
                    return &it->second;
                }
                // 흐림이 클수록 성기게 찍는다
                const double sigma = static_cast<double>(blur) / emu_per_px;
                const double step = std::clamp(sigma / 4, 1.0, 4.0);
                const double width = static_cast<double>(slide_width_) / emu_per_px;
                const double height = static_cast<double>(slide_height_) / emu_per_px;
                raster::Raster raster(static_cast<int>(std::ceil(width / step)), static_cast<int>(std::ceil(height / step)));
                if (gradient != nullptr) {
                    draw_gradient(raster, *gradient, step, width, height);
                } else if (pattern != nullptr) {
                    draw_pattern(raster, *pattern);
                } else if (!draw_image(raster, image->path, step, width, height, where)) {
                    return nullptr;
                }
                raster::blur(raster, sigma / step);
                return &backdrops_.emplace(key, Backdrop{std::move(raster), step}).first->second;
            }

            // t(0 ~ 1) 위치의 색. smooth면 PowerPoint의 두 색 그라데이션처럼 선형광에서 곡선으로 섞는다
            std::array<float, 3> gradient_color(const ir::Gradient& gradient, const std::vector<double>& positions, bool smooth, double t) const {
                if (t <= positions.front()) {
                    return opaque(gradient.colors.front());
                }
                for (std::size_t i = 0; i + 1 < positions.size(); ++i) {
                    if (t <= positions[i + 1]) {
                        const double span = positions[i + 1] - positions[i];
                        const double ratio = span <= 0 ? 1 : (t - positions[i]) / span;
                        const auto from = opaque(gradient.colors[i]);
                        const auto to = opaque(gradient.colors[i + 1]);
                        std::array<float, 3> color{};
                        for (std::size_t c = 0; c < color.size(); ++c) {
                            if (smooth) {
                                const double a = srgb_to_linear(from[c] / 255.0);
                                const double b = srgb_to_linear(to[c] / 255.0);
                                color[c] = static_cast<float>(linear_to_srgb(a + (b - a) * gradient_curve(ratio)) * 255);
                            } else {
                                color[c] = static_cast<float>(from[c] + (to[c] - from[c]) * ratio);
                            }
                        }
                        return color;
                    }
                }
                return opaque(gradient.colors.back());
            }

            // linear는 DrawingML의 lin처럼 각도 방향으로 슬라이드 끝에서 끝까지, radial은 가운데에서 모서리까지 색이 바뀐다
            void draw_gradient(raster::Raster& raster, const ir::Gradient& gradient, double step, double width, double height) {
                const double angle = static_cast<double>(to_direction(gradient.angle)) / 60000 * std::numbers::pi / 180;
                const auto positions = gradient_stops(gradient);
                const bool smooth = smooth_gradient(gradient);
                const double direction_x = std::cos(angle);
                const double direction_y = std::sin(angle);
                const double span = std::abs(width * direction_x) + std::abs(height * direction_y);
                const double corner = std::hypot(width / 2, height / 2);
                for (int y = 0; y < raster.height; ++y) {
                    for (int x = 0; x < raster.width; ++x) {
                        const double px = (x + 0.5) * step - width / 2;
                        const double py = (y + 0.5) * step - height / 2;
                        const double t = gradient.radial ? std::hypot(px, py) / corner : (px * direction_x + py * direction_y) / span + 0.5;
                        const auto color = gradient_color(gradient, positions, smooth, std::clamp(t, 0.0, 1.0));
                        std::copy(color.begin(), color.end(), raster.at(x, y));
                    }
                }
            }

            // 무늬는 흐리면 두 색이 섞인 단색이 된다
            void draw_pattern(raster::Raster& raster, const ir::Pattern& pattern) {
                const auto it = pattern_presets().find(pattern.kind);
                const auto density = static_cast<float>(it != pattern_presets().end() ? it->second.second : 0.5);
                const auto foreground = opaque(pattern.foreground);
                const auto background = opaque(pattern.background);
                for (int y = 0; y < raster.height; ++y) {
                    for (int x = 0; x < raster.width; ++x) {
                        for (int c = 0; c < 3; ++c) {
                            raster.at(x, y)[c] = foreground[c] * density + background[c] * (1 - density);
                        }
                    }
                }
            }

            // background_xml과 같은 방식으로 그림을 슬라이드에 맞춘다
            bool draw_image(raster::Raster& raster, const std::string& path, double step, double width, double height, const std::string& where) {
                const raster::Raster* source = decoded_image(path, where);
                if (source == nullptr) {
                    return false;
                }
                const double scale = std::max(width / source->width, height / source->height); // 원본 한 픽셀의 슬라이드 px 크기
                const double left = (width - source->width * scale) / 2;
                const double top = (height - source->height * scale) / 2;
                // 한 칸이 원본의 여러 픽셀을 덮으면 원본을 먼저 반씩 줄여 둔다
                const raster::Raster* current = source;
                raster::Raster reduced;
                while (step / scale * current->width / source->width >= 2 && current->width > 1 && current->height > 1) {
                    reduced = raster::halve(*current);
                    current = &reduced;
                }
                const double ratio_x = static_cast<double>(current->width) / source->width;
                const double ratio_y = static_cast<double>(current->height) / source->height;
                for (int y = 0; y < raster.height; ++y) {
                    for (int x = 0; x < raster.width; ++x) {
                        const double u = ((x + 0.5) * step - left) / scale * ratio_x;
                        const double v = ((y + 0.5) * step - top) / scale * ratio_y;
                        raster::sample(*current, u - 0.5, v - 0.5, raster.at(x, y));
                    }
                }
                return true;
            }

            std::optional<std::pair<int, int>> image_size(const std::string& path) const {
                const auto bytes = read_file(resolve(path));
                return bytes ? raster::image_size(*bytes) : std::nullopt;
            }

            // 파일을 열지 못한 에러는 add_media가 알린다
            const raster::Raster* decoded_image(const std::string& path, const std::string& where) {
                const std::filesystem::path file = resolve(path);
                auto it = decoded_.find(file);
                if (it == decoded_.end()) {
                    std::optional<raster::Raster> raster;
                    if (const auto bytes = read_file(file)) {
                        raster = raster::decode(*bytes);
                        if (!raster) {
                            error(where + ": cannot read image " + display(file) + " for backdrop");
                        }
                    }
                    it = decoded_.emplace(file, std::move(raster)).first;
                }
                return it->second ? &*it->second : nullptr;
            }

            // ---- 글자

            // align은 text-align이 없는 문단의 정렬 (l, ctr, r, just)이고 빈 문자열이면 물려받는다
            std::string text_body_xml(const ir::Text& text, const std::string& body_properties, const std::string& align, const std::string& list_style = "<a:lstStyle/>") {
                std::string xml = "<p:txBody>" + body_properties + list_style;
                if (text.paragraphs.empty()) {
                    xml += "<a:p><a:endParaRPr/></a:p>";
                }
                for (const auto& paragraph : text.paragraphs) {
                    xml += paragraph_xml(paragraph, align);
                }
                return xml + "</p:txBody>";
            }

            static std::string alignment(const ir::TextStyle& style) {
                static const std::map<std::string, std::string> aligns = {{"left", "l"}, {"center", "ctr"}, {"right", "r"}, {"justify", "just"}};
                if (!style.text_align) {
                    return "";
                }
                const auto it = aligns.find(style.text_align->member);
                return it != aligns.end() ? it->second : "";
            }

            // 문단 단위 값은 그 값을 가진 첫 run의 것을 쓴다
            template <typename T>
            static const T* paragraph_value(const ir::Paragraph& paragraph, std::optional<T> ir::TextStyle::* member) {
                for (const auto& run : paragraph.runs) {
                    if (run.style.*member) {
                        return &*(run.style.*member);
                    }
                }
                return nullptr;
            }

            // 1/100pt 단위의 길이
            static Emu to_hundredths(const ir::Number& number) {
                return std::llround(static_cast<double>(to_emu(number)) / 127);
            }

            std::string paragraph_xml(const ir::Paragraph& paragraph, const std::string& default_align) {
                std::string align = default_align;
                if (const auto* value = paragraph_value(paragraph, &ir::TextStyle::text_align)) {
                    ir::TextStyle style;
                    style.text_align = *value;
                    align = alignment(style);
                }
                std::string attributes = align.empty() ? "" : attribute("algn", align);
                std::string children;
                if (const auto* line_height = paragraph_value(paragraph, &ir::TextStyle::line_height)) {
                    children += "<a:lnSpc><a:spcPct" + attribute("val", std::llround(number_sum(*line_height) * 100000)) + "/></a:lnSpc>";
                }
                for (const auto& [member, tag] : {std::pair{&ir::TextStyle::space_before, "spcBef"}, std::pair{&ir::TextStyle::space_after, "spcAft"}}) {
                    if (const auto* value = paragraph_value(paragraph, member)) {
                        children += std::string("<a:") + tag + "><a:spcPts" + attribute("val", to_hundredths(*value)) + "/></a:" + tag + ">";
                    }
                }
                std::optional<Emu> margin;
                std::optional<Emu> indent;
                if (const auto* value = paragraph_value(paragraph, &ir::TextStyle::margin_left)) {
                    margin = to_emu(*value);
                }
                if (const auto* value = paragraph_value(paragraph, &ir::TextStyle::text_indent)) {
                    indent = to_emu(*value);
                }
                if (paragraph.list == ir::ListKind::NONE) {
                    children += "<a:buNone/>";
                } else {
                    const int level = std::clamp(paragraph.level, 0, 8);
                    margin = margin.value_or(list_indent * (level + 1));
                    indent = indent.value_or(-list_indent);
                    attributes += attribute("lvl", level);
                    if (const auto* color = paragraph_value(paragraph, &ir::TextStyle::list_marker_color)) {
                        children += "<a:buClr>" + color_xml(*color) + "</a:buClr>";
                    }
                    if (paragraph.list == ir::ListKind::NUMBERS) {
                        static const std::map<std::string, std::string> styles = {
                            {"decimal", "arabicPeriod"}, {"lower_alpha", "alphaLcPeriod"}, {"upper_alpha", "alphaUcPeriod"},
                            {"lower_roman", "romanLcPeriod"}, {"upper_roman", "romanUcPeriod"}, {"circled", "circleNumDbPlain"},
                        };
                        std::string type = "arabicPeriod";
                        if (const auto* style = paragraph_value(paragraph, &ir::TextStyle::list_style)) {
                            type = styles.at(style->member);
                        }
                        std::string start;
                        if (const auto* value = paragraph_value(paragraph, &ir::TextStyle::list_start)) {
                            start = attribute("startAt", std::llround(number_sum(*value)));
                        }
                        children += "<a:buAutoNum" + attribute("type", type) + start + "/>";
                    } else {
                        std::string marker = paragraph.list == ir::ListKind::BULLETS ? "\xE2\x80\xA2" : "\xE2\x80\x93";
                        if (const auto* value = paragraph_value(paragraph, &ir::TextStyle::list_marker)) {
                            marker = *value;
                        }
                        children += "<a:buFont typeface=\"Arial\"/><a:buChar" + attribute("char", marker) + "/>";
                    }
                }
                if (margin) {
                    attributes += attribute("marL", *margin);
                }
                if (indent) {
                    attributes += attribute("indent", *indent);
                }

                std::string xml = "<a:p><a:pPr" + attributes + ">" + children + "</a:pPr>";
                for (const auto& run : paragraph.runs) {
                    xml += run_xml(run);
                }
                if (paragraph.runs.empty()) {
                    xml += "<a:endParaRPr/>";
                }
                return xml + "</a:p>";
            }

            // '\n'은 같은 문단 안의 줄바꿈(a:br)이고, 필드는 a:fld다
            std::string run_xml(const ir::Run& run) {
                const std::string properties = run_properties_xml(run.style);
                if (!run.field.empty()) {
                    return "<a:fld" + attribute("id", guid(++guid_count_)) + attribute("type", run.field) + ">" + properties + "<a:t>" + escape(run.text) + "</a:t></a:fld>";
                }
                std::string xml;
                std::size_t start = 0;
                while (true) {
                    const std::size_t end = run.text.find('\n', start);
                    const std::string piece = run.text.substr(start, end == std::string::npos ? std::string::npos : end - start);
                    if (!piece.empty()) {
                        xml += "<a:r>" + properties + "<a:t>" + escape(piece) + "</a:t></a:r>";
                    }
                    if (end == std::string::npos) {
                        break;
                    }
                    xml += properties.empty() ? "<a:br/>" : "<a:br>" + properties + "</a:br>";
                    start = end + 1;
                }
                return xml;
            }

            // tag는 a:rPr 또는 개체 틀의 기본 모양인 a:defRPr
            std::string run_properties_xml(const ir::TextStyle& style, const std::string& tag = "a:rPr") {
                std::string attributes;
                std::string children;
                // 글자 크기는 1/100pt 단위
                if (style.font_size) {
                    attributes += attribute("sz", to_hundredths(*style.font_size));
                }
                if (style.font_weight) {
                    attributes += attribute("b", style.font_weight->member == "bold" ? "1" : "0");
                }
                if (style.font_style) {
                    attributes += attribute("i", style.font_style->member == "italic" ? "1" : "0");
                }
                if (style.text_decoration) {
                    static const std::map<std::string, std::string> decorations = {
                        {"none", " u=\"none\" strike=\"noStrike\""}, {"underline", " u=\"sng\""}, {"double_underline", " u=\"dbl\""},
                        {"wavy_underline", " u=\"wavy\""}, {"line_through", " strike=\"sngStrike\""}, {"double_line_through", " strike=\"dblStrike\""},
                    };
                    attributes += decorations.at(style.text_decoration->member);
                }
                if (style.text_transform) {
                    const std::string& value = style.text_transform->member;
                    attributes += attribute("cap", value == "uppercase" ? "all" : value == "small_caps" ? "small" : "none");
                }
                if (style.letter_spacing) {
                    attributes += attribute("spc", to_hundredths(*style.letter_spacing));
                }
                if (style.vertical_align) {
                    const std::string& value = style.vertical_align->member;
                    attributes += attribute("baseline", value == "super" ? "30000" : value == "sub" ? "-25000" : "0");
                }
                if (style.color) {
                    children += solid_fill_xml(*style.color);
                } else if (opacity_ < 1) {
                    children += "<a:solidFill>" + color_xml(scheme(default_text_color_)) + "</a:solidFill>";
                }
                if (style.highlight) {
                    children += "<a:highlight>" + color_xml(*style.highlight) + "</a:highlight>";
                }
                if (style.font_family) {
                    // theme.heading_font, theme.body_font는 +mj, +mn으로 들어온다
                    const std::string& family = *style.font_family;
                    const bool themed = family == "+mj" || family == "+mn";
                    children += "<a:latin" + attribute("typeface", themed ? family + "-lt" : family) + "/><a:ea" + attribute("typeface", themed ? family + "-ea" : family)
                        + "/><a:cs" + attribute("typeface", themed ? family + "-cs" : family) + "/>";
                }
                if (style.link && tag == "a:rPr") {
                    children += hyperlink_xml(*style.link);
                } else if (style.action && tag == "a:rPr") {
                    children += interaction_xml("a:hlinkClick", style.action, "", false, "");
                }
                if (style.hover_action && tag == "a:rPr") {
                    children += interaction_xml("a:hlinkMouseOver", style.hover_action, "", false, "");
                }
                if (attributes.empty() && children.empty()) {
                    return tag == "a:rPr" ? "" : "<" + tag + "/>";
                }
                return "<" + tag + attributes + ">" + children + "</" + tag + ">";
            }

            // ---- 미디어

            // 같은 파일은 한 번만 넣는다. ppt/media 안에서의 파일 이름을 돌려준다
            std::optional<std::string> add_media(const std::string& path, const std::string& where) {
                return add_file(path, where, "image", {{".png", "image/png"}, {".jpg", "image/jpeg"}, {".jpeg", "image/jpeg"}, {".gif", "image/gif"}, {".bmp", "image/bmp"}},
                                "unsupported image format", "png, jpg, gif, bmp");
            }

            std::optional<std::string> add_audio(const std::string& path, const std::string& where) {
                return add_file(path, where, "audio", {{".wav", "audio/x-wav"}}, "unsupported sound format", "wav");
            }

            std::optional<std::string> add_file(const std::string& path, const std::string& where, const std::string& prefix,
                                                 const std::map<std::string, std::string>& types, const std::string& unsupported, const std::string& supported) {
                const std::filesystem::path file = resolve(path);
                std::error_code error_code;
                std::filesystem::path key = std::filesystem::weakly_canonical(file, error_code);
                if (error_code) {
                    key = file;
                }
                if (const auto it = media_.find(key); it != media_.end()) {
                    return it->second;
                }

                std::string extension = display(file.extension());
                std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                const auto type = types.find(extension);
                if (type == types.end()) {
                    error(where + ": " + unsupported + " '" + extension + "' (" + supported + ")");
                    return std::nullopt;
                }
                auto bytes = read_file(file);
                if (!bytes) {
                    error(where + ": cannot open " + prefix + " file " + display(file));
                    return std::nullopt;
                }
                const std::string name = prefix + std::to_string(++media_count_) + extension;
                add_part("ppt/media/" + name, std::move(*bytes));
                defaults_[extension.substr(1)] = type->second;
                media_.emplace(key, name);
                return name;
            }

            // 만든 png를 ppt/media에 넣고 그 이름을 돌려준다
            std::string add_generated_png(std::string bytes) {
                const std::string name = "image" + std::to_string(++media_count_) + ".png";
                add_part("ppt/media/" + name, std::move(bytes));
                defaults_["png"] = "image/png";
                return name;
            }

            // ---- 전환

            // 시간을 지정했거나 PowerPoint 2010 이후의 전환이면 호환용 AlternateContent로 감싼다.
            // 자동으로 넘기는 시간과 전환 소리는 전환이 없어도 p:transition에 적는다
            std::string transition_xml(const ir::Slide& slide, const std::string& where) {
                std::string advance;
                if (slide.advance_after) {
                    advance = attribute("advTm", to_milliseconds(*slide.advance_after));
                }
                std::string sound;
                if (slide.transition_sound) {
                    if (const auto media = add_audio(*slide.transition_sound, where + ", transition_sound")) {
                        const std::string id = relate(relationship_type + "audio", "../media/" + *media);
                        sound = "<p:sndAc><p:stSnd><p:snd" + attribute("r:embed", id) + attribute("name", display(utf8_path(*slide.transition_sound).filename())) + "/></p:stSnd></p:sndAc>";
                    }
                }
                if (!slide.transition) {
                    return advance.empty() && sound.empty() ? "" : "<p:transition" + advance + ">" + sound + "</p:transition>";
                }
                const ir::Transition& transition = *slide.transition;
                const std::string key = transition.kind + "." + transition.option;
                const auto it = transition_table().find(key);
                if (it == transition_table().end()) {
                    error(where + ": the pptx backend does not support transition " + transition.kind + (transition.option.empty() ? "" : "." + transition.option));
                    return "";
                }
                const TransitionXml& xml = it->second;
                std::optional<Emu> duration;
                if (transition.duration) {
                    duration = to_milliseconds(*transition.duration);
                }
                std::string speed;
                if (duration) {
                    speed = *duration <= 500 ? " spd=\"fast\"" : *duration <= 750 ? " spd=\"med\"" : " spd=\"slow\"";
                }
                if (xml.ns == "p" && !duration) {
                    return "<p:transition" + advance + ">" + xml.element + sound + "</p:transition>";
                }

                const std::string requires_ns = xml.ns == "p" ? "p14" : xml.ns;
                std::string duration_attribute;
                if (duration) {
                    // p14가 아닌 Choice에서는 p14 namespace를 따로 선언해야 한다
                    duration_attribute = (requires_ns == "p14" ? "" : attribute("xmlns:p14", extension_namespaces.at("p14"))) + attribute("p14:dur", *duration);
                }
                const std::string fallback = xml.ns == "p" ? xml.element : "<p:fade/>";
                return "<mc:AlternateContent xmlns:mc=\"http://schemas.openxmlformats.org/markup-compatibility/2006\">"
                    "<mc:Choice" + attribute("xmlns:" + requires_ns, extension_namespaces.at(requires_ns)) + attribute("Requires", requires_ns) + ">"
                    "<p:transition" + speed + duration_attribute + advance + ">" + xml.element + sound + "</p:transition></mc:Choice>"
                    "<mc:Fallback><p:transition" + speed + advance + ">" + fallback + sound + "</p:transition></mc:Fallback></mc:AlternateContent>";
            }

            // ---- 애니메이션

            // animate 문장들. 클릭할 때마다 시작하는 묶음이 있고, 그 안에 after_previous마다 이어지는 시간 묶음이 있다
            std::string timing_xml(const ir::Slide& slide, const std::string& where) {
                struct Step {
                    long long start = 0;
                    long long end = 0;
                    std::string effects;
                };
                struct Click {
                    bool automatic = false; // 첫 효과가 클릭 없이 시작하면 슬라이드가 나올 때 시작한다
                    std::vector<Step> steps;
                };
                std::vector<Click> clicks;
                std::string builds;
                std::map<int, int> group_ids; // spid -> 다음 grpId
                for (const auto& animation : slide.animations) {
                    const std::string animation_where = where + ", animate " + animation.target;
                    const auto target = named_.find(animation.target);
                    if (target == named_.end()) {
                        continue;
                    }
                    const int spid = ids_.at(target->second);
                    // 직접 그린 이동 경로는 오른쪽으로 가는 경로 효과에서 경로만 바꾼다
                    const std::string key = !animation.path.empty() ? "move.right"
                        : animation.category + "." + animation.effect + (animation.option.empty() ? "" : "." + animation.option);
                    AnimationPreset media_preset{1, false, false, ""};
                    std::string media_effect;
                    if (animation.category == "media") {
                        // 비디오, 오디오의 재생, 일시 중지, 중지 명령. 재생은 잘라 낸 뒤의 길이만큼 걸린다
                        const auto media = std::find_if(media_elements_.begin(), media_elements_.end(), [&](const Media& each) { return each.spid == spid; });
                        static const std::map<std::string, std::pair<std::string, std::string>> commands = {
                            {"play", {"1", "playFrom(0.0)"}}, {"pause", {"2", "pause"}}, {"stop", {"3", "stop"}},
                        };
                        const auto& [preset_id, command] = commands.at(animation.effect);
                        const long long length = animation.effect == "play" && media != media_elements_.end() ? media->duration : 0;
                        media_preset.duration = length;
                        media_effect = "<p:par><p:cTn id=\"#\" presetID=\"" + preset_id + "\" presetClass=\"mediacall\" presetSubtype=\"0\" fill=\"hold\" nodeType=\"clickEffect\">"
                            "<p:stCondLst><p:cond delay=\"0\"/></p:stCondLst><p:childTnLst><p:cmd type=\"call\" cmd=\"" + command + "\"><p:cBhvr>"
                            "<p:cTn id=\"#\"" + attribute("dur", std::max(1LL, length)) + " fill=\"hold\"/><p:tgtEl><p:spTgt spid=\"@\"/></p:tgtEl></p:cBhvr></p:cmd></p:childTnLst></p:cTn></p:par>";
                        media_preset.xml = media_effect.c_str();
                    }
                    const auto found = animation.category == "media" ? animation_presets().end() : animation_presets().find(key);
                    if (animation.category != "media" && found == animation_presets().end()) {
                        error(animation_where + ": the pptx backend does not support " + key);
                        continue;
                    }
                    const AnimationPreset& preset = animation.category == "media" ? media_preset : found->second;
                    long long duration = preset.duration;
                    double factor = 1;
                    if (animation.duration) {
                        const Emu milliseconds = to_milliseconds(*animation.duration);
                        factor = duration > 0 ? static_cast<double>(milliseconds) / static_cast<double>(duration) : 1;
                        duration = milliseconds;
                    }
                    const long long delay = animation.delay ? to_milliseconds(*animation.delay) : 0;
                    const std::string path = animation.path.empty() ? "" : motion_path(animation.path);
                    std::string node = "withEffect";
                    if (animation.start == "on_click" || clicks.empty()) {
                        clicks.push_back({animation.start != "on_click", {Step{}}});
                        node = animation.start == "on_click" ? "clickEffect" : animation.start == "after_previous" ? "afterEffect" : "withEffect";
                    } else if (animation.start == "after_previous") {
                        const long long start = clicks.back().steps.back().end;
                        clicks.back().steps.push_back({start, start, ""});
                        node = "afterEffect";
                    }
                    Step& step = clicks.back().steps.back();
                    const std::string& object = target->second->object;
                    const bool has_text = object == "text_box" || object == "shape" || object == "placeholder";
                    int group_id = 0;
                    if (preset.builds && has_text) {
                        group_id = group_ids[spid]++;
                        builds += "<p:bldP" + attribute("spid", spid) + attribute("grpId", group_id) + (preset.animate_background ? " animBg=\"1\"" : "") + "/>";
                    }
                    step.effects += instantiate_effect(preset.xml, node, spid, group_id, delay, factor, path);
                    step.end = std::max(step.end, step.start + delay + std::max(duration, 0LL));
                }
                // 비디오, 오디오마다 재생 상태(cMediaNode)와, when_clicked면 그 개체를 누를 때 재생하고 멈추는 대화형 순서
                std::string media_nodes;
                std::string triggers;
                for (const auto& media : media_elements_) {
                    const ir::Element& element = *media.element;
                    const bool video = element.object == "video";
                    const auto flag = [&](const char* name) {
                        const auto* value = std::get_if<bool>(find_property(element, name));
                        return value != nullptr && *value;
                    };
                    const double volume = optional_scalar(element, "volume").value_or(100);
                    const bool across = !video && flag("across_slides");
                    const bool hidden = flag(video ? "hide_when_stopped" : "hide_icon");
                    // 끝나면 처음으로 되돌리는 것(rewind)은 재생 상태를 유지하지 않는 것(fill="remove")이다
                    media_nodes += std::string(video ? "<p:video" : "<p:audio") + (video && flag("fullscreen") ? " fullScrn=\"1\"" : "") + ">"
                        "<p:cMediaNode" + attribute("vol", std::llround(volume * 1000)) + (across ? " numSld=\"999\"" : "") + (hidden ? " showWhenStopped=\"0\"" : "") + ">"
                        "<p:cTn id=\"#\"" + (flag("loop") ? " repeatCount=\"indefinite\"" : "") + (flag("rewind") ? " fill=\"remove\"" : " fill=\"hold\"") + " display=\"0\">"
                        "<p:stCondLst><p:cond delay=\"indefinite\"/></p:stCondLst>"
                        + (across ? "" : "<p:endCondLst><p:cond evt=\"onStopAudio\" delay=\"0\"><p:tgtEl><p:sldTgt/></p:tgtEl></p:cond></p:endCondLst>")
                        + "</p:cTn><p:tgtEl><p:spTgt" + attribute("spid", media.spid) + "/></p:tgtEl></p:cMediaNode>" + (video ? "</p:video>" : "</p:audio>");
                    if (enum_member(element, "start") != "when_clicked") {
                        continue;
                    }
                    const std::string spid = attribute("spid", media.spid);
                    triggers += "<p:seq concurrent=\"1\" nextAc=\"seek\"><p:cTn id=\"#\" restart=\"whenNotActive\" fill=\"hold\" evtFilter=\"cancelBubble\" nodeType=\"interactiveSeq\">"
                        "<p:stCondLst><p:cond evt=\"onClick\" delay=\"0\"><p:tgtEl><p:spTgt" + spid + "/></p:tgtEl></p:cond></p:stCondLst>"
                        "<p:endSync evt=\"end\" delay=\"0\"><p:rtn val=\"all\"/></p:endSync><p:childTnLst>"
                        "<p:par><p:cTn id=\"#\" fill=\"hold\"><p:stCondLst><p:cond delay=\"0\"/></p:stCondLst><p:childTnLst>"
                        "<p:par><p:cTn id=\"#\" fill=\"hold\"><p:stCondLst><p:cond delay=\"0\"/></p:stCondLst><p:childTnLst>"
                        "<p:par><p:cTn id=\"#\" presetID=\"2\" presetClass=\"mediacall\" presetSubtype=\"0\" fill=\"hold\" nodeType=\"clickEffect\">"
                        "<p:stCondLst><p:cond delay=\"0\"/></p:stCondLst><p:childTnLst><p:cmd type=\"call\" cmd=\"togglePause\"><p:cBhvr>"
                        "<p:cTn id=\"#\" dur=\"1\" fill=\"hold\"/><p:tgtEl><p:spTgt" + spid + "/></p:tgtEl></p:cBhvr></p:cmd></p:childTnLst></p:cTn></p:par>"
                        "</p:childTnLst></p:cTn></p:par></p:childTnLst></p:cTn></p:par></p:childTnLst></p:cTn>"
                        "<p:nextCondLst><p:cond evt=\"onNext\" delay=\"0\"><p:tgtEl><p:spTgt" + spid + "/></p:tgtEl></p:cond></p:nextCondLst></p:seq>";
                }
                if (clicks.empty() && media_nodes.empty()) {
                    return "";
                }
                std::string sequence;
                for (const auto& click : clicks) {
                    std::string steps;
                    for (const auto& step : click.steps) {
                        steps += "<p:par><p:cTn id=\"#\" fill=\"hold\"><p:stCondLst><p:cond" + attribute("delay", step.start) + "/></p:stCondLst>"
                            "<p:childTnLst>" + step.effects + "</p:childTnLst></p:cTn></p:par>";
                    }
                    const std::string condition = click.automatic ? "<p:cond delay=\"indefinite\"/><p:cond evt=\"onBegin\" delay=\"0\"><p:tn val=\"2\"/></p:cond>" : "<p:cond delay=\"indefinite\"/>";
                    sequence += "<p:par><p:cTn id=\"#\" fill=\"hold\"><p:stCondLst>" + condition + "</p:stCondLst><p:childTnLst>" + steps + "</p:childTnLst></p:cTn></p:par>";
                }
                const std::string main_sequence = clicks.empty() ? ""
                    : "<p:seq concurrent=\"1\" nextAc=\"seek\"><p:cTn id=\"#\" dur=\"indefinite\" nodeType=\"mainSeq\"><p:childTnLst>" + sequence + "</p:childTnLst></p:cTn>"
                    "<p:prevCondLst><p:cond evt=\"onPrev\" delay=\"0\"><p:tgtEl><p:sldTgt/></p:tgtEl></p:cond></p:prevCondLst>"
                    "<p:nextCondLst><p:cond evt=\"onNext\" delay=\"0\"><p:tgtEl><p:sldTgt/></p:tgtEl></p:cond></p:nextCondLst></p:seq>";
                std::string xml = "<p:timing><p:tnLst><p:par><p:cTn id=\"#\" dur=\"indefinite\" restart=\"never\" nodeType=\"tmRoot\"><p:childTnLst>"
                    + main_sequence + triggers + media_nodes + "</p:childTnLst></p:cTn></p:par></p:tnLst>" + (builds.empty() ? "" : "<p:bldLst>" + builds + "</p:bldLst>") + "</p:timing>";
                // cTn id는 문서 순서대로 1부터
                const std::string placeholder = "<p:cTn id=\"#\"";
                int number = 0;
                for (std::size_t at = xml.find(placeholder); at != std::string::npos; at = xml.find(placeholder, at)) {
                    const std::string replacement = "<p:cTn id=\"" + std::to_string(++number) + "\"";
                    xml.replace(at, placeholder.size(), replacement);
                    at += replacement.size();
                }
                return xml;
            }

            static void replace_first(std::string& text, const std::string& from, const std::string& to) {
                if (const std::size_t at = text.find(from); at != std::string::npos) {
                    text.replace(at, from.size(), to);
                }
            }

            // 효과의 시작 방식, 대상, 지연을 채우고 안의 시간을 길이에 맞춰 늘린다
            static std::string instantiate_effect(std::string xml, const std::string& node, int spid, int group_id, long long delay, double factor, const std::string& path) {
                replace_first(xml, "nodeType=\"clickEffect\"", "nodeType=\"" + node + "\"");
                replace_first(xml, "grpId=\"0\"", "grpId=\"" + std::to_string(group_id) + "\"");
                const std::string target = "spid=\"@\"";
                for (std::size_t at = xml.find(target); at != std::string::npos; at = xml.find(target, at)) {
                    xml.replace(at, target.size(), "spid=\"" + std::to_string(spid) + "\"");
                    ++at;
                }
                // 효과 자신의 시작 조건이 첫 cond다
                const std::string start = "<p:cond delay=\"0\"/>";
                const std::size_t at = xml.find(start);
                std::string head = xml.substr(0, at) + "<p:cond delay=\"" + std::to_string(delay) + "\"/>";
                std::string tail = xml.substr(at + start.size());
                if (factor != 1) {
                    tail = scale_times(tail, factor);
                }
                xml = head + tail;
                if (!path.empty()) {
                    const std::string attribute_start = " path=\"";
                    const std::size_t begin = xml.find(attribute_start);
                    const std::size_t end = xml.find('"', begin + attribute_start.size());
                    xml.replace(begin + attribute_start.size(), end - begin - attribute_start.size(), path);
                    replace_first(xml, " ptsTypes=\"\"", "");
                }
                return xml;
            }

            // 숫자로 된 dur, delay를 factor배 한다. indefinite는 그대로 둔다
            static std::string scale_times(const std::string& xml, double factor) {
                std::string result;
                std::size_t position = 0;
                while (position < xml.size()) {
                    std::size_t found = std::string::npos;
                    std::size_t marker = 0;
                    for (const std::string name : {" dur=\"", " delay=\""}) {
                        const std::size_t at = xml.find(name, position);
                        if (at < found) {
                            found = at;
                            marker = name.size();
                        }
                    }
                    if (found == std::string::npos) {
                        result += xml.substr(position);
                        break;
                    }
                    const std::size_t digits = found + marker;
                    std::size_t end = digits;
                    while (end < xml.size() && std::isdigit(static_cast<unsigned char>(xml[end]))) {
                        ++end;
                    }
                    result += xml.substr(position, digits - position);
                    if (end > digits && end < xml.size() && xml[end] == '"') {
                        result += std::to_string(std::llround(static_cast<double>(std::stoll(xml.substr(digits, end - digits))) * factor));
                    } else {
                        result += xml.substr(digits, end - digits);
                    }
                    position = end;
                }
                return result;
            }

            // px 좌표의 SVG path를 슬라이드 크기에 대한 비율로. PowerPoint의 이동 경로는 M, L, C, Z와 끝의 E를 쓴다
            std::string motion_path(const std::string& text) const {
                std::string message;
                const auto path = parse_svg_path(text, message);
                if (!path) {
                    return "";
                }
                const double width = static_cast<double>(slide_width_) / emu_per_px;
                const double height = static_cast<double>(slide_height_) / emu_per_px;
                std::string result;
                for (const auto& segment : path->segments) {
                    result += (result.empty() ? "" : " ") + std::string(1, segment.command);
                    for (const auto& point : segment.points) {
                        result += " " + format_decimal(point.x / width) + " " + format_decimal(point.y / height);
                    }
                }
                return result + " E";
            }

            // ---- 파트

            // master_number번째 slide master에 속한 layout을 만들고 그 번호를 돌려준다
            std::size_t add_layout(const std::string& name, const std::vector<ir::Element>* elements, const ir::Value* background, std::size_t master_number) {
                const std::size_t number = ++layout_count_;
                MasterPart& master = master_parts_.at(master_number - 1);
                master.layouts.push_back(number);
                std::vector<Relationship> relationships{{relationship_type + "slideMaster", "../slideMasters/slideMaster" + std::to_string(master_number) + ".xml"}};
                begin_tree(relationships, true, background, elements);
                const std::string where = "layout " + master.name + "." + name;
                const std::string background_part = background != nullptr ? background_xml(*background, where) : "";
                const std::string shapes = elements != nullptr ? shapes_xml(*elements, where) : "";
                const std::string file = "slideLayout" + std::to_string(number) + ".xml";
                add_part("ppt/slideLayouts/" + file,
                         xml_declaration + "<p:sldLayout" + namespaces + (elements == nullptr ? " type=\"blank\"" : "") + " preserve=\"1\">"
                         "<p:cSld" + attribute("name", name) + ">" + background_part + "<p:spTree>" + group_properties + shapes + "</p:spTree></p:cSld>"
                         "<p:clrMapOvr><a:masterClrMapping/></p:clrMapOvr></p:sldLayout>",
                         content_type + "presentationml.slideLayout+xml");
                add_part("ppt/slideLayouts/_rels/" + file + ".rels", relationships_xml(relationships));
                relationships_ = nullptr;
                return number;
            }

            // slide master마다 theme를 하나씩 둔다
            void add_master(std::size_t number) {
                MasterPart& master = master_parts_.at(number - 1);
                master.id = next_master_id_++;
                std::vector<Relationship> relationships;
                std::string layout_ids;
                for (const std::size_t layout : master.layouts) {
                    relationships.push_back({relationship_type + "slideLayout", "../slideLayouts/slideLayout" + std::to_string(layout) + ".xml"});
                    layout_ids += "<p:sldLayoutId" + attribute("id", std::to_string(next_master_id_++)) + attribute("r:id", "rId" + std::to_string(relationships.size())) + "/>";
                }
                const std::string theme = "theme" + std::to_string(number) + ".xml";
                relationships.push_back({relationship_type + "theme", "../theme/" + theme});
                add_part("ppt/theme/" + theme, theme_xml(master.theme), content_type + "theme+xml");
                const std::string file = "slideMaster" + std::to_string(number) + ".xml";
                add_part("ppt/slideMasters/" + file,
                         xml_declaration + "<p:sldMaster" + namespaces + ">"
                         "<p:cSld" + attribute("name", master.name) + "><p:bg><p:bgRef idx=\"1001\"><a:schemeClr val=\"bg1\"/></p:bgRef></p:bg>"
                         "<p:spTree>" + group_properties + "</p:spTree></p:cSld>"
                         + color_map_xml() +
                         "<p:sldLayoutIdLst>" + layout_ids + "</p:sldLayoutIdLst>"
                         "<p:txStyles><p:titleStyle>" + level_styles_xml(4400, "+mj") + "</p:titleStyle>"
                         "<p:bodyStyle>" + level_styles_xml(2800, "+mn") + "</p:bodyStyle>"
                         "<p:otherStyle>" + level_styles_xml(1800, "+mn") + "</p:otherStyle></p:txStyles>"
                         "</p:sldMaster>",
                         content_type + "presentationml.slideMaster+xml");
                add_part("ppt/slideMasters/_rels/" + file + ".rels", relationships_xml(relationships));
            }

            static std::string color_map_xml() {
                return "<p:clrMap bg1=\"lt1\" tx1=\"dk1\" bg2=\"lt2\" tx2=\"dk2\" accent1=\"accent1\" accent2=\"accent2\" accent3=\"accent3\""
                       " accent4=\"accent4\" accent5=\"accent5\" accent6=\"accent6\" hlink=\"hlink\" folHlink=\"folHlink\"/>";
            }

            // 발표자 메모 페이지의 틀. 슬라이드 그림과 메모 본문 자리를 가진다
            void add_notes_master() {
                add_part("ppt/notesMasters/notesMaster1.xml",
                         xml_declaration + "<p:notesMaster" + namespaces + ">"
                         "<p:cSld><p:bg><p:bgRef idx=\"1001\"><a:schemeClr val=\"bg1\"/></p:bgRef></p:bg><p:spTree>" + group_properties +
                         "<p:sp><p:nvSpPr><p:cNvPr id=\"2\" name=\"Slide Image Placeholder 1\"/><p:cNvSpPr><a:spLocks noGrp=\"1\" noRot=\"1\" noChangeAspect=\"1\"/></p:cNvSpPr>"
                         "<p:nvPr><p:ph type=\"sldImg\" idx=\"2\"/></p:nvPr></p:nvSpPr>"
                         "<p:spPr><a:xfrm><a:off x=\"685800\" y=\"1143000\"/><a:ext cx=\"5486400\" cy=\"3086100\"/></a:xfrm><a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom>"
                         "<a:noFill/><a:ln w=\"12700\"><a:solidFill><a:prstClr val=\"black\"/></a:solidFill></a:ln></p:spPr></p:sp>"
                         "<p:sp><p:nvSpPr><p:cNvPr id=\"3\" name=\"Notes Placeholder 2\"/><p:cNvSpPr><a:spLocks noGrp=\"1\"/></p:cNvSpPr>"
                         "<p:nvPr><p:ph type=\"body\" sz=\"quarter\" idx=\"3\"/></p:nvPr></p:nvSpPr>"
                         "<p:spPr><a:xfrm><a:off x=\"685800\" y=\"4400550\"/><a:ext cx=\"5486400\" cy=\"3600450\"/></a:xfrm><a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom></p:spPr>"
                         "<p:txBody><a:bodyPr vert=\"horz\" lIns=\"91440\" tIns=\"45720\" rIns=\"91440\" bIns=\"45720\" rtlCol=\"0\"/><a:lstStyle/><a:p><a:endParaRPr/></a:p></p:txBody></p:sp>"
                         "</p:spTree></p:cSld>" + color_map_xml() +
                         "<p:notesStyle>" + level_styles_xml(1200, "+mn") + "</p:notesStyle></p:notesMaster>",
                         content_type + "presentationml.notesMaster+xml");
                // theme 번호는 slide master들 다음
                const std::string theme = "theme" + std::to_string(master_parts_.size() + 1) + ".xml";
                add_part("ppt/notesMasters/_rels/notesMaster1.xml.rels", relationships_xml({{relationship_type + "theme", "../theme/" + theme}}));
                add_part("ppt/theme/" + theme, theme_xml(nullptr), content_type + "theme+xml");
            }

            void add_notes_slide(std::size_t number, const ir::Slide& slide) {
                const std::string where = "slide " + std::to_string(slide.page) + " notes";
                std::vector<Relationship> relationships{
                    {relationship_type + "notesMaster", "../notesMasters/notesMaster1.xml"},
                    {relationship_type + "slide", "../slides/slide" + std::to_string(number) + ".xml"},
                };
                // 메모 안의 링크는 메모 파트의 relationships에 넣는다
                std::vector<Relationship>* slide_relationships = relationships_;
                relationships_ = &relationships;
                std::string paragraphs;
                for (const auto& paragraph : slide.notes.paragraphs) {
                    paragraphs += paragraph_xml(paragraph, "");
                }
                relationships_ = slide_relationships;
                const std::string file = "notesSlide" + std::to_string(number) + ".xml";
                add_part("ppt/notesSlides/" + file,
                         xml_declaration + "<p:notes" + namespaces + "><p:cSld><p:spTree>" + group_properties +
                         "<p:sp><p:nvSpPr><p:cNvPr id=\"2\" name=\"Slide Image Placeholder 1\"/><p:cNvSpPr><a:spLocks noGrp=\"1\" noRot=\"1\" noChangeAspect=\"1\"/></p:cNvSpPr>"
                         "<p:nvPr><p:ph type=\"sldImg\"/></p:nvPr></p:nvSpPr><p:spPr/></p:sp>"
                         "<p:sp><p:nvSpPr><p:cNvPr id=\"3\" name=\"Notes Placeholder 2\"/><p:cNvSpPr><a:spLocks noGrp=\"1\"/></p:cNvSpPr>"
                         "<p:nvPr><p:ph type=\"body\" idx=\"1\"/></p:nvPr></p:nvSpPr><p:spPr/>"
                         "<p:txBody><a:bodyPr/><a:lstStyle/>" + paragraphs + "</p:txBody></p:sp>"
                         "</p:spTree></p:cSld><p:clrMapOvr><a:masterClrMapping/></p:clrMapOvr></p:notes>",
                         content_type + "presentationml.notesSlide+xml");
                add_part("ppt/notesSlides/_rels/" + file + ".rels", relationships_xml(relationships));
            }

            void add_slide(std::size_t number, const ir::Slide& slide, std::size_t layout_number) {
                const std::string where = "slide " + std::to_string(slide.page);
                std::vector<Relationship> relationships{{relationship_type + "slideLayout", "../slideLayouts/slideLayout" + std::to_string(layout_number) + ".xml"}};
                // slide에 배경이 없으면 layout의 배경이 보인다
                const ir::Value* background = slide.background ? &*slide.background : nullptr;
                if (background == nullptr && slide.layout) {
                    const ir::Layout& layout = document_.masters.at(slide.layout->master).layouts.at(slide.layout->layout);
                    background = layout.background ? &*layout.background : nullptr;
                }
                const int next_id = begin_tree(relationships, false, background, &slide.elements);
                const std::string background_part = slide.background ? background_xml(*slide.background, where) : "";
                const std::string shapes = shapes_xml(slide.elements, where) + slide_placeholders_xml(slide, next_id);
                if (!slide.notes.paragraphs.empty()) {
                    relationships.push_back({relationship_type + "notesSlide", "../notesSlides/notesSlide" + std::to_string(number) + ".xml"});
                    add_notes_slide(number, slide);
                }
                const std::string transition = transition_xml(slide, where);
                const std::string timing = timing_xml(slide, where);
                // 검토 메모는 slide의 creationId로 slide를 가리킨다
                std::string creation;
                std::string comments;
                if (!slide.reviews.empty()) {
                    const std::string creation_id = std::to_string(1000000 + number);
                    creation = "<p:extLst><p:ext uri=\"{BB962C8B-B14F-4D97-AF65-F5344CB8AC3E}\"><p14:creationId" + attribute("xmlns:p14", extension_namespaces.at("p14"))
                        + attribute("val", creation_id) + "/></p:ext></p:extLst>";
                    const std::string file = "modernComment_" + std::to_string(number) + ".xml";
                    add_comments(file, slide, number, creation_id);
                    comments = "<p:extLst><p:ext uri=\"{6950BFC3-D8DA-4A85-94F7-54DA5524770B}\"><p188:commentRel" + attribute("xmlns:p188", comments_namespace)
                        + attribute("r:id", relate("http://schemas.microsoft.com/office/2018/10/relationships/comments", "../comments/" + file)) + "/></p:ext></p:extLst>";
                }
                const std::string file = "slide" + std::to_string(number) + ".xml";
                add_part("ppt/slides/" + file,
                         xml_declaration + "<p:sld" + namespaces + (slide.hidden ? " show=\"0\"" : "") + "><p:cSld>" + background_part
                         + "<p:spTree>" + group_properties + shapes + "</p:spTree>" + creation + "</p:cSld>"
                         "<p:clrMapOvr><a:masterClrMapping/></p:clrMapOvr>" + transition + timing + comments + "</p:sld>",
                         content_type + "presentationml.slide+xml");
                add_part("ppt/slides/_rels/" + file + ".rels", relationships_xml(relationships));
                relationships_ = nullptr;
            }

            // 이름의 첫 글자 (UTF-8)
            static std::string first_letter(const std::string& name) {
                if (name.empty()) {
                    return "";
                }
                const auto lead = static_cast<unsigned char>(name[0]);
                const std::size_t length = lead < 0x80 ? 1 : lead < 0xE0 ? 2 : lead < 0xF0 ? 3 : 4;
                return name.substr(0, length);
            }

            void add_comments(const std::string& file, const ir::Slide& slide, std::size_t number, const std::string& creation_id) {
                std::string xml = xml_declaration + "<p188:cmLst" + attribute("xmlns:a", "http://schemas.openxmlformats.org/drawingml/2006/main")
                    + attribute("xmlns:r", "http://schemas.openxmlformats.org/officeDocument/2006/relationships") + attribute("xmlns:p188", comments_namespace) + ">";
                const std::string created = timestamp();
                for (const auto& review : slide.reviews) {
                    auto author = authors_.find(review.author);
                    if (author == authors_.end()) {
                        author = authors_.emplace(review.author, guid(++guid_count_)).first;
                    }
                    const Emu x = to_emu(review.x, slide_width_);
                    const Emu y = to_emu(review.y, slide_height_);
                    std::string paragraphs;
                    std::size_t start = 0;
                    while (true) {
                        const std::size_t end = review.text.find('\n', start);
                        const std::string line = review.text.substr(start, end == std::string::npos ? std::string::npos : end - start);
                        paragraphs += line.empty() ? "<a:p><a:endParaRPr/></a:p>" : "<a:p><a:r><a:t>" + escape(line) + "</a:t></a:r></a:p>";
                        if (end == std::string::npos) {
                            break;
                        }
                        start = end + 1;
                    }
                    xml += "<p188:cm" + attribute("id", guid(++guid_count_)) + attribute("authorId", author->second) + attribute("created", created) + ">"
                        "<pc:sldMkLst xmlns:pc=\"http://schemas.microsoft.com/office/powerpoint/2013/main/command\"><pc:docMk/>"
                        "<pc:sldMk" + attribute("cId", creation_id) + attribute("sldId", std::to_string(255 + number)) + "/></pc:sldMkLst>"
                        "<p188:pos" + attribute("x", x) + attribute("y", y) + "/><p188:txBody><a:bodyPr/><a:lstStyle/>" + paragraphs + "</p188:txBody></p188:cm>";
                }
                add_part("ppt/comments/" + file, xml + "</p188:cmLst>", "application/vnd.ms-powerpoint.comments+xml");
            }

            void add_presentation() {
                const std::size_t slide_count = document_.slides.size();
                std::vector<Relationship> relationships;
                std::string master_ids;
                for (std::size_t i = 0; i < master_parts_.size(); ++i) {
                    relationships.push_back({relationship_type + "slideMaster", "slideMasters/slideMaster" + std::to_string(i + 1) + ".xml"});
                    master_ids += "<p:sldMasterId" + attribute("id", std::to_string(master_parts_[i].id)) + attribute("r:id", "rId" + std::to_string(relationships.size())) + "/>";
                }
                std::string slide_ids;
                for (std::size_t i = 0; i < slide_count; ++i) {
                    relationships.push_back({relationship_type + "slide", "slides/slide" + std::to_string(i + 1) + ".xml"});
                    slide_ids += "<p:sldId" + attribute("id", std::to_string(256 + i)) + attribute("r:id", "rId" + std::to_string(relationships.size())) + "/>";
                }
                std::string notes_master_ids;
                if (has_notes_) {
                    relationships.push_back({relationship_type + "notesMaster", "notesMasters/notesMaster1.xml"});
                    notes_master_ids = "<p:notesMasterIdLst><p:notesMasterId" + attribute("r:id", "rId" + std::to_string(relationships.size())) + "/></p:notesMasterIdLst>";
                }
                relationships.push_back({relationship_type + "presProps", "presProps.xml"});
                relationships.push_back({relationship_type + "viewProps", "viewProps.xml"});
                relationships.push_back({relationship_type + "theme", "theme/theme1.xml"});
                relationships.push_back({relationship_type + "tableStyles", "tableStyles.xml"});
                if (!authors_.empty()) {
                    relationships.push_back({"http://schemas.microsoft.com/office/2018/10/relationships/authors", "authors.xml"});
                    std::string authors;
                    for (const auto& [name, id] : authors_) {
                        authors += "<p188:author" + attribute("id", id) + attribute("name", name) + attribute("initials", first_letter(name))
                            + attribute("userId", name) + " providerId=\"None\"/>";
                    }
                    add_part("ppt/authors.xml",
                             xml_declaration + "<p188:authorLst" + attribute("xmlns:a", "http://schemas.openxmlformats.org/drawingml/2006/main")
                             + attribute("xmlns:r", "http://schemas.openxmlformats.org/officeDocument/2006/relationships") + attribute("xmlns:p188", comments_namespace) + ">"
                             + authors + "</p188:authorLst>",
                             "application/vnd.ms-powerpoint.authors+xml");
                }

                add_part("ppt/presentation.xml",
                         xml_declaration + "<p:presentation" + namespaces + " saveSubsetFonts=\"1\">"
                         "<p:sldMasterIdLst>" + master_ids + "</p:sldMasterIdLst>"
                         + notes_master_ids
                         + (slide_ids.empty() ? "" : "<p:sldIdLst>" + slide_ids + "</p:sldIdLst>") +
                         "<p:sldSz" + attribute("cx", slide_width_) + attribute("cy", slide_height_) + "/>"
                         "<p:notesSz cx=\"6858000\" cy=\"9144000\"/>"
                         "<p:defaultTextStyle>" + level_styles_xml(1800, "+mn") + "</p:defaultTextStyle>"
                         + sections_xml() +
                         "</p:presentation>",
                         content_type + "presentationml.presentation.main+xml");
                add_part("ppt/_rels/presentation.xml.rels", relationships_xml(relationships));
            }

            // 이어지는 slide들을 구역 하나로 묶는다. 구역이 하나라도 있으면 첫 section 앞의 slide들은 기본 구역에 들어간다
            std::string sections_xml() {
                const auto& slides = document_.slides;
                if (std::none_of(slides.begin(), slides.end(), [](const ir::Slide& slide) { return slide.section.has_value(); })) {
                    return "";
                }
                std::string sections;
                std::string current;
                std::string ids;
                for (std::size_t i = 0; i < slides.size(); ++i) {
                    const std::string name = slides[i].section.value_or("Default Section");
                    if (i > 0 && name != current) {
                        sections += "<p14:section" + attribute("name", current) + attribute("id", guid(++guid_count_)) + "><p14:sldIdLst>" + ids + "</p14:sldIdLst></p14:section>";
                        ids.clear();
                    }
                    current = name;
                    ids += "<p14:sldId" + attribute("id", std::to_string(256 + i)) + "/>";
                }
                sections += "<p14:section" + attribute("name", current) + attribute("id", guid(++guid_count_)) + "><p14:sldIdLst>" + ids + "</p14:sldIdLst></p14:section>";
                return "<p:extLst><p:ext uri=\"{521415D9-36F7-43E2-AB2F-B90AF26B5E84}\"><p14:sectionLst" + attribute("xmlns:p14", extension_namespaces.at("p14")) + ">"
                    + sections + "</p14:sectionLst></p:ext></p:extLst>";
            }

            void add_common_parts() {
                const std::string show = target_.loop ? "<p:showPr loop=\"1\" showNarration=\"1\"><p:present/><p:sldAll/><p:penClr><a:prstClr val=\"red\"/></p:penClr></p:showPr>" : "";
                add_part("ppt/presProps.xml", xml_declaration + "<p:presentationPr" + namespaces + (show.empty() ? "/>" : ">" + show + "</p:presentationPr>"),
                         content_type + "presentationml.presProps+xml");
                add_part("ppt/viewProps.xml", xml_declaration + "<p:viewPr" + namespaces + "/>", content_type + "presentationml.viewProps+xml");
                add_part("ppt/tableStyles.xml",
                         xml_declaration + "<a:tblStyleLst xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" def=\"{5C22544A-7EE6-4342-B048-85BDC9FD1C3A}\"/>",
                         content_type + "presentationml.tableStyles+xml");
                add_part("docProps/core.xml",
                         xml_declaration + "<cp:coreProperties xmlns:cp=\"http://schemas.openxmlformats.org/package/2006/metadata/core-properties\""
                         " xmlns:dc=\"http://purl.org/dc/elements/1.1/\" xmlns:dcterms=\"http://purl.org/dc/terms/\""
                         " xmlns:dcmitype=\"http://purl.org/dc/dcmitype/\" xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\">"
                         "<dc:title>" + escape(target_.title.empty() ? target_.name : target_.title) + "</dc:title>"
                         "<dc:creator>" + escape(target_.author.empty() ? "templide" : target_.author) + "</dc:creator></cp:coreProperties>",
                         "application/vnd.openxmlformats-package.core-properties+xml");
                add_part("docProps/app.xml",
                         xml_declaration + "<Properties xmlns=\"http://schemas.openxmlformats.org/officeDocument/2006/extended-properties\""
                         " xmlns:vt=\"http://schemas.openxmlformats.org/officeDocument/2006/docPropsVTypes\">"
                         "<Application>templide</Application><Slides>" + std::to_string(document_.slides.size()) + "</Slides></Properties>",
                         content_type + "extended-properties+xml");
                add_part("_rels/.rels", relationships_xml({
                    {relationship_type + "officeDocument", "ppt/presentation.xml"},
                    {"http://schemas.openxmlformats.org/package/2006/relationships/metadata/core-properties", "docProps/core.xml"},
                    {relationship_type + "extended-properties", "docProps/app.xml"},
                }));
            }

            // theme 선언이 있으면 그 색과 글꼴을 쓰고, 없는 값은 기본 테마의 값이다
            static std::string theme_xml(const ir::Theme* theme) {
                const auto color = [&](const std::string& name, const std::string& fallback) {
                    if (theme != nullptr) {
                        if (const auto it = theme->colors.find(name); it != theme->colors.end()) {
                            char hex[8];
                            std::snprintf(hex, sizeof hex, "%02X%02X%02X", it->second.r, it->second.g, it->second.b);
                            return "<a:" + name + "><a:srgbClr val=\"" + hex + "\"/></a:" + name + ">";
                        }
                    }
                    return "<a:" + name + ">" + fallback + "</a:" + name + ">";
                };
                const std::string heading = theme != nullptr ? theme->heading_font : "";
                const std::string body = theme != nullptr ? theme->body_font : "";
                const std::string phClr = "<a:solidFill><a:schemeClr val=\"phClr\"/></a:solidFill>";
                const auto line = [&](const char* width) { return std::string("<a:ln w=\"") + width + "\">" + phClr + "</a:ln>"; };
                return xml_declaration + "<a:theme xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\"" + attribute("name", theme != nullptr ? theme->name : "templide") + "><a:themeElements>"
                    "<a:clrScheme name=\"templide\">"
                    + color("dk1", "<a:sysClr val=\"windowText\" lastClr=\"000000\"/>") + color("lt1", "<a:sysClr val=\"window\" lastClr=\"FFFFFF\"/>")
                    + color("dk2", "<a:srgbClr val=\"44546A\"/>") + color("lt2", "<a:srgbClr val=\"E7E6E6\"/>")
                    + color("accent1", "<a:srgbClr val=\"4472C4\"/>") + color("accent2", "<a:srgbClr val=\"ED7D31\"/>")
                    + color("accent3", "<a:srgbClr val=\"A5A5A5\"/>") + color("accent4", "<a:srgbClr val=\"FFC000\"/>")
                    + color("accent5", "<a:srgbClr val=\"5B9BD5\"/>") + color("accent6", "<a:srgbClr val=\"70AD47\"/>")
                    + color("hlink", "<a:srgbClr val=\"0563C1\"/>") + color("folHlink", "<a:srgbClr val=\"954F72\"/>") +
                    "</a:clrScheme>"
                    // 기본 한글 글꼴은 맑은 고딕. 글꼴을 정하면 한글에도 같은 글꼴을 쓴다
                    "<a:fontScheme name=\"templide\">"
                    "<a:majorFont><a:latin" + attribute("typeface", heading.empty() ? "Calibri Light" : heading) + "/><a:ea" + attribute("typeface", heading.empty() ? "Malgun Gothic" : heading) + "/><a:cs typeface=\"\"/></a:majorFont>"
                    "<a:minorFont><a:latin" + attribute("typeface", body.empty() ? "Calibri" : body) + "/><a:ea" + attribute("typeface", body.empty() ? "Malgun Gothic" : body) + "/><a:cs typeface=\"\"/></a:minorFont>"
                    "</a:fontScheme>"
                    "<a:fmtScheme name=\"templide\">"
                    "<a:fillStyleLst>" + phClr + phClr + phClr + "</a:fillStyleLst>"
                    "<a:lnStyleLst>" + line("6350") + line("12700") + line("19050") + "</a:lnStyleLst>"
                    "<a:effectStyleLst><a:effectStyle><a:effectLst/></a:effectStyle><a:effectStyle><a:effectLst/></a:effectStyle><a:effectStyle><a:effectLst/></a:effectStyle></a:effectStyleLst>"
                    "<a:bgFillStyleLst>" + phClr + phClr + phClr + "</a:bgFillStyleLst>"
                    "</a:fmtScheme></a:themeElements><a:objectDefaults/><a:extraClrSchemeLst/></a:theme>";
            }

            std::string content_types_xml() const {
                std::string xml = xml_declaration + "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
                    "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
                    "<Default Extension=\"xml\" ContentType=\"application/xml\"/>";
                for (const auto& [extension, type] : defaults_) {
                    xml += "<Default" + attribute("Extension", extension) + attribute("ContentType", type) + "/>";
                }
                for (const auto& [name, type] : overrides_) {
                    xml += "<Override" + attribute("PartName", "/" + name) + attribute("ContentType", type) + "/>";
                }
                return xml + "</Types>";
            }

            // [Content_Types].xml을 맨 앞에 두고 zip으로 묶어 저장한다
            void save() {
                mz_zip_archive zip{};
                if (!mz_zip_writer_init_heap(&zip, 0, 0)) {
                    error("cannot create a zip archive");
                    return;
                }
                const auto add = [&](const std::string& name, const std::string& content) {
                    return mz_zip_writer_add_mem(&zip, name.c_str(), content.data(), content.size(), MZ_DEFAULT_COMPRESSION) != 0;
                };
                bool ok = add("[Content_Types].xml", content_types_xml());
                for (const auto& [name, content] : parts_) {
                    ok = ok && add(name, content);
                }
                void* buffer = nullptr;
                std::size_t size = 0;
                ok = ok && mz_zip_writer_finalize_heap_archive(&zip, &buffer, &size);
                mz_zip_writer_end(&zip);
                if (!ok) {
                    mz_free(buffer);
                    error("cannot create a zip archive");
                    return;
                }

                const std::filesystem::path output = resolve(target_.path);
                std::error_code error_code;
                if (output.has_parent_path()) {
                    std::filesystem::create_directories(output.parent_path(), error_code);
                }
                std::ofstream file(output, std::ios::binary);
                file.write(static_cast<const char*>(buffer), static_cast<std::streamsize>(size));
                mz_free(buffer);
                if (!file) {
                    error("cannot write " + display(output));
                }
            }
        };
    }

    std::vector<std::string> write(const ir::Document& document, const ir::Target& target, const std::filesystem::path& base_dir) {
        return Writer(document, target, base_dir).run();
    }
}
