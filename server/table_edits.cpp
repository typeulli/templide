#include "edits.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

// 표(table object)를 화면에서 고치는 수정. 원문은 고친 곳만 잘라 바꾸므로 주석과 줄 맞춤이 남는다
namespace templide::server {
    namespace {
        EditResult failure(std::string message) {
            return {{}, std::move(message)};
        }

        const ir::Property* find(const std::vector<ir::Property>& properties, const std::string& name) {
            for (const auto& property : properties) {
                if (property.name == name) {
                    return &property;
                }
            }
            return nullptr;
        }

        std::string amount(double value) {
            value = std::round(value * 100) / 100;
            if (value == 0) {
                value = 0;
            }
            char buffer[32];
            std::snprintf(buffer, sizeof buffer, "%.10g", value);
            return buffer;
        }

        // 원문 위치가 있는 줄의 들여쓰기와, 그 위치가 줄의 첫 글자인지
        struct Line {
            std::size_t begin;
            std::string indent;
            bool own;
        };

        Line line_of(const std::string& text, std::size_t at) {
            std::size_t begin = at;
            while (begin > 0 && text[begin - 1] != '\n') {
                --begin;
            }
            std::string indent;
            std::size_t i = begin;
            while (i < text.size() && (text[i] == ' ' || text[i] == '\t')) {
                indent += text[i++];
            }
            return {begin, indent, i == at};
        }

        // 표의 cells. 모든 줄이 [ ... ] 리터럴로 적혀 있어야 줄과 칸을 끼우고 뺄 수 있다
        struct Grid {
            const ir::List* list = nullptr;
            std::vector<const ir::List*> rows;
            std::size_t row_count = 0;
            std::size_t column_count = 0;
            std::string path;
        };

        std::string reason_of(const ir::Origin& origin) {
            return lock_message(origin.reason.empty() ? "computed" : origin.reason);
        }

        bool read_grid(const ir::Element& element, Grid& grid, std::string& error) {
            const auto* property = find(element.properties, "cells");
            const auto* list = property != nullptr ? std::get_if<ir::List>(&property->value) : nullptr;
            if (list == nullptr || list->items.empty()) {
                error = "This table has no cells";
                return false;
            }
            if (list->origin.kind != ir::Origin::Kind::LITERAL) {
                error = reason_of(list->origin);
                return false;
            }
            grid.list = list;
            grid.path = list->origin.range.path;
            grid.row_count = list->items.size();
            for (std::size_t r = 0; r < list->items.size(); ++r) {
                const auto* row = std::get_if<ir::List>(&list->items[r]);
                if (row == nullptr || row->origin.kind != ir::Origin::Kind::LITERAL || row->origin.range.path != grid.path || list->ranges[r].path != grid.path) {
                    error = "Row " + std::to_string(r + 1) + " is not written as a list in the code; change it in the code";
                    return false;
                }
                if (r == 0) {
                    grid.column_count = row->items.size();
                }
                for (const auto& range : row->ranges) {
                    if (range.path != grid.path) {
                        error = "A cell is written in another file; change it in the code";
                        return false;
                    }
                }
                grid.rows.push_back(row);
            }
            return true;
        }

        // 리스트 항목 하나를 index번째 앞에 끼운다. 항목들이 줄마다 하나씩 적혀 있으면 새 줄에, 아니면 같은 줄에 쓴다
        TextEdit insert_item(const std::string& text, const std::string& path, const std::vector<ir::SourceRange>& ranges, std::size_t index, const std::string& item) {
            if (index < ranges.size()) {
                const Line line = line_of(text, ranges[index].begin);
                if (line.own) {
                    return {path, line.begin, line.begin, line.indent + item + ",\n"};
                }
                return {path, ranges[index].begin, ranges[index].begin, item + ", "};
            }
            const ir::SourceRange& last = ranges.back();
            const Line line = line_of(text, last.begin);
            if (line.own) {
                return {path, last.end, last.end, ",\n" + line.indent + item};
            }
            return {path, last.end, last.end, ", " + item};
        }

        // 리스트 항목 하나를 지운다. 쉼표와 공백도 함께 지운다. 항목이 둘 이상이어야 한다
        TextEdit remove_item(const std::string& path, const std::vector<ir::SourceRange>& ranges, std::size_t index) {
            if (index + 1 < ranges.size()) {
                return {path, ranges[index].begin, ranges[index + 1].begin, ""};
            }
            return {path, ranges[index - 1].end, ranges[index].end, ""};
        }

