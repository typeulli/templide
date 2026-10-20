#include "chart.h"

#include "backend.h"

#include <algorithm>
#include <variant>

namespace templide::backend::chart {
    namespace {
        double number_of(const ir::Number& number) {
            double total = 0;
            for (const auto& term : number.terms) {
                total += term.second;
            }
            return total;
        }

        // [[...], [...]]의 줄마다 수를 모은다. 수가 아닌 것이 있으면 nullopt다
        std::optional<std::vector<std::vector<double>>> rows_of(const ir::Value* value) {
            std::vector<std::vector<double>> result;
            const auto* outer = value == nullptr ? nullptr : std::get_if<ir::List>(value);
            if (outer == nullptr) {
                return result;
            }
            for (const auto& item : outer->items) {
                const auto* row = std::get_if<ir::List>(&item);
                if (row == nullptr) {
                    return std::nullopt;
                }
                std::vector<double> numbers;
                for (const auto& cell : row->items) {
                    const auto* number = std::get_if<ir::Number>(&cell);
                    if (number == nullptr) {
                        return std::nullopt;
                    }
                    numbers.push_back(number_of(*number));
                }
                result.push_back(std::move(numbers));
            }
            return result;
        }

        std::vector<std::string> strings_of(const ir::Value* value) {
            std::vector<std::string> result;
            if (const auto* list = value == nullptr ? nullptr : std::get_if<ir::List>(value)) {
                for (const auto& item : list->items) {
                    if (const auto* text = std::get_if<std::string>(&item)) {
                        result.push_back(*text);
                    }
                }
            }
            return result;
        }

        bool flag(const ir::Element& element, const char* name, bool fallback) {
            const auto* value = std::get_if<bool>(find_property(element, name));
            return value == nullptr ? fallback : *value;
        }

        std::string text_of(const ir::Element& element, const char* name) {
            const auto* value = std::get_if<std::string>(find_property(element, name));
            return value == nullptr ? "" : *value;
        }
    }

    const char* kind_name(Kind kind) {
        switch (kind) {
            case Kind::Column: return "column";
            case Kind::Bar: return "bar";
            case Kind::Line: return "line";
            case Kind::Area: return "area";
            case Kind::Pie: return "pie";
            case Kind::Scatter: return "scatter";
        }
        return "column";
    }

    std::optional<Chart> resolve(const ir::Element& element) {
        Chart chart;
        const std::string kind = enum_member(element, "kind");
        if (kind == "column") {
            chart.kind = Kind::Column;
        } else if (kind == "bar") {
            chart.kind = Kind::Bar;
        } else if (kind == "line") {
            chart.kind = Kind::Line;
        } else if (kind == "area") {
            chart.kind = Kind::Area;
        } else if (kind == "pie") {
            chart.kind = Kind::Pie;
        } else if (kind == "scatter") {
            chart.kind = Kind::Scatter;
        } else {
            return std::nullopt;
        }
        const auto values = rows_of(find_property(element, "values"));
        const auto xs = rows_of(find_property(element, "xs"));
        if (!values || !xs || values->empty()) {
            return std::nullopt;
        }
        const std::vector<std::string> names = strings_of(find_property(element, "names"));
        for (std::size_t i = 0; i < values->size(); ++i) {
            Series series;
            series.name = i < names.size() ? names[i] : "Series " + std::to_string(i + 1);
            series.values = (*values)[i];
            if (chart.kind == Kind::Scatter && i < xs->size()) {
                series.xs = (*xs)[i];
            }
            chart.series.push_back(std::move(series));
        }
        if (const auto* colors = std::get_if<ir::List>(find_property(element, "colors"))) {
            for (const auto& item : colors->items) {
                if (const auto* color = std::get_if<ir::Color>(&item)) {
                    chart.colors.push_back(*color);
                }
            }
        }
        for (std::size_t i = 0; i < chart.series.size(); ++i) {
            chart.series[i].color = chart.color_of(i);
        }
        if (chart.kind != Kind::Scatter) {
            chart.categories = strings_of(find_property(element, "categories"));
            const std::size_t count = chart.series.front().values.size();
            for (std::size_t i = chart.categories.size(); i < count; ++i) {
                chart.categories.push_back(std::to_string(i + 1));
            }
        }
        chart.title = text_of(element, "title");
        chart.x_title = text_of(element, "x_title");
        chart.y_title = text_of(element, "y_title");
        if (const std::string legend = enum_member(element, "legend"); !legend.empty()) {
            chart.legend = legend;
        }
        chart.stacked = flag(element, "stacked", false);
        chart.data_labels = flag(element, "data_labels", false);
        chart.smooth = flag(element, "smooth", false);
        chart.markers = flag(element, "markers", true);
        chart.gridlines = flag(element, "gridlines", true);
        if (const auto* range = std::get_if<ir::List>(find_property(element, "range")); range != nullptr && range->items.size() == 2) {
            const auto* low = std::get_if<ir::Number>(&range->items[0]);
            const auto* high = std::get_if<ir::Number>(&range->items[1]);
            if (low != nullptr && high != nullptr) {
                chart.min = number_of(*low);
                chart.max = number_of(*high);
            }
        }
        if (const auto* size = std::get_if<ir::Number>(find_property(element, "font_size"))) {
            chart.font_px = std::max(1.0, number_of(*size));
        }
        if (const auto* color = std::get_if<ir::Color>(find_property(element, "text_color"))) {
            chart.text_color = *color;
        }
        return chart;
    }
}
