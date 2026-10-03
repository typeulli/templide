#pragma once

#include <array>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// 미들 엔드와 backend가 함께 쓰는 도형 자료와 기하 계산
namespace templide::geometry {
    struct Point {
        double x = 0;
        double y = 0;
    };

    // 연결점 번호와 도형 상자에 대한 위치 비율
    struct Site {
        int index;
        double x;
        double y;
    };

    // 도형 종류 -> 위, 오른쪽, 아래, 왼쪽에 가장 가까운 연결점. PowerPoint에서 연결점마다 선을 붙여 잰 값이다
    const std::map<std::string, std::array<Site, 4>>& connection_sites();

    // 도형 종류 -> 조정값 (이름, 기본값). PowerPoint은 조정값을 하나라도 적으면 모두 적어야 파일을 연다
    const std::map<std::string, std::vector<std::pair<std::string, long long>>>& shape_adjustments();

    // SVG path를 moveTo, lineTo, 3차 베지어, close만으로 바꾼 것. 좌표는 path에 적힌 그대로다
    struct PathSegment {
        char command; // 'M', 'L', 'C', 'Z'
        std::vector<Point> points;
    };

    struct ParsedPath {
        std::vector<PathSegment> segments;
        Point min;
        Point max;
    };

    // 잘못된 path면 nullopt이고 error에 까닭을 남긴다
    std::optional<ParsedPath> parse_svg_path(const std::string& text, std::string& error);

    // CSS의 rotateX(a)는 PowerPoint의 Y 회전 css_rotate_x_sign * a와, rotateY(a)는 X 회전 css_rotate_y_sign * a와 같게 보인다.
    // PowerPoint의 X 회전은 오른쪽이, Y 회전은 아래쪽이 멀어진다
    constexpr int css_rotate_x_sign = -1;
    constexpr int css_rotate_y_sign = 1;

    // 연결선의 모양. 좌표는 EMU이고 rotation은 90도 단위(0, 90, 180, 270)다
    struct ConnectorGeometry {
        std::string preset;
        int rotation = 0;
        bool flip_h = false;
        bool flip_v = false;
        double x = 0;
        double y = 0;
        double width = 0;
        double height = 0;
        std::vector<long long> adjust; // adj1, adj2, ...
    };

    // start에서 start_out 방향(연결점이 있는 변의 바깥쪽)으로 나가서, end_out 쪽 변으로 end에 들어가는 선.
    // kind는 straight, elbow, curved. margin은 되돌아갈 때 도형에서 떨어지는 거리
    ConnectorGeometry route_connector(const std::string& kind, Point start, Point start_out, Point end, Point end_out, double margin);

    // 연결선이 붙는 개체의 모양과 자리. 좌표의 단위는 route_connector와 같다
    struct Anchor {
        std::string kind; // 연결점 표의 도형 종류
        double x;
        double y;
        double width;
        double height;
        double rotation; // 라디안
        bool flip_h;
        bool flip_v;
    };

    struct AnchorPoint {
        int site; // 연결점 번호
        Point point;
        Point out;
    };

    struct ConnectorPlan {
        ConnectorGeometry geometry;
        AnchorPoint start;
        AnchorPoint end;
    };

    // from의 from_side 쪽 연결점에서 to의 to_side 쪽 연결점으로 가는 연결선. side는 top, right, bottom, left, auto이고
    // auto(또는 빈 문자열)는 두 개체의 가운데를 이은 방향에서 마주 보는 쪽이다. 연결점이 없는 종류면 nullopt
    std::optional<ConnectorPlan> plan_connector(const Anchor& from, const Anchor& to, std::string from_side, std::string to_side, const std::string& kind, double margin);

    // 그림 틀(x, y, width, height)과 자르기 비율(왼쪽, 위, 오른쪽, 아래). fit이 cover면 넘치는 쪽을 가운데 기준으로 더 자르고,
    // contain이면 틀을 줄인다. image_size(가로, 세로 px)를 모르면 그대로 둔다
    struct ImageFit {
        double x;
        double y;
        double width;
        double height;
        std::array<double, 4> crop;
    };

    ImageFit fit_image(ImageFit frame, const std::string& fit, std::optional<std::pair<int, int>> image_size);
}
