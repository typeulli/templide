#include "xlsx.h"

#include <miniz.h>

#include <cstdio>

namespace templide::backend::xlsx {
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
    }

    std::string column_name(std::size_t column) {
        std::string name;
        for (std::size_t n = column + 1; n > 0; n = (n - 1) / 26) {
            name.insert(name.begin(), static_cast<char>('A' + (n - 1) % 26));
        }
        return name;
    }

    std::optional<std::string> workbook(const std::vector<std::vector<Cell>>& rows) {
        std::string sheet = declaration + "<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\"><sheetData>";
        for (std::size_t r = 0; r < rows.size(); ++r) {
            sheet += "<row r=\"" + std::to_string(r + 1) + "\">";
            for (std::size_t c = 0; c < rows[r].size(); ++c) {
                const std::string reference = column_name(c) + std::to_string(r + 1);
                if (const auto* number = std::get_if<double>(&rows[r][c])) {
                    sheet += "<c r=\"" + reference + "\"><v>" + number_text(*number) + "</v></c>";
                } else if (const auto* text = std::get_if<std::string>(&rows[r][c])) {
                    sheet += "<c r=\"" + reference + "\" t=\"inlineStr\"><is><t xml:space=\"preserve\">" + escape(*text) + "</t></is></c>";
                }
            }
            sheet += "</row>";
        }
        sheet += "</sheetData></worksheet>";

        const std::string main_namespace = "http://schemas.openxmlformats.org/spreadsheetml/2006/main";
        const std::string relationship = "http://schemas.openxmlformats.org/officeDocument/2006/relationships";
        const std::vector<std::pair<std::string, std::string>> parts = {
            {"[Content_Types].xml", declaration + "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
                "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
                "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
                "<Override PartName=\"/xl/workbook.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml\"/>"
                "<Override PartName=\"/xl/worksheets/sheet1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml\"/>"
                "<Override PartName=\"/xl/styles.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml\"/></Types>"},
            {"_rels/.rels", declaration + "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
                "<Relationship Id=\"rId1\" Type=\"" + relationship + "/officeDocument\" Target=\"xl/workbook.xml\"/></Relationships>"},
            {"xl/workbook.xml", declaration + "<workbook xmlns=\"" + main_namespace + "\" xmlns:r=\"" + relationship + "\"><sheets><sheet name=\"Sheet1\" sheetId=\"1\" r:id=\"rId1\"/></sheets></workbook>"},
            {"xl/_rels/workbook.xml.rels", declaration + "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
                "<Relationship Id=\"rId1\" Type=\"" + relationship + "/worksheet\" Target=\"worksheets/sheet1.xml\"/>"
                "<Relationship Id=\"rId2\" Type=\"" + relationship + "/styles\" Target=\"styles.xml\"/></Relationships>"},
            {"xl/styles.xml", declaration + "<styleSheet xmlns=\"" + main_namespace + "\">"
                "<fonts count=\"1\"><font><sz val=\"11\"/><name val=\"Calibri\"/></font></fonts>"
                "<fills count=\"2\"><fill><patternFill patternType=\"none\"/></fill><fill><patternFill patternType=\"gray125\"/></fill></fills>"
                "<borders count=\"1\"><border><left/><right/><top/><bottom/><diagonal/></border></borders>"
                "<cellStyleXfs count=\"1\"><xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\"/></cellStyleXfs>"
                "<cellXfs count=\"1\"><xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\" xfId=\"0\"/></cellXfs>"
                "<cellStyles count=\"1\"><cellStyle name=\"Normal\" xfId=\"0\" builtinId=\"0\"/></cellStyles></styleSheet>"},
            {"xl/worksheets/sheet1.xml", sheet},
        };

        mz_zip_archive zip{};
        if (!mz_zip_writer_init_heap(&zip, 0, 0)) {
            return std::nullopt;
        }
        bool ok = true;
        for (const auto& [name, content] : parts) {
            ok = ok && mz_zip_writer_add_mem(&zip, name.c_str(), content.data(), content.size(), MZ_DEFAULT_COMPRESSION) != 0;
        }
        void* buffer = nullptr;
        std::size_t size = 0;
        ok = ok && mz_zip_writer_finalize_heap_archive(&zip, &buffer, &size);
        mz_zip_writer_end(&zip);
        if (!ok) {
            mz_free(buffer);
            return std::nullopt;
        }
        std::string bytes(static_cast<const char*>(buffer), size);
        mz_free(buffer);
        return bytes;
    }
}
