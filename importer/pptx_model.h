#pragma once

#include "xml.h"

#include <cstdint>
#include <deque>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

// pptx 불러오기의 안쪽. pptx_model.cpp가 파일을 읽어 Presentation을 만들고(tree도 만든다), pptx_convert.cpp가 .tlide 코드를 쓴다
namespace templide::importer {
    using Emu = long long;
    constexpr double emu_per_px = 9525;
    constexpr double emu_per_pt = 12700;

    // 파트 하나의 관계. type은 주소의 마지막 부분(slideLayout, image, hyperlink 등)이고 target은 풀어 쓴 파트 이름이나 바깥 주소다
    struct Relationship {
        std::string type;
        std::string target;
        bool external = false;
    };

    // XML 파트와 그 관계
    struct Part {
        std::string name; // ppt/slides/slide1.xml
        xml::Node root;
        std::map<std::string, Relationship> rels;

        const Relationship* rel(const std::string& id) const;
        const Relationship* first(const std::string& type) const;
    };

    // zip 파일. 열어 둔 채로 파트를 읽는다
    class Package {
    public:
        Package() = default;
        ~Package();
        Package(const Package&) = delete;
        Package& operator=(const Package&) = delete;

        bool open(const std::filesystem::path& file, std::string& error);
        bool exists(const std::string& name) const;
        std::optional<std::string> read(const std::string& name) const;
        // XML 파트와 _rels의 관계를 읽는다. 없거나 잘못됐으면 nullptr
        std::unique_ptr<Part> part(const std::string& name) const;

    private:
        struct State;
        State* state_ = nullptr;
    };

    // 파트 이름 기준으로 상대 주소를 푼다 (../media/image1.png -> ppt/media/image1.png)
    std::string resolve_target(const std::string& part, const std::string& target);

    // 색. scheme이 있으면 테마 색 그대로(templide의 theme.이름)이고, r, g, b는 그 테마의 값이다
    struct Color {
        int r = 0;
        int g = 0;
        int b = 0;
        double a = 1;
        std::string scheme; // dark1, light1, dark2, light2, accent1..6, hyperlink, followed_hyperlink
    };

    struct Theme {
        std::string name;
        std::map<std::string, Color> colors; // dk1, lt1, dk2, lt2, accent1..6, hlink, folHlink
        std::string major_latin;
        std::string major_ea;
        std::string minor_latin;
        std::string minor_ea;
        std::vector<const xml::Node*> fills;       // fillStyleLst
        std::vector<const xml::Node*> lines;       // lnStyleLst
        std::vector<const xml::Node*> effects;     // effectStyleLst의 effectStyle
        std::vector<const xml::Node*> backgrounds; // bgFillStyleLst
        std::unique_ptr<Part> part;
    };

    // spTree의 개체 하나
    struct Shape {
        const xml::Node* node = nullptr; // p:sp, p:pic, p:cxnSp, p:grpSp, p:graphicFrame 등 (AlternateContent면 고른 쪽)
        int id = 0;                      // cNvPr id
        std::string name;                // cNvPr name
        // text_box, shape, image, line, connector, freeform, group, placeholder, video, audio,
        // 바꿀 수 없는 것: table, chart, smartart, ole, ink, equation, model3d, zoom, unknown
        std::string object;
        std::string reason;  // 바꿀 수 없는 까닭. 비어 있으면 바꿀 수 있다
        std::string excerpt; // 글의 앞부분 (목록에 보인다)
        std::string ph_type; // 개체 틀이면 그 종류 (title, ctrTitle, subTitle, body, obj, dt, ftr, sldNum, pic ...). 아니면 비어 있다
        std::optional<long long> ph_idx;
        std::vector<Shape> children; // 그룹
        bool has_text = false;
    };

    // 애니메이션 하나 (재생 차례대로)
    struct Animation {
        int target = 0;           // 대상 개체의 id
        std::string category;     // enter, emphasis, exit, move, media
        std::string effect;       // fade, fly, path(직접 그린 경로), play ...
        std::string option;
        std::string path;         // move path의 SVG (px)
        std::string start = "on_click";
        std::optional<long long> duration; // ms. 기본 길이와 같으면 없다
        std::optional<long long> delay;    // ms
        std::string reason;       // 바꿀 수 없는 까닭
        std::vector<std::string> notes; // 비슷하게 바꾼 것 (문단별 → 개체 전체 등)
    };

    struct Review {
        std::string author;
        std::string text;
        Emu x = 0;
        Emu y = 0;
    };