        // cell 블록의 col_span, row_span. 없으면 1
        std::size_t span_of(const ir::CellStyle& style, const char* name) {
            const auto* property = find(style.properties, name);
            const auto* number = property != nullptr ? std::get_if<ir::Number>(&property->value) : nullptr;
            if (number == nullptr || number->terms.empty()) {
                return 1;
            }
            return static_cast<std::size_t>(std::max(1.0, std::round(number->terms.front().second)));
        }

        const ir::CellStyle* style_at(const ir::Element& element, std::size_t row, std::size_t column) {
            for (const auto& style : element.cell_styles) {
                if (style.row == static_cast<int>(row + 1) && style.column == static_cast<int>(column + 1)) {
                    return &style;
                }
            }
            return nullptr;
        }

        // cell 블록 안의 속성 하나를 바꾼다. 없으면 블록에 넣는다
        EditResult set_in_block(const Editor& editor, const ir::CellStyle& style, const std::string& name, const NewValue& value) {
            if (const auto* found = find(style.properties, name)) {
                return editor.set(found->origin, name, value);
            }
            ir::Origin origin = style.source;
            origin.name = name;
            return editor.set(origin, name, value);
        }

        void append(EditResult& result, EditResult part) {
            if (result.error.empty() && !part.error.empty()) {
                result.error = part.error;
            }
            result.edits.insert(result.edits.end(), part.edits.begin(), part.edits.end());
        }

        NewValue number(double value) {
            return {NewValue::Kind::NUMBER, value};
        }

        // 표 속성 중 정수(header_rows 등)
        std::size_t whole(const ir::Element& element, const char* name) {
            const auto* property = find(element.properties, name);
            const auto* value = property != nullptr ? std::get_if<ir::Number>(&property->value) : nullptr;
            if (value == nullptr || value->terms.empty()) {
                return 0;
            }
            return static_cast<std::size_t>(std::max(0.0, std::round(value->terms.front().second)));
        }
    }

    EditResult Editor::table_set_text(const ir::Element& element, std::size_t row, std::size_t column, const std::string& text) const {
        Grid grid;
        std::string error;
        if (!read_grid(element, grid, error)) {
            return failure(error);
        }
        if (row >= grid.row_count || column >= grid.rows[row]->items.size()) {
            return failure("No such cell");
        }
        const ir::List& cells = *grid.rows[row];
        // 문자열 리터럴 하나뿐인 칸은 그 리터럴만 바꿔 감싼 style(...)을 지킨다
        if (const auto* content = std::get_if<ir::Text>(&cells.items[column])) {
            if (content->paragraphs.size() == 1 && content->paragraphs.front().runs.size() == 1) {
                const ir::Run& run = content->paragraphs.front().runs.front();
                const std::string* source = text_of(run.origin.range.path);
                if (run.origin.kind == ir::Origin::Kind::LITERAL && run.origin.text && source != nullptr && run.origin.range.begin < source->size() && (*source)[run.origin.range.begin] == '"') {
                    return set_text(run, text);
                }
            }
        }
        const ir::SourceRange& range = cells.ranges[column];
        return {{{grid.path, range.begin, range.end, quote_string(text)}}, ""};
    }

    EditResult Editor::table_insert(const ir::Element& element, bool rows, std::size_t index) const {
        Grid grid;
        std::string error;
        if (!read_grid(element, grid, error)) {
            return failure(error);
        }
        const std::size_t count = rows ? grid.row_count : grid.column_count;
        if (index > count) {
            return failure("No such position");
        }
        const std::string* text = text_of(grid.path);
        if (text == nullptr) {
            return failure("Cannot find the source file");
        }
        EditResult result;
        if (rows) {
            std::string row = "[";
            for (std::size_t c = 0; c < grid.column_count; ++c) {
                row += (c > 0 ? ", " : "") + std::string("\"\"");
            }
            result.edits.push_back(insert_item(*text, grid.path, grid.list->ranges, index, row + "]"));
        } else {
            for (const auto* row : grid.rows) {
                result.edits.push_back(insert_item(*text, grid.path, row->ranges, index, "\"\""));
            }
        }
        // col_widths, row_heights. 새 줄은 이웃한 줄과 같은 비율이다
        if (const auto* property = find(element.properties, rows ? "row_heights" : "col_widths")) {
            if (const auto* sizes = std::get_if<ir::List>(&property->value); sizes != nullptr && !sizes->items.empty()) {
                if (sizes->origin.kind != ir::Origin::Kind::LITERAL || sizes->ranges.size() != sizes->items.size()) {
                    return failure(std::string("'") + property->name + "' is not written as a list in the code; change it in the code");
                }
                const std::string* sizes_text = text_of(sizes->origin.range.path);
                const ir::SourceRange& neighbor = sizes->ranges[std::min(index, sizes->items.size() - 1)];
                if (sizes_text == nullptr) {
                    return failure("Cannot find the source file");
                }
                result.edits.push_back(insert_item(*sizes_text, sizes->origin.range.path, sizes->ranges, index, sizes_text->substr(neighbor.begin, neighbor.end - neighbor.begin)));
            }
        }
        // 머리글 한가운데에 끼우면 머리글이 하나 늘어난다
        const char* header = rows ? "header_rows" : "header_cols";
        if (index < whole(element, header)) {
            append(result, set_property(element, header, number(static_cast<double>(whole(element, header) + 1))));
        }
        // cell 블록. 뒤쪽 번호는 하나 밀고, 병합한 칸 한가운데에 끼우면 그 칸이 늘어난다
        for (const auto& style : element.cell_styles) {
            const std::size_t position = static_cast<std::size_t>(rows ? style.row : style.column) - 1;
            const std::size_t span = span_of(style, rows ? "row_span" : "col_span");
            if (position >= index) {
                append(result, set(rows ? style.row_origin : style.column_origin, "", number(static_cast<double>(position + 2))));
            } else if (position + span > index) {
                append(result, set_in_block(*this, style, rows ? "row_span" : "col_span", number(static_cast<double>(span + 1))));
            }
        }
        if (!result.error.empty()) {
            return failure(result.error);
        }
        return result;
    }

