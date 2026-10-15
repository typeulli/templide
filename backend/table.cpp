#include "table.h"

#include "backend.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace templide::backend::table {
    namespace {
        double pixels(const ir::Number& number) {
            double total = 0;
            for (const auto& term : number.terms) {
                total += term.second;
            }
            return total;
        }

        const ir::Value* find(const std::vector<ir::Property>& properties, const std::string& name) {
            for (const auto& property : properties) {
                if (property.name == name) {
                    return &property.value;
                }
            }
            return nullptr;
        }

        std::optional<double> number_of(const std::vector<ir::Property>& properties, const std::string& name) {
            if (const auto* number = std::get_if<ir::Number>(find(properties, name))) {
                return pixels(*number);
            }
            return std::nullopt;
        }

        std::string member_of(const std::vector<ir::Property>& properties, const std::string& name) {
            const auto* value = std::get_if<ir::EnumValue>(find(properties, name));
            return value != nullptr ? value->member : "";
        }

        // 선 속성을 읽는 중인 값과 그 값을 정한 정도. 안 정했으면 0, 칸의 line_*이면 1, 칸의 line_*_left 같은 한 쪽이면 2다
        struct Side {
            Line line;
            int rank = 0;
        };

        constexpr std::size_t left = 0;
        constexpr std::size_t top = 1;
        constexpr std::size_t right = 2;
        constexpr std::size_t bottom = 3;
        const std::array<const char*, 4> side_names = {"left", "top", "right", "bottom"};

        std::size_t whole(const std::vector<ir::Property>& properties, const std::string& name, std::size_t limit) {
            const double value = number_of(properties, name).value_or(0);
            return value < 0 ? 0 : std::min(limit, static_cast<std::size_t>(std::llround(value)));
        }
    }

    std::optional<Table> resolve(const ir::Element& element) {
        const auto* grid = std::get_if<ir::List>(find_property(element, "cells"));
        if (grid == nullptr || grid->items.empty()) {
            return std::nullopt;
        }
        const auto* first_row = std::get_if<ir::List>(&grid->items.front());
        if (first_row == nullptr || first_row->items.empty()) {
            return std::nullopt;
        }
        Table table;
        table.rows = grid->items.size();
        table.columns = first_row->items.size();
        const std::vector<ir::Property>& properties = element.properties;
        table.header_rows = whole(properties, "header_rows", table.rows);
        table.header_columns = whole(properties, "header_cols", table.columns);
        const auto numbers = [&](const char* name) {
            std::vector<ir::Number> result;
            if (const auto* list = std::get_if<ir::List>(find(properties, name))) {
                for (const auto& item : list->items) {
                    if (const auto* number = std::get_if<ir::Number>(&item)) {
                        result.push_back(*number);
                    }
                }
            }
            return result;
        };
        table.column_widths = numbers("col_widths");
        table.row_heights = numbers("row_heights");
        const auto flag = [&](const char* name) {
            const auto* value = std::get_if<bool>(find(properties, name));
            return value != nullptr && *value;
        };
        const bool banded_rows = flag("banded_rows");
        const bool banded_columns = flag("banded_cols");

        // 표 전체의 값. 칸마다 cell 블록이 덮어쓴다
        const std::string table_anchor = member_of(properties, "anchor").empty() ? "middle" : member_of(properties, "anchor");
        const std::string table_align = member_of(properties, "align").empty() ? "left" : member_of(properties, "align");
        const double table_padding = number_of(properties, "padding").value_or(8);
        std::array<double, 4> table_sides{};
        for (std::size_t side = 0; side < 4; ++side) {
            table_sides[side] = number_of(properties, std::string("padding_") + side_names[side]).value_or(table_padding);
        }
        const ir::Value* table_fill = find(properties, "fill");
        const ir::Value* header_fill = find(properties, "header_fill");
        const ir::Value* band_fill = find(properties, "band_fill");
        Line table_line;
        table_line.color = ir::Color{0xBF, 0xBF, 0xBF, 1, ""};
        table_line.width = 1;
        if (const auto* color = std::get_if<ir::Color>(find(properties, "line_color"))) {
            table_line.color = *color;
        }
        table_line.width = number_of(properties, "line_width").value_or(table_line.width);
        if (const std::string dash = member_of(properties, "line_dash"); dash != "solid") {
            table_line.dash = dash;
        }

        std::map<std::pair<std::size_t, std::size_t>, const ir::CellStyle*> styles;
        for (const auto& style : element.cell_styles) {
            styles[{static_cast<std::size_t>(style.row), static_cast<std::size_t>(style.column)}] = &style;
        }

        table.cells.resize(table.rows * table.columns);
        std::vector<std::size_t> owner(table.cells.size());
        for (std::size_t r = 0; r < table.rows; ++r) {
            for (std::size_t c = 0; c < table.columns; ++c) {
                Cell& cell = table.cells[r * table.columns + c];
                cell.row = r;
                cell.column = c;
                cell.owner_row = r;
                cell.owner_column = c;
                owner[r * table.columns + c] = r * table.columns + c;
            }
        }
        // 병합. 가려진 칸은 왼쪽 위 칸이 주인이다
        for (const auto& [position, style] : styles) {
            const std::size_t r = position.first - 1;
            const std::size_t c = position.second - 1;
            if (r >= table.rows || c >= table.columns) {
                continue;
            }
            const std::size_t row_span = std::max<std::size_t>(1, whole(style->properties, "row_span", table.rows - r));
            const std::size_t column_span = std::max<std::size_t>(1, whole(style->properties, "col_span", table.columns - c));
            Cell& anchor = table.cells[r * table.columns + c];
            anchor.row_span = row_span;
            anchor.column_span = column_span;
            for (std::size_t rr = r; rr < r + row_span; ++rr) {
                for (std::size_t cc = c; cc < c + column_span; ++cc) {
                    if (rr != r || cc != c) {
                        Cell& covered = table.cells[rr * table.columns + cc];
                        covered.covered = true;
                        covered.owner_row = r;
                        covered.owner_column = c;
                        owner[rr * table.columns + cc] = r * table.columns + c;
                    }
                }
            }
        }

        std::vector<std::array<Side, 4>> sides(table.cells.size());
        for (std::size_t r = 0; r < table.rows; ++r) {
            const auto* row = std::get_if<ir::List>(&grid->items[r]);
            for (std::size_t c = 0; c < table.columns; ++c) {
                Cell& cell = table.cells[r * table.columns + c];
                if (row != nullptr && c < row->items.size()) {
                    if (const auto* text = std::get_if<ir::Text>(&row->items[c])) {
                        cell.text = *text;
                    }
                }
                const auto found = styles.find({r + 1, c + 1});
                const std::vector<ir::Property> none;
                const std::vector<ir::Property>& cell_properties = found != styles.end() ? found->second->properties : none;

                cell.header = r < table.header_rows || c < table.header_columns;
                const bool banded = (banded_rows && r >= table.header_rows && (r - table.header_rows) % 2 == 0)
                    || (banded_columns && c >= table.header_columns && (c - table.header_columns) % 2 == 0);
                const ir::Value* fill = find(cell_properties, "fill");
                if (fill == nullptr) {
                    fill = cell.header ? header_fill : banded ? band_fill : table_fill;
                }
                if (fill != nullptr) {
                    cell.fill = *fill;
                }
                cell.anchor = member_of(cell_properties, "anchor").empty() ? table_anchor : member_of(cell_properties, "anchor");
                const std::string align = member_of(cell_properties, "align").empty() ? table_align : member_of(cell_properties, "align");
                cell.align = align;

                const auto cell_padding = number_of(cell_properties, "padding");
                for (std::size_t side = 0; side < 4; ++side) {
                    cell.padding[side] = number_of(cell_properties, std::string("padding_") + side_names[side]).value_or(cell_padding ? *cell_padding : table_sides[side]);
                }

                // 머리글은 굵게, 칸의 정렬은 글에 정하지 않은 문단에만 적용한다
                for (auto& paragraph : cell.text.paragraphs) {
                    for (auto& run : paragraph.runs) {
                        if (cell.header && !run.style.font_weight) {
                            run.style.font_weight = ir::EnumValue{"font_weight", "bold"};
                        }
                        if (align != "left" && !run.style.text_align) {
                            run.style.text_align = ir::EnumValue{"text_align", align};
                        }
                    }
                }

                // 테두리. 표의 값 < 칸 전체의 line_* < 칸의 한쪽 line_*_방향
                std::array<Side, 4>& own = sides[r * table.columns + c];
                for (auto& side : own) {
                    side.line = table_line;
                }
                const auto* all_color = std::get_if<ir::Color>(find(cell_properties, "line_color"));
                const auto all_width = number_of(cell_properties, "line_width");
                const std::string all_dash = member_of(cell_properties, "line_dash");
                for (std::size_t side = 0; side < 4; ++side) {
                    Side& value = own[side];
                    if (all_color != nullptr) {
                        value.line.color = *all_color;
                        value.rank = std::max(value.rank, 1);
                    }
                    if (all_width) {
                        value.line.width = *all_width;
                        value.rank = std::max(value.rank, 1);
                    }
                    if (!all_dash.empty()) {
                        value.line.dash = all_dash == "solid" ? "" : all_dash;
                        value.rank = std::max(value.rank, 1);
                    }
                    if (const auto* color = std::get_if<ir::Color>(find(cell_properties, std::string("line_color_") + side_names[side]))) {
                        value.line.color = *color;
                        value.rank = 2;
                    }
                    if (const auto width = number_of(cell_properties, std::string("line_width_") + side_names[side])) {
                        value.line.width = *width;
                        value.rank = 2;
                    }
                }
            }
        }

        // 마주 닿는 선은 더 분명하게 정한 쪽을 따르고, 같으면 오른쪽, 아래쪽 칸을 따른다. 병합한 칸 안쪽 선은 그리지 않고,
        // 병합한 칸의 한 변은 그 변이 시작하는 곳에서 만나는 선 하나를 쓴다
        std::vector<std::array<Line, 4>> borders(table.cells.size());
        for (std::size_t i = 0; i < table.cells.size(); ++i) {
            for (std::size_t side = 0; side < 4; ++side) {
                borders[i][side] = sides[i][side].line;
            }
        }
        const auto pick = [&](std::size_t before, std::size_t before_side, std::size_t after, std::size_t after_side) {
            return sides[before][before_side].rank > sides[after][after_side].rank ? sides[before][before_side].line : sides[after][after_side].line;
        };
        for (std::size_t r = 0; r < table.rows; ++r) {
            for (std::size_t c = 1; c < table.columns; ++c) {
                const std::size_t a = owner[r * table.columns + c - 1];
                const std::size_t b = owner[r * table.columns + c];
                if (a == b) {
                    continue;
                }
                const Line line = pick(a, right, b, left);
                if (table.cells[a].row == r) {
                    borders[a][right] = line;
                }
                if (table.cells[b].row == r) {
                    borders[b][left] = line;
                }
            }
        }
        for (std::size_t r = 1; r < table.rows; ++r) {
            for (std::size_t c = 0; c < table.columns; ++c) {
                const std::size_t a = owner[(r - 1) * table.columns + c];
                const std::size_t b = owner[r * table.columns + c];
                if (a == b) {
                    continue;
                }
                const Line line = pick(a, bottom, b, top);
                if (table.cells[a].column == c) {
                    borders[a][bottom] = line;
                }
                if (table.cells[b].column == c) {
                    borders[b][top] = line;
                }
            }
        }
        for (std::size_t i = 0; i < table.cells.size(); ++i) {
            table.cells[i].border = borders[i];
        }
        return table;
    }

    std::vector<double> divide(const std::vector<double>& weights, std::size_t count, double total) {
        double sum = 0;
        for (const double weight : weights) {
            sum += weight;
        }
        if (count == 0) {
            return {};
        }
        if (weights.size() != count || !(sum > 0)) {
            return std::vector<double>(count, total / static_cast<double>(count));
        }
        std::vector<double> result;
        for (const double weight : weights) {
            result.push_back(total * weight / sum);
        }
        return result;
    }

    std::vector<long long> divide_integer(const std::vector<double>& weights, std::size_t count, long long total) {
        const std::vector<double> parts = divide(weights, count, static_cast<double>(total));
        std::vector<long long> result;
        double cumulative = 0;
        long long previous = 0;
        for (std::size_t i = 0; i < parts.size(); ++i) {
            cumulative += parts[i];
            const long long edge = i + 1 == parts.size() ? total : std::llround(cumulative);
            result.push_back(edge - previous);
            previous = edge;
        }
        return result;
    }
}