    struct Master {
        std::unique_ptr<Part> part;
        std::string name;
        Theme theme;
        std::map<std::string, std::string> color_map; // bg1 -> lt1 등
        std::vector<Shape> shapes;
        std::vector<int> layouts; // Presentation::layouts의 index
    };

    struct Layout {
        std::unique_ptr<Part> part;
        int master = 0;
        std::string name;
        std::string type; // sldLayout의 type (title, obj, twoObj ...)
        bool show_master_shapes = true;
        std::vector<Shape> shapes;
    };

    struct Slide {
        std::unique_ptr<Part> part;
        int layout = -1;
        std::vector<Shape> shapes;
        std::vector<Animation> animations;
        std::map<int, std::string> media_start; // 비디오, 오디오 id -> click_sequence, auto, when_clicked
        bool hidden = false;
        std::string section;
        std::unique_ptr<Part> notes;
        std::vector<Review> reviews;
        std::string title; // 목록에 보일 제목
    };

    struct Presentation {
        std::filesystem::path file;
        Package package;
        std::unique_ptr<Part> presentation;
        Emu width = 12192000;
        Emu height = 6858000;
        std::vector<Master> masters;
        std::vector<Layout> layouts;
        std::vector<Slide> slides;
        bool hangul = false; // 글에 한글이 있다. 테마 글꼴을 고를 때 쓴다
    };

    // pptx를 읽는다. 실패하면 nullptr이고 error에 까닭이 있다
    std::unique_ptr<Presentation> load(const std::filesystem::path& file, std::string& error);

    // ---- 도우미

    // 개체가 개체 틀이면 그 nvPr의 p:ph
    const xml::Node* placeholder_of(const xml::Node& shape);
    // 개체의 spPr (그림, 연결선, 도형). 그룹이면 grpSpPr
    const xml::Node* shape_properties(const xml::Node& shape);
    // 개체의 글 상자
    const xml::Node* text_body(const xml::Node& shape);
    // 글의 문단들을 이어 붙인 것
    std::string plain_text(const xml::Node* body, std::size_t limit = 200);
    // AlternateContent 안에서 쓸 쪽
    const xml::Node* alternate(const xml::Node& node);

    // ---- 개체 틀
    //
    // templide의 개체 틀은 레이아웃마다 제목(title), 부제목(subtitle), 본문(body)이 하나씩이다. 레이아웃에서 역할마다 처음 나오는 것만 placeholder로 옮기고,
    // 슬라이드의 개체 틀은 그 짝이 옮겨지고 슬라이드에서 위치를 바꾸지 않았으면 title = ... 처럼 내용만 쓴다. 아니면 글상자로 옮긴다

    // 개체 틀 종류의 역할. title, subtitle, body이거나 빈 문자열
    std::string role_of(const std::string& type);
    // 레이아웃에서 placeholder로 옮기는 개체 틀: 역할 -> 개체 id
    std::map<std::string, int> layout_roles(const Layout& layout);
    // 슬라이드나 레이아웃 개체 틀의 짝 (레이아웃이나 마스터에서 idx, 없으면 종류가 같은 것)
    const Shape* match_placeholder(const std::vector<Shape>& shapes, const Shape& placeholder);
    // 슬라이드의 개체 틀을 title = ...처럼 쓸 수 있으면 그 역할
    std::string slide_role(const Presentation& presentation, const Slide& slide, const Shape& shape);
    // 연결선이 잇는 개체들의 id
    std::set<int> connected_ids(const std::vector<Shape>& shapes);

    // 슬라이드의 화면 전환, 자동으로 넘기는 시간, 전환 소리
    struct TransitionInfo {
        bool present = false;
        std::string kind;   // templide의 전환 이름 (fade 등). 없으면 비어 있다
        std::string option;
        std::optional<long long> duration; // ms
        std::optional<long long> advance;  // ms
        std::string sound;  // 소리 파일의 파트 이름 (wav)
        std::string reason; // 바꿀 수 없는 까닭
    };
    TransitionInfo read_transition(const Slide& slide);

    // 노드 id. 마스터 m1, 레이아웃 m1/l2, 슬라이드 s3, 개체 s3/e12, 애니메이션 s3/a4, 배경 s3/bg, 전환 s3/tr, 메모 s3/notes, 검토 메모 s3/rv
    std::string master_id(std::size_t master);
    std::string layout_id(const Presentation& presentation, std::size_t layout);
    std::string slide_id(std::size_t slide);
}