    EditResult Editor::table_remove(const ir::Element& element, bool rows, std::size_t index) const {
        Grid grid;
        std::string error;
        if (!read_grid(element, grid, error)) {
            return failure(error);
        }
        const std::size_t count = rows ? grid.row_count : grid.column_count;
        if (index >= count) {
            return failure("No such position");
        }
        if (count <= 1) {
            return failure("A table needs at least one row and one column");
        }
        EditResult result;
        if (rows) {
            result.edits.push_back(remove_item(grid.path, grid.list->ranges, index));
        } else {
            for (const auto* row : grid.rows) {
                result.edits.push_back(remove_item(grid.path, row->ranges, index));
            }
        }
        if (const auto* property = find(element.properties, rows ? "row_heights" : "col_widths")) {
            if (const auto* sizes = std::get_if<ir::List>(&property->value); sizes != nullptr && !sizes->items.empty()) {
                if (sizes->origin.kind != ir::Origin::Kind::LITERAL || sizes->ranges.size() != sizes->items.size() || sizes->items.size() <= index) {
                    return failure(std::string("'") + property->name + "' is not written as a list in the code; change it in the code");
                }
                result.edits.push_back(remove_item(sizes->origin.range.path, sizes->ranges, index));
            }
        }
        const char* header = rows ? "header_rows" : "header_cols";
        if (index < whole(element, header)) {
            append(result, set_property(element, header, number(static_cast<double>(whole(element, header) - 1))));
        }
        for (const auto& style : element.cell_styles) {
            const std::size_t position = static_cast<std::size_t>(rows ? style.row : style.column) - 1;
            const std::size_t span = span_of(style, rows ? "row_span" : "col_span");
            if (position == index) {
                append(result, remove_statement(style.source));
            } else if (position > index) {
                append(result, set(rows ? style.row_origin : style.column_origin, "", number(static_cast<double>(position))));
            } else if (position + span > index) {
                append(result, set_in_block(*this, style, rows ? "row_span" : "col_span", number(static_cast<double>(span - 1))));
            }
        }
        if (!result.error.empty()) {
            return failure(result.error);
        }
        return result;
    }

