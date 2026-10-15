#pragma once

#include "../middleend/ir.h"

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

/** 백엔드가 표를 그릴 수 있도록 병합과 셀 서식을 해석한 결과를 제공한다. */
namespace templide::backend::table {
    /** 셀 테두리 하나. width가 0이면 선을 그리지 않는다. */
    struct Line {
        ir::Color color;
        double width = 0;
        std::string dash; // 실선이 아니면 dash_style 이름(dot, dash 등), 실선이면 빈 문자열
    };

    /** 표의 셀 하나. 병합된 영역은 왼쪽 위 셀에 span으로 기록한다. */
    struct Cell {
        std::size_t row = 0;    // 0부터
        std::size_t column = 0;
        std::size_t row_span = 1;
        std::size_t column_span = 1;
        bool covered = false;
        std::size_t owner_row = 0;    // 가려진 칸이면 그 칸을 가린 병합 칸(왼쪽 위)의 자리. 아니면 자기 자리
        std::size_t owner_column = 0;
        bool header = false;
        ir::Text text;                 // 머리글의 굵은 글자와 칸의 가로 정렬을 반영한 글
        std::optional<ir::Value> fill; // 색, 그라데이션, 무늬, 그림. 없으면 칠하지 않는다
        std::string anchor;            // top, middle, bottom
        std::string align;             // left, center, right, justify. 글에 정하지 않은 문단의 가로 정렬
        std::array<double, 4> padding{8, 8, 8, 8}; // 왼쪽, 위, 오른쪽, 아래 (px)
        // 왼쪽, 위, 오른쪽, 아래. 이웃한 칸과 맞닿는 선은 두 칸이 같은 값을 갖는다
        std::array<Line, 4> border;
    };

    struct Table {
        std::size_t rows = 0;
        std::size_t columns = 0;
        std::size_t header_rows = 0;
        std::size_t header_columns = 0;
        std::vector<ir::Number> column_widths; // 비었으면 똑같이 나눈다. 값은 비율이다
        std::vector<ir::Number> row_heights;
        std::vector<Cell> cells;               // 행 우선

        const Cell& at(std::size_t row, std::size_t column) const {
            return cells[row * columns + column];
        }
    };

    /** IR의 table element를 백엔드용 표 구조로 변환한다. */
    std::optional<Table> resolve(const ir::Element& element);

    /** 비율을 total에 맞춰 count개 길이로 나눈다. */
    std::vector<double> divide(const std::vector<double>& weights, std::size_t count, double total);

    /** 누적 반올림으로 정수 길이를 나누어 합을 total로 맞춘다. */
    std::vector<long long> divide_integer(const std::vector<double>& weights, std::size_t count, long long total);
}
