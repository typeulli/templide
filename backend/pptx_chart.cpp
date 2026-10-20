#include "pptx_chart.h"

#include "xlsx.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace templide::backend::pptx_chart {
    namespace {
        const std::string declaration = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n";

        std::string escape(const std::string& text) {
            std::string result;
            for (const char c : text) {
                switch (c) {
                    case '&': result += "&amp;"; break;
                    case '<': result += "&lt;"; break;
                    case '>': result += "&gt;"; break;
                    case '"': result += "&quot;"; break;
                    default: result += c;
                }
            }
            return result;
        }

        std::string number_text(double value) {
            char buffer[32];
            std::snprintf(buffer, sizeof buffer, "%.15g", value);
            return buffer;
        }

        std::string val(const std::string& name, const std::string& value) {
            return "<c:" + name + " val=\"" + value + "\"/>";
        }

        // 글자 상수(val("axId", "111"))가 bool로 가지 않도록
        std::string val(const std::string& name, const char* value) {
            return val(name, std::string(value));
        }

        std::string val(const std::string& name, bool value) {
            return val(name, std::string(value ? "1" : "0"));
        }

        // Sheet1의 칸 범위. 열은 0부터, 행은 1부터
        std::string range(std::size_t column, std::size_t first_row, std::size_t last_row) {
            const std::string name = xlsx::column_name(column);
            return "Sheet1!$" + name + "$" + std::to_string(first_row) + (last_row > first_row ? ":$" + name + "$" + std::to_string(last_row) : "");
        }

        std::string string_reference(const std::string& formula, const std::vector<std::string>& values) {
            std::string xml = "<c:strRef><c:f>" + formula + "</c:f><c:strCache>" + val("ptCount", std::to_string(values.size()));
            for (std::size_t i = 0; i < values.size(); ++i) {
                xml += "<c:pt idx=\"" + std::to_string(i) + "\"><c:v>" + escape(values[i]) + "</c:v></c:pt>";
            }
            return xml + "</c:strCache></c:strRef>";
        }

        std::string number_reference(const std::string& formula, const std::vector<double>& values) {
            std::string xml = "<c:numRef><c:f>" + formula + "</c:f><c:numCache><c:formatCode>General</c:formatCode>" + val("ptCount", std::to_string(values.size()));
            for (std::size_t i = 0; i < values.size(); ++i) {
                xml += "<c:pt idx=\"" + std::to_string(i) + "\"><c:v>" + number_text(values[i]) + "</c:v></c:pt>";
            }
            return xml + "</c:numCache></c:numRef>";
        }

        class Builder {
        public:
            Builder(const chart::Chart& chart, const std::function<std::string(const ir::Color&)>& color) : c_(chart), color_(color) {}

            Parts build() {
                Parts parts;
                parts.workbook = xlsx::workbook(rows());
                parts.chart = chart_xml(parts.workbook.has_value());
                return parts;
            }

        private:
            const chart::Chart& c_;
            const std::function<std::string(const ir::Color&)>& color_;

            bool scatter() const { return c_.kind == chart::Kind::Scatter; }
            bool pie() const { return c_.kind == chart::Kind::Pie; }

            std::string fill(const ir::Color& value) const { return "<a:solidFill>" + color_(value) + "</a:solidFill>"; }

            // 계열 i의 색. 정하지 않았으면 테마의 강조색을 차례로 쓴다
            std::string series_color(std::size_t index, const std::optional<ir::Color>& chosen) const {
                return fill(chosen.value_or(ir::Color{0, 0, 0, 1, "accent" + std::to_string(index % 6 + 1)}));
            }

            long size_hundredths(double px) const { return std::lround(px * 0.75 * 100); }

            std::string text_properties(double scale = 1, bool bold = false, const std::string& attributes = "") const {
                return "<c:txPr><a:bodyPr" + attributes + "/><a:lstStyle/><a:p><a:pPr><a:defRPr sz=\"" + std::to_string(size_hundredths(c_.font_px * scale)) + "\" b=\"" + (bold ? "1" : "0") + "\">"
                    + fill(c_.text_color) + "</a:defRPr></a:pPr><a:endParaRPr lang=\"en-US\"/></a:p></c:txPr>";
            }

            std::string title(const std::string& text, double scale, bool bold, const std::string& body_attributes) const {
                const std::string size = std::to_string(size_hundredths(c_.font_px * scale));
                const std::string properties = " sz=\"" + size + "\" b=\"" + (bold ? "1" : "0") + "\"";
                return "<c:title><c:tx><c:rich><a:bodyPr" + body_attributes + "/><a:lstStyle/><a:p><a:pPr><a:defRPr" + properties + ">" + fill(c_.text_color) + "</a:defRPr></a:pPr><a:r><a:rPr lang=\"en-US\""
                    + properties + ">" + fill(c_.text_color) + "</a:rPr><a:t>" + escape(text) + "</a:t></a:r></a:p></c:rich></c:tx>" + val("overlay", false) + "</c:title>";
            }

            std::string data_labels() const {
                if (!c_.data_labels) {
                    return "";
                }
                return "<c:dLbls><c:spPr><a:noFill/><a:ln><a:noFill/></a:ln></c:spPr>" + text_properties() + val("showLegendKey", false) + val("showVal", true) + val("showCatName", false)
                    + val("showSerName", false) + val("showPercent", false) + val("showBubbleSize", false) + "</c:dLbls>";
            }

            std::string name_reference(std::size_t series_index) const {
                const std::size_t column = scatter() ? series_index * 2 + 1 : series_index + 1;
                return "<c:tx>" + string_reference(range(column, 1, 1), {c_.series[series_index].name}) + "</c:tx>";
            }

            std::string category_reference() const {
                return "<c:cat>" + string_reference(range(0, 2, c_.categories.size() + 1), c_.categories) + "</c:cat>";
            }

            std::string value_reference(std::size_t series_index) const {
                const auto& values = c_.series[series_index].values;
                const std::size_t column = scatter() ? series_index * 2 + 1 : series_index + 1;
                return number_reference(range(column, 2, values.size() + 1), values);
            }

            std::string series_xml(std::size_t i) const {
                const auto& series = c_.series[i];
                const std::string head = val("idx", std::to_string(i)) + val("order", std::to_string(i)) + name_reference(i);
                switch (c_.kind) {
                    case chart::Kind::Column:
                    case chart::Kind::Bar:
                        return "<c:ser>" + head + "<c:spPr>" + series_color(i, series.color) + "</c:spPr>" + val("invertIfNegative", false) + data_labels() + category_reference()
                            + "<c:val>" + value_reference(i) + "</c:val></c:ser>";
                    case chart::Kind::Line: {
                        const std::string marker = c_.markers
                            ? "<c:marker><c:symbol val=\"circle\"/><c:size val=\"7\"/><c:spPr>" + series_color(i, series.color) + "<a:ln w=\"9525\"><a:solidFill><a:srgbClr val=\"FFFFFF\"/></a:solidFill></a:ln></c:spPr></c:marker>"
                            : "<c:marker><c:symbol val=\"none\"/></c:marker>";
                        return "<c:ser>" + head + "<c:spPr><a:ln w=\"28575\" cap=\"rnd\">" + series_color(i, series.color) + "<a:round/></a:ln></c:spPr>" + marker + data_labels() + category_reference()
                            + "<c:val>" + value_reference(i) + "</c:val>" + val("smooth", c_.smooth) + "</c:ser>";
                    }
                    case chart::Kind::Area:
                        return "<c:ser>" + head + "<c:spPr>" + series_color(i, series.color) + "</c:spPr>" + data_labels() + category_reference() + "<c:val>" + value_reference(i) + "</c:val></c:ser>";
                    case chart::Kind::Pie: {
                        std::string points;
                        for (std::size_t k = 0; k < c_.categories.size(); ++k) {
                            points += "<c:dPt>" + val("idx", std::to_string(k)) + val("bubble3D", false) + "<c:spPr>" + series_color(k, c_.color_of(k)) + "<a:ln w=\"12700\"><a:solidFill><a:srgbClr val=\"FFFFFF\"/></a:solidFill></a:ln></c:spPr></c:dPt>";
                        }
                        return "<c:ser>" + head + points + data_labels() + category_reference() + "<c:val>" + value_reference(i) + "</c:val></c:ser>";
                    }
                    case chart::Kind::Scatter: {
                        const std::size_t x_column = i * 2;
                        const std::string line = c_.smooth ? "<a:ln w=\"28575\" cap=\"rnd\">" + series_color(i, series.color) + "<a:round/></a:ln>" : "<a:ln w=\"19050\"><a:noFill/></a:ln>";
                        return "<c:ser>" + head + "<c:spPr>" + line + "</c:spPr><c:marker><c:symbol val=\"circle\"/><c:size val=\"7\"/><c:spPr>" + series_color(i, series.color)
                            + "<a:ln w=\"9525\"><a:solidFill><a:srgbClr val=\"FFFFFF\"/></a:solidFill></a:ln></c:spPr></c:marker>" + data_labels()
                            + "<c:xVal>" + number_reference(range(x_column, 2, series.xs.size() + 1), series.xs) + "</c:xVal><c:yVal>" + value_reference(i) + "</c:yVal>" + val("smooth", c_.smooth) + "</c:ser>";
                    }
                }
                return "";
            }

            std::string axis_line() const {
                return "<c:spPr><a:ln w=\"9525\"><a:solidFill><a:srgbClr val=\"7F7F7F\"/></a:solidFill></a:ln></c:spPr>";
            }

            std::string gridlines() const {
                return c_.gridlines ? "<c:majorGridlines><c:spPr><a:ln w=\"9525\"><a:solidFill><a:srgbClr val=\"D9D9D9\"/></a:solidFill></a:ln></c:spPr></c:majorGridlines>" : "";
            }

            std::string scaling() const {
                return "<c:scaling><c:orientation val=\"minMax\"/>" + (c_.max ? val("max", number_text(*c_.max)) : "") + (c_.min ? val("min", number_text(*c_.min)) : "") + "</c:scaling>";
            }

            std::string category_axis(const std::string& position) const {
                return "<c:catAx>" + val("axId", "111") + "<c:scaling><c:orientation val=\"minMax\"/></c:scaling>" + val("delete", false) + val("axPos", position)
                    + (c_.x_title.empty() ? "" : title(c_.x_title, 1, false, position == "l" ? " rot=\"-5400000\" vert=\"horz\"" : "")) + "<c:numFmt formatCode=\"General\" sourceLinked=\"1\"/>"
                    + val("majorTickMark", "none") + val("minorTickMark", "none") + val("tickLblPos", "low") + axis_line() + "<c:crossAx val=\"222\"/>" + val("crosses", "autoZero")
                    + val("auto", true) + val("lblAlgn", "ctr") + val("lblOffset", "100") + val("noMultiLvlLbl", false) + "</c:catAx>";
            }

            // 값 축. 산점도는 x축도 값 축이다 (id가 다르다)
            std::string value_axis(const std::string& id, const std::string& cross, const std::string& position, const std::string& heading, bool grid, bool numeric_x) const {
                const std::string between = c_.kind == chart::Kind::Area || scatter() ? "midCat" : "between";
                const std::string rotation = position == "l" ? " rot=\"-5400000\" vert=\"horz\"" : "";
                return "<c:valAx>" + val("axId", id) + (numeric_x ? "<c:scaling><c:orientation val=\"minMax\"/></c:scaling>" : scaling()) + val("delete", false) + val("axPos", position)
                    + (grid ? gridlines() : "") + (heading.empty() ? "" : title(heading, 1, false, rotation)) + "<c:numFmt formatCode=\"General\" sourceLinked=\"1\"/>" + val("majorTickMark", "none")
                    + val("minorTickMark", "none") + val("tickLblPos", "nextTo") + axis_line() + "<c:crossAx val=\"" + cross + "\"/>" + val("crosses", "autoZero") + val("crossBetween", between) + "</c:valAx>";
            }

            std::string plot_xml() const {
                std::string series;
                for (std::size_t i = 0; i < c_.series.size(); ++i) {
                    if (pie() && i > 0) {
                        break; // 원은 첫 계열만 그린다
                    }
                    series += series_xml(i);
                }
                const std::string axis_ids = "<c:axId val=\"111\"/><c:axId val=\"222\"/>";
                switch (c_.kind) {
                    case chart::Kind::Column:
                    case chart::Kind::Bar: {
                        const bool horizontal = c_.kind == chart::Kind::Bar;
                        return "<c:barChart>" + val("barDir", horizontal ? "bar" : "col") + val("grouping", c_.stacked ? "stacked" : "clustered") + val("varyColors", false) + series
                            + val("gapWidth", "150") + (c_.stacked ? val("overlap", "100") : "") + axis_ids + "</c:barChart>" + category_axis(horizontal ? "l" : "b")
                            + value_axis("222", "111", horizontal ? "b" : "l", c_.y_title, true, false);
                    }
                    case chart::Kind::Line:
                        return "<c:lineChart>" + val("grouping", c_.stacked ? "stacked" : "standard") + val("varyColors", false) + series + val("marker", true) + axis_ids + "</c:lineChart>"
                            + category_axis("b") + value_axis("222", "111", "l", c_.y_title, true, false);
                    case chart::Kind::Area:
                        return "<c:areaChart>" + val("grouping", c_.stacked ? "stacked" : "standard") + val("varyColors", false) + series + axis_ids + "</c:areaChart>" + category_axis("b")
                            + value_axis("222", "111", "l", c_.y_title, true, false);
                    case chart::Kind::Pie:
                        return "<c:pieChart>" + val("varyColors", true) + series + val("firstSliceAng", "0") + "</c:pieChart>";
                    case chart::Kind::Scatter:
                        return "<c:scatterChart>" + val("scatterStyle", "lineMarker") + val("varyColors", false) + series + axis_ids + "</c:scatterChart>"
                            + value_axis("111", "222", "b", c_.x_title, c_.gridlines, true) + value_axis("222", "111", "l", c_.y_title, true, false);
                }
                return "";
            }

            std::string legend_xml() const {
                if (c_.legend == "none") {
                    return "";
                }
                const std::string position = c_.legend == "top" ? "t" : c_.legend == "left" ? "l" : c_.legend == "right" ? "r" : "b";
                return "<c:legend>" + val("legendPos", position) + val("overlay", false) + text_properties() + "</c:legend>";
            }

            std::string chart_xml(bool with_data) const {
                return declaration + "<c:chartSpace xmlns:c=\"http://schemas.openxmlformats.org/drawingml/2006/chart\" xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" "
                    "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">" + val("roundedCorners", false) + "<c:chart>"
                    + (c_.title.empty() ? val("autoTitleDeleted", true) : title(c_.title, 1.3, true, "") + val("autoTitleDeleted", false)) + "<c:plotArea><c:layout/>" + plot_xml() + "</c:plotArea>" + legend_xml()
                    + val("plotVisOnly", true) + val("dispBlanksAs", "gap") + "</c:chart><c:spPr><a:noFill/><a:ln><a:noFill/></a:ln></c:spPr>" + text_properties()
                    + (with_data ? "<c:externalData r:id=\"rId1\"><c:autoUpdate val=\"0\"/></c:externalData>" : "") + "</c:chartSpace>";
            }

            // 통합 문서의 칸. 첫 행은 이름이고 첫 열은 범주다 (산점도는 계열마다 x, y 두 열)
            std::vector<std::vector<xlsx::Cell>> rows() const {
                std::vector<std::vector<xlsx::Cell>> result;
                if (scatter()) {
                    std::size_t longest = 0;
                    std::vector<xlsx::Cell> header;
                    for (const auto& series : c_.series) {
                        header.emplace_back(std::string("X"));
                        header.emplace_back(series.name);
                        longest = std::max(longest, series.values.size());
                    }
                    result.push_back(header);
                    for (std::size_t r = 0; r < longest; ++r) {
                        std::vector<xlsx::Cell> row;
                        for (const auto& series : c_.series) {
                            row.emplace_back(r < series.xs.size() ? xlsx::Cell(series.xs[r]) : xlsx::Cell());
                            row.emplace_back(r < series.values.size() ? xlsx::Cell(series.values[r]) : xlsx::Cell());
                        }
                        result.push_back(row);
                    }
                    return result;
                }
                std::vector<xlsx::Cell> header{xlsx::Cell()};
                for (const auto& series : c_.series) {
                    header.emplace_back(series.name);
                }
                result.push_back(header);
                for (std::size_t r = 0; r < c_.categories.size(); ++r) {
                    std::vector<xlsx::Cell> row{xlsx::Cell(c_.categories[r])};
                    for (const auto& series : c_.series) {
                        row.emplace_back(r < series.values.size() ? xlsx::Cell(series.values[r]) : xlsx::Cell());
                    }
                    result.push_back(row);
                }
                return result;
            }
        };
    }

    Parts build(const chart::Chart& chart, const std::function<std::string(const ir::Color&)>& color) {
        return Builder(chart, color).build();
    }
}