    EditResult Editor::table_merge(const ir::Element& element, std::size_t row, std::size_t column, std::size_t row_span, std::size_t column_span) const {
        Grid grid;
        std::string error;
        if (!read_grid(element, grid, error)) {
            return failure(error);
        }
        if (row_span * column_span <= 1) {
            return failure("Select at least two cells to merge");
        }
        if (row + row_span > grid.row_count || column + column_span > grid.column_count) {
            return failure("The selection reaches outside the table");
        }
        EditResult result;
        // 겹치는 병합이 있으면 먼저 풀어야 한다. 가려질 칸의 cell 블록은 지운다
        for (const auto& style : element.cell_styles) {
            const std::size_t r = static_cast<std::size_t>(style.row) - 1;
            const std::size_t c = static_cast<std::size_t>(style.column) - 1;
            if (r == row && c == column) {
                continue;
            }
            const std::size_t rs = span_of(style, "row_span");
            const std::size_t cs = span_of(style, "col_span");
            const bool overlaps = r < row + row_span && row < r + rs && c < column + column_span && column < c + cs;
            if (!overlaps) {
                continue;
            }
            if (rs * cs > 1) {
                return failure("The selection overlaps a merged cell; split it first");
            }
            append(result, remove_statement(style.source));
        }
        // 왼쪽 위 칸에 span을 적는다
        if (const ir::CellStyle* anchor = style_at(element, row, column)) {
            if (column_span > 1) {
                append(result, set_in_block(*this, *anchor, "col_span", number(static_cast<double>(column_span))));
            }
            if (row_span > 1) {
                append(result, set_in_block(*this, *anchor, "row_span", number(static_cast<double>(row_span))));
            }
        } else if (element.from_template) {
            return failure(lock_message("template"));
        } else {
            std::string block = "cell(" + std::to_string(row + 1) + ", " + std::to_string(column + 1) + ") {";
            if (column_span > 1) {
                block += " col_span = " + std::to_string(column_span) + ";";
            }
            if (row_span > 1) {
                block += " row_span = " + std::to_string(row_span) + ";";
            }
            append(result, insert_into(element.source, block + " }"));
        }
        // 가려지는 칸의 글은 비운다
        for (std::size_t r = row; r < row + row_span; ++r) {
            for (std::size_t c = column; c < column + column_span; ++c) {
                if (r == row && c == column) {
                    continue;
                }
                const auto* content = std::get_if<ir::Text>(&grid.rows[r]->items[c]);
                bool empty = true;
                if (content != nullptr) {
                    for (const auto& paragraph : content->paragraphs) {
                        for (const auto& run : paragraph.runs) {
                            empty = empty && run.text.empty() && run.field.empty();
                        }
                    }
                }
                if (!empty) {
                    const ir::SourceRange& range = grid.rows[r]->ranges[c];
                    result.edits.push_back({grid.path, range.begin, range.end, "\"\""});
                }
            }
        }
        if (!result.error.empty()) {
            return failure(result.error);
        }
        return result;
    }

    EditResult Editor::table_split(const ir::Element& element, std::size_t row, std::size_t column) const {
        const ir::CellStyle* style = style_at(element, row, column);
        if (style == nullptr || span_of(*style, "row_span") * span_of(*style, "col_span") <= 1) {
            return failure("This cell is not merged");
        }
        std::size_t others = 0;
        for (const auto& property : style->properties) {
            if (property.name != "row_span" && property.name != "col_span") {
                ++others;
            }
        }
        if (others == 0) {
            return remove_statement(style->source);
        }
        EditResult result;
        for (const char* name : {"col_span", "row_span"}) {
            if (const auto* property = find(style->properties, name)) {
                append(result, unset(property->origin));
            }
        }
        if (!result.error.empty()) {
            return failure(result.error);
        }
        return result;
    }

    EditResult Editor::table_set_cell(const ir::Element& element, std::size_t row, std::size_t column, const std::string& property, const NewValue& value) const {
        if (const ir::CellStyle* style = style_at(element, row, column)) {
            return set_in_block(*this, *style, property, value);
        }
        // template이 만든 표의 source는 바깥쪽 put이라 거기에 cell 블록을 넣을 수 없다
        if (element.from_template) {
            return failure(lock_message("template"));
        }
        if (element.source.kind != ir::Origin::Kind::BLOCK) {
            return failure(lock_message(element.source.reason));
        }
        ir::Origin fresh;
        fresh.kind = ir::Origin::Kind::BLOCK;
        std::string error;
        const std::string text = code(fresh, property, value, error);
        if (!error.empty()) {
            return failure(error);
        }
        return insert_into(element.source, "cell(" + std::to_string(row + 1) + ", " + std::to_string(column + 1) + ") { " + property + " = " + text + "; }");
    }

    EditResult Editor::table_unset_cell(const ir::Element& element, std::size_t row, std::size_t column, const std::string& property) const {
        const ir::CellStyle* style = style_at(element, row, column);
        const auto* found = style != nullptr ? find(style->properties, property) : nullptr;
        if (found == nullptr) {
            return {};
        }
        if (style->properties.size() == 1) {
            return remove_statement(style->source);
        }
        return unset(found->origin);
    }

    EditResult Editor::table_set_sizes(const ir::Element& element, bool columns, const std::vector<double>& pixels) const {
        Grid grid;
        std::string error;
        if (!read_grid(element, grid, error)) {
            return failure(error);
        }
        if (pixels.size() != (columns ? grid.column_count : grid.row_count)) {
            return failure("The number of sizes does not match the table");
        }
        std::string list = "[";
        for (std::size_t i = 0; i < pixels.size(); ++i) {
            list += (i > 0 ? ", " : "") + amount(pixels[i]) + "px";
        }
        return set_property(element, columns ? "col_widths" : "row_heights", {NewValue::Kind::CODE, 0, list + "]"});
    }
}
