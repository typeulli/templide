#include "latex.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>

namespace templide::latex {
    namespace {
        // ---------- 기호표

        struct Symbol {
            const char* text;
            bool letter; // 변수처럼 기울여 쓰는 글자(그리스 소문자)
        };

        const std::map<std::string, Symbol>& symbols() {
            static const std::map<std::string, Symbol> table = {
                {"alpha", {"α", true}}, {"beta", {"β", true}}, {"gamma", {"γ", true}}, {"delta", {"δ", true}}, {"epsilon", {"ϵ", true}}, {"varepsilon", {"ε", true}},
                {"zeta", {"ζ", true}}, {"eta", {"η", true}}, {"theta", {"θ", true}}, {"vartheta", {"ϑ", true}}, {"iota", {"ι", true}}, {"kappa", {"κ", true}},
                {"lambda", {"λ", true}}, {"mu", {"μ", true}}, {"nu", {"ν", true}}, {"xi", {"ξ", true}}, {"omicron", {"ο", true}}, {"pi", {"π", true}}, {"varpi", {"ϖ", true}},
                {"rho", {"ρ", true}}, {"varrho", {"ϱ", true}}, {"sigma", {"σ", true}}, {"varsigma", {"ς", true}}, {"tau", {"τ", true}}, {"upsilon", {"υ", true}},
                {"phi", {"ϕ", true}}, {"varphi", {"φ", true}}, {"chi", {"χ", true}}, {"psi", {"ψ", true}}, {"omega", {"ω", true}},
                {"Gamma", {"Γ", false}}, {"Delta", {"Δ", false}}, {"Theta", {"Θ", false}}, {"Lambda", {"Λ", false}}, {"Xi", {"Ξ", false}}, {"Pi", {"Π", false}},
                {"Sigma", {"Σ", false}}, {"Upsilon", {"Υ", false}}, {"Phi", {"Φ", false}}, {"Psi", {"Ψ", false}}, {"Omega", {"Ω", false}},
                {"times", {"×", false}}, {"cdot", {"⋅", false}}, {"div", {"÷", false}}, {"pm", {"±", false}}, {"mp", {"∓", false}}, {"ast", {"∗", false}},
                {"star", {"⋆", false}}, {"circ", {"∘", false}}, {"bullet", {"∙", false}}, {"oplus", {"⊕", false}}, {"ominus", {"⊖", false}}, {"otimes", {"⊗", false}},
                {"odot", {"⊙", false}}, {"leq", {"≤", false}}, {"le", {"≤", false}}, {"geq", {"≥", false}}, {"ge", {"≥", false}}, {"neq", {"≠", false}},
                {"ne", {"≠", false}}, {"approx", {"≈", false}}, {"equiv", {"≡", false}}, {"sim", {"∼", false}}, {"simeq", {"≃", false}}, {"cong", {"≅", false}},
                {"propto", {"∝", false}}, {"ll", {"≪", false}}, {"gg", {"≫", false}}, {"in", {"∈", false}}, {"notin", {"∉", false}}, {"ni", {"∋", false}},
                {"subset", {"⊂", false}}, {"supset", {"⊃", false}}, {"subseteq", {"⊆", false}}, {"supseteq", {"⊇", false}}, {"cup", {"∪", false}}, {"cap", {"∩", false}},
                {"setminus", {"∖", false}}, {"emptyset", {"∅", false}}, {"varnothing", {"∅", false}}, {"forall", {"∀", false}}, {"exists", {"∃", false}},
                {"nexists", {"∄", false}}, {"neg", {"¬", false}}, {"lnot", {"¬", false}}, {"land", {"∧", false}}, {"wedge", {"∧", false}}, {"lor", {"∨", false}},
                {"vee", {"∨", false}}, {"to", {"→", false}}, {"rightarrow", {"→", false}}, {"leftarrow", {"←", false}}, {"gets", {"←", false}},
                {"leftrightarrow", {"↔", false}}, {"Rightarrow", {"⇒", false}}, {"Leftarrow", {"⇐", false}}, {"Leftrightarrow", {"⇔", false}}, {"implies", {"⟹", false}},
                {"iff", {"⟺", false}}, {"mapsto", {"↦", false}}, {"uparrow", {"↑", false}}, {"downarrow", {"↓", false}}, {"infty", {"∞", false}}, {"partial", {"∂", false}},
                {"nabla", {"∇", false}}, {"angle", {"∠", false}}, {"perp", {"⊥", false}}, {"parallel", {"∥", false}}, {"mid", {"∣", false}}, {"ldots", {"…", false}},
                {"dots", {"…", false}}, {"cdots", {"⋯", false}}, {"vdots", {"⋮", false}}, {"ddots", {"⋱", false}}, {"prime", {"′", false}}, {"hbar", {"ℏ", false}},
                {"ell", {"ℓ", true}}, {"Re", {"ℜ", false}}, {"Im", {"ℑ", false}}, {"aleph", {"ℵ", false}}, {"therefore", {"∴", false}}, {"because", {"∵", false}},
                {"square", {"□", false}}, {"triangle", {"△", false}}, {"dagger", {"†", false}}, {"ddagger", {"‡", false}}, {"degree", {"°", false}},
                {"langle", {"⟨", false}}, {"rangle", {"⟩", false}}, {"lfloor", {"⌊", false}}, {"rfloor", {"⌋", false}}, {"lceil", {"⌈", false}}, {"rceil", {"⌉", false}},
                {"lbrace", {"{", false}}, {"rbrace", {"}", false}}, {"backslash", {"\\", false}},
            };
            return table;
        }

        // 적분, 합 같은 큰 연산자. flag는 첨자를 아래위에 두는지
        const std::map<std::string, std::pair<const char*, bool>>& big_operators() {
            static const std::map<std::string, std::pair<const char*, bool>> table = {
                {"sum", {"∑", true}}, {"prod", {"∏", true}}, {"coprod", {"∐", true}}, {"int", {"∫", false}}, {"iint", {"∬", false}}, {"iiint", {"∭", false}},
                {"oint", {"∮", false}}, {"bigcup", {"⋃", true}}, {"bigcap", {"⋂", true}}, {"bigoplus", {"⨁", true}}, {"bigotimes", {"⨂", true}},
                {"bigvee", {"⋁", true}}, {"bigwedge", {"⋀", true}},
            };
            return table;
        }

        const std::set<std::string>& function_names() {
            static const std::set<std::string> names = {"sin", "cos", "tan", "cot", "sec", "csc", "arcsin", "arccos", "arctan", "sinh", "cosh", "tanh", "coth",
                                                        "log", "ln", "lg", "exp", "det", "gcd", "deg", "dim", "ker", "hom", "arg", "Pr", "max", "min", "sup", "inf"};
            return names;
        }

        const std::set<std::string>& limit_names() {
            static const std::set<std::string> names = {"lim", "limsup", "liminf", "max", "min", "sup", "inf", "det", "gcd", "Pr"};
            return names;
        }

        const std::map<std::string, const char*>& accents() {
            static const std::map<std::string, const char*> table = {
                {"hat", "\xCC\x82"}, {"widehat", "\xCC\x82"}, {"check", "\xCC\x8C"}, {"tilde", "\xCC\x83"}, {"widetilde", "\xCC\x83"}, {"acute", "\xCC\x81"},
                {"grave", "\xCC\x80"}, {"dot", "\xCC\x87"}, {"ddot", "\xCC\x88"}, {"bar", "\xCC\x84"}, {"vec", "\xE2\x83\x97"}, {"breve", "\xCC\x86"},
            };
            return table;
        }

        // \mathbf 같은 글자 모양 명령 -> OMML의 style
        const std::map<std::string, const char*>& font_commands() {
            static const std::map<std::string, const char*> table = {
                {"mathrm", "p"}, {"mathbf", "b"}, {"mathit", "i"}, {"boldsymbol", "bi"}, {"bm", "bi"}, {"mathbb", "scr:double-struck"}, {"mathcal", "scr:script"},
                {"mathscr", "scr:script"}, {"mathfrak", "scr:fraktur"}, {"mathsf", "scr:sans-serif"}, {"mathtt", "scr:monospace"}, {"operatorname", "p"},
            };
            return table;
        }

        // \left, \right 뒤에 올 수 있는 괄호 글자. 명령은 이름으로, 한 글자는 그대로
        std::string delimiter_text(const std::string& name) {
            static const std::map<std::string, std::string> named = {
                {"langle", "⟨"}, {"rangle", "⟩"}, {"lfloor", "⌊"}, {"rfloor", "⌋"}, {"lceil", "⌈"}, {"rceil", "⌉"}, {"lbrace", "{"}, {"rbrace", "}"},
                {"{", "{"}, {"}", "}"}, {"|", "‖"}, {"backslash", "\\"}, {"vert", "|"}, {"Vert", "‖"},
            };
            if (const auto it = named.find(name); it != named.end()) {
                return it->second;
            }
            return name;
        }

        // ---------- 파서

        class Parser {
        public:
            explicit Parser(std::string_view source) : s_(source) {}

            Result run() {
                Result result;
                result.nodes = row(Stop::End);
                if (error_.empty() && i_ < s_.size()) {
                    fail("Unexpected '" + std::string(1, s_[i_]) + "'");
                }
                result.error = error_;
                result.unsupported = unsupported_;
                return result;
            }

        private:
            enum class Stop { End, Brace, Right, Cell };

            std::string_view s_;
            std::size_t i_ = 0;
            std::string error_;
            std::string unsupported_;

            void fail(const std::string& message) {
                if (error_.empty()) {
                    error_ = message;
                }
            }

            void unsupported(const std::string& what) {
                if (unsupported_.empty()) {
                    unsupported_ = what;
                }
            }

            bool at_end() const { return i_ >= s_.size(); }

            void skip_space() {
                while (i_ < s_.size() && (s_[i_] == ' ' || s_[i_] == '\t' || s_[i_] == '\n' || s_[i_] == '\r')) {
                    ++i_;
                }
            }

            // 지금 자리가 \이름이면 그 이름을 돌려준다 (읽지는 않는다)
            std::string peek_command() const {
                if (i_ >= s_.size() || s_[i_] != '\\') {
                    return "";
                }
                std::size_t j = i_ + 1;
                if (j < s_.size() && std::isalpha(static_cast<unsigned char>(s_[j]))) {
                    std::size_t k = j;
                    while (k < s_.size() && std::isalpha(static_cast<unsigned char>(s_[k]))) {
                        ++k;
                    }
                    return std::string(s_.substr(j, k - j));
                }
                return j < s_.size() ? std::string(1, s_[j]) : "";
            }

            std::string take_command() {
                const std::string name = peek_command();
                i_ += 1 + name.size();
                return name;
            }

            static std::size_t utf8_length(unsigned char lead) {
                return lead < 0x80 ? 1 : lead < 0xE0 ? 2 : lead < 0xF0 ? 3 : 4;
            }

            Node text(const std::string& value, const std::string& style = "", bool italic = false) {
                Node node;
                node.kind = Node::Kind::Text;
                node.text = value;
                node.style = style;
                node.italic = italic;
                return node;
            }

            // 행. stop에 맞는 끝에서 멈춘다 (끝 글자는 읽지 않는다)
            Nodes row(Stop stop) {
                Nodes nodes;
                while (error_.empty()) {
                    skip_space();
                    if (at_end()) {
                        if (stop == Stop::Brace) {
                            fail("Missing '}'");
                        } else if (stop == Stop::Right) {
                            fail("Missing \\right");
                        }
                        break;
                    }
                    const char c = s_[i_];
                    if (c == '}') {
                        if (stop != Stop::Brace) {
                            fail("Unexpected '}'");
                        }
                        break;
                    }
                    if (c == '&' && stop == Stop::Cell) {
                        break;
                    }
                    if (c == '&') {
                        ++i_;
                        unsupported("&");
                        continue;
                    }
                    if (c == '\\') {
                        const std::string name = peek_command();
                        if (name == "right") {
                            if (stop != Stop::Right) {
                                fail("\\right without \\left");
                            }
                            break;
                        }
                        if (name == "end") {
                            if (stop != Stop::Cell) {
                                fail("\\end without \\begin");
                            }
                            break;
                        }
                        if (name == "\\") {
                            if (stop == Stop::Cell) {
                                break;
                            }
                            i_ += 2;
                            unsupported("\\\\");
                            continue;
                        }
                    }
                    nodes.push_back(term());
                }
                return merge(std::move(nodes));
            }

            // 이어진 글자(변수)와 이어진 숫자를 한 run으로 합친다
            Nodes merge(Nodes nodes) {
                Nodes result;
                for (auto& node : nodes) {
                    if (!result.empty() && node.kind == Node::Kind::Text && result.back().kind == Node::Kind::Text && node.style == result.back().style
                        && node.italic == result.back().italic && node.italic && node.text.size() == 1 && result.back().text.size() >= 1 && !result.back().text.empty()
                        && std::isalpha(static_cast<unsigned char>(node.text[0])) && std::isalpha(static_cast<unsigned char>(result.back().text.back()))) {
                        result.back().text += node.text;
                    } else {
                        result.push_back(std::move(node));
                    }
                }
                return result;
            }

            // 항 하나: 낱개와 거기에 붙은 첨자
            Node term() {
                Node atom = atom_with_scripts();
                return atom;
            }

            Node group_of(Nodes inner) {
                Node node;
                node.kind = Node::Kind::Group;
                node.a = std::move(inner);
                return node;
            }

            // { ... } 또는 낱개 하나 (^, _, \frac 같은 인자)
            Nodes argument(const std::string& owner) {
                skip_space();
                if (at_end()) {
                    fail("Missing argument for " + owner);
                    return {};
                }
                if (s_[i_] == '{') {
                    ++i_;
                    Nodes inner = row(Stop::Brace);
                    if (error_.empty()) {
                        ++i_; // '}'
                    }
                    return inner;
                }
                if (s_[i_] == '}' || s_[i_] == '&') {
                    fail("Missing argument for " + owner);
                    return {};
                }
                Nodes one;
                one.push_back(atom());
                return one;
            }

            // 위 첨자와 아래 첨자. 이미 있는 쪽이 다시 오면 오류다
            void scripts(Nodes& below, Nodes& above, bool& any) {
                while (error_.empty()) {
                    skip_space();
                    if (at_end()) {
                        return;
                    }
                    const char c = s_[i_];
                    if (c == '^') {
                        ++i_;
                        if (!above.empty()) {
                            fail("Double superscript");
                            return;
                        }
                        above = argument("^");
                        any = true;
                    } else if (c == '_') {
                        ++i_;
                        if (!below.empty()) {
                            fail("Double subscript");
                            return;
                        }
                        below = argument("_");
                        any = true;
                    } else if (c == '\'') {
                        ++i_;
                        if (above.empty()) {
                            above.push_back(text("′", "p"));
                        } else if (above.size() == 1 && above[0].kind == Node::Kind::Text && above[0].text.rfind("′", 0) == 0) {
                            above[0].text += "′";
                        }
                        any = true;
                    } else {
                        return;
                    }
                }
            }

            Node atom_with_scripts() {
                Node base = atom();
                if (!error_.empty()) {
                    return base;
                }
                // 큰 연산자와 lim은 첨자를 그 연산자에 붙인다
                if (base.kind == Node::Kind::NAry || base.kind == Node::Kind::LimLow || base.kind == Node::Kind::Func) {
                    return base;
                }
                Nodes below;
                Nodes above;
                bool any = false;
                scripts(below, above, any);
                if (!any) {
                    return base;
                }
                Node node;
                node.kind = Node::Kind::Script;
                node.a.push_back(std::move(base));
                node.b = std::move(below);
                node.c = std::move(above);
                return node;
            }

            // 큰 연산자의 피연산자: 바로 뒤의 항 하나 (없으면 비어 있다)
            Nodes operand() {
                skip_space();
                if (at_end() || s_[i_] == '}' || s_[i_] == '&') {
                    return {};
                }
                const std::string name = peek_command();
                if (name == "right" || name == "end" || name == "\\") {
                    return {};
                }
                Nodes one;
                one.push_back(atom_with_scripts());
                return one;
            }

            static void apply_style(Nodes& nodes, const std::string& style) {
                for (auto& node : nodes) {
                    if (node.kind == Node::Kind::Text) {
                        if (node.style.empty()) {
                            node.style = style;
                            node.italic = false;
                        }
                    }
                    apply_style(node.a, style);
                    apply_style(node.b, style);
                    apply_style(node.c, style);
                    for (auto& row : node.rows) {
                        for (auto& cell : row) {
                            apply_style(cell, style);
                        }
                    }
                }
            }

            // { ... }의 안 (글자 그대로). 짝이 맞는 }까지다
            std::string raw_group(const std::string& owner) {
                skip_space();
                if (at_end() || s_[i_] != '{') {
                    fail("Missing argument for " + owner);
                    return "";
                }
                ++i_;
                std::size_t depth = 1;
                std::string result;
                while (!at_end()) {
                    const char c = s_[i_];
                    if (c == '{') {
                        ++depth;
                    } else if (c == '}') {
                        if (--depth == 0) {
                            ++i_;
                            return result;
                        }
                    }
                    result += c;
                    ++i_;
                }
                fail("Missing '}'");
                return result;
            }

            Node atom() {
                skip_space();
                if (at_end()) {
                    fail("Unexpected end");
                    return Node{};
                }
                const char c = s_[i_];
                if (c == '{') {
                    ++i_;
                    Nodes inner = row(Stop::Brace);
                    if (error_.empty()) {
                        ++i_;
                    }
                    return group_of(std::move(inner));
                }
                if (c == '\\') {
                    return command();
                }
                if (std::isalpha(static_cast<unsigned char>(c))) {
                    ++i_;
                    return text(std::string(1, c), "", true);
                }
                if (std::isdigit(static_cast<unsigned char>(c)) || (c == '.' && i_ + 1 < s_.size() && std::isdigit(static_cast<unsigned char>(s_[i_ + 1])))) {
                    std::size_t j = i_;
                    while (j < s_.size() && std::isdigit(static_cast<unsigned char>(s_[j]))) {
                        ++j;
                    }
                    if (j + 1 < s_.size() && s_[j] == '.' && std::isdigit(static_cast<unsigned char>(s_[j + 1]))) {
                        ++j;
                        while (j < s_.size() && std::isdigit(static_cast<unsigned char>(s_[j]))) {
                            ++j;
                        }
                    }
                    const std::string number(s_.substr(i_, j - i_));
                    i_ = j;
                    return text(number, "p");
                }
                if (static_cast<unsigned char>(c) >= 0x80) {
                    const std::size_t length = std::min(utf8_length(static_cast<unsigned char>(c)), s_.size() - i_);
                    const std::string glyph(s_.substr(i_, length));
                    i_ += length;
                    return text(glyph, "nor");
                }
                if (std::string("+-=<>!,;:()[]/|*.").find(c) != std::string::npos) {
                    ++i_;
                    return text(std::string(1, c), "p");
                }
                if (c == '~') {
                    ++i_;
                    return text("\xC2\xA0", "nor");
                }
                if (c == '^' || c == '_') {
                    fail(std::string("Missing base before '") + c + "'");
                    ++i_;
                    return Node{};
                }
                fail("Unexpected '" + std::string(1, c) + "'");
                ++i_;
                return Node{};
            }

            Node command() {
                const std::string name = take_command();
                if (name.empty()) {
                    fail("Dangling '\\'");
                    return Node{};
                }
                // 공백
                static const std::map<std::string, const char*> spaces = {
                    {",", "\xE2\x80\x89"}, {":", "\xE2\x80\x84"}, {";", "\xE2\x80\x85"}, {" ", " "}, {"quad", "\xE2\x80\x83"}, {"qquad", "\xE2\x80\x83\xE2\x80\x83"},
                    {"!", ""}, {"enspace", "\xE2\x80\x82"},
                };
                if (const auto it = spaces.find(name); it != spaces.end()) {
                    return text(it->second, "nor");
                }
                if (name == "{" || name == "}" || name == "%" || name == "#" || name == "$" || name == "&" || name == "_") {
                    return text(name, "p");
                }
                if (name == "|") {
                    return text("‖", "p");
                }
                if (const auto it = symbols().find(name); it != symbols().end()) {
                    return text(it->second.text, it->second.letter ? "" : "p", it->second.letter);
                }
                if (const auto it = big_operators().find(name); it != big_operators().end()) {
                    Node node;
                    node.kind = Node::Kind::NAry;
                    node.text = it->second.first;
                    node.flag = it->second.second;
                    bool any = false;
                    while (error_.empty()) {
                        const std::size_t before = i_;
                        scripts(node.b, node.c, any);
                        const std::string next = peek_command();
                        if (next == "limits" || next == "nolimits") {
                            take_command();
                            node.flag = next == "limits";
                            continue;
                        }
                        if (i_ == before) {
                            break;
                        }
                    }
                    node.a = operand();
                    return node;
                }
                if (limit_names().contains(name)) {
                    skip_space();
                    if (!at_end() && s_[i_] == '_') {
                        Node node;
                        node.kind = Node::Kind::LimLow;
                        node.a.push_back(text(name, "p"));
                        ++i_;
                        node.b = argument("_");
                        return node;
                    }
                    return function_node(name);
                }
                if (function_names().contains(name)) {
                    return function_node(name);
                }
                if (name == "frac" || name == "dfrac" || name == "tfrac" || name == "cfrac") {
                    Node node;
                    node.kind = Node::Kind::Frac;
                    node.a = argument("\\" + name);
                    node.b = argument("\\" + name);
                    return node;
                }
                if (name == "binom" || name == "dbinom" || name == "tbinom") {
                    Node fraction;
                    fraction.kind = Node::Kind::Frac;
                    fraction.flag = true;
                    fraction.a = argument("\\" + name);
                    fraction.b = argument("\\" + name);
                    Node node;
                    node.kind = Node::Kind::Delimited;
                    node.text = "(";
                    node.text2 = ")";
                    node.a.push_back(std::move(fraction));
                    return node;
                }
                if (name == "sqrt") {
                    Node node;
                    node.kind = Node::Kind::Sqrt;
                    skip_space();
                    if (!at_end() && s_[i_] == '[') {
                        ++i_;
                        const std::size_t close = s_.find(']', i_);
                        if (close == std::string_view::npos) {
                            fail("Missing ']'");
                            return node;
                        }
                        Parser inner(s_.substr(i_, close - i_));
                        Result parsed = inner.run();
                        if (!parsed.error.empty()) {
                            fail(parsed.error);
                        }
                        unsupported_ = unsupported_.empty() ? parsed.unsupported : unsupported_;
                        node.b = std::move(parsed.nodes);
                        i_ = close + 1;
                    }
                    node.a = argument("\\sqrt");
                    return node;
                }
                if (name == "text" || name == "textrm" || name == "textbf" || name == "textit" || name == "mbox" || name == "hbox") {
                    const std::string content = raw_group("\\" + name);
                    return text(content, name == "textbf" ? "nor" : "nor");
                }
                if (const auto it = font_commands().find(name); it != font_commands().end()) {
                    Node node = group_of(argument("\\" + name));
                    Nodes wrapper;
                    wrapper.push_back(std::move(node));
                    apply_style(wrapper, it->second);
                    return std::move(wrapper.front());
                }
                if (const auto it = accents().find(name); it != accents().end()) {
                    Node node;
                    node.kind = Node::Kind::Accent;
                    node.text = it->second;
                    node.a = argument("\\" + name);
                    return node;
                }
                if (name == "overline" || name == "underline" || name == "bar") {
                    Node node;
                    node.kind = Node::Kind::Bar;
                    node.flag = name != "underline";
                    node.a = argument("\\" + name);
                    return node;
                }
                if (name == "left") {
                    return left_right();
                }
                if (name == "begin") {
                    return environment();
                }
                if (name == "right" || name == "end") {
                    fail("\\" + name + " is not expected here");
                    return Node{};
                }
                unsupported("\\" + name);
                return text("\\" + name, "nor");
            }

            // sin x처럼 이름 뒤의 항 하나를 피연산자로 묶는다 (\sin^2 x는 첨자가 이름에 붙는다). 피연산자가 없으면 이름만 놓는다
            Node function_node(const std::string& name) {
                Node head = text(name, "p");
                Nodes below;
                Nodes above;
                bool any = false;
                scripts(below, above, any);
                if (any) {
                    Node script;
                    script.kind = Node::Kind::Script;
                    script.a.push_back(std::move(head));
                    script.b = std::move(below);
                    script.c = std::move(above);
                    head = std::move(script);
                }
                Nodes body = operand();
                if (body.empty()) {
                    return head;
                }
                Node node;
                node.kind = Node::Kind::Func;
                node.a.push_back(std::move(head));
                node.b = std::move(body);
                return node;
            }

            // \left( ... \right)
            Node left_right() {
                const auto delimiter = [&](const std::string& owner) -> std::string {
                    skip_space();
                    if (at_end()) {
                        fail("Missing delimiter after " + owner);
                        return "";
                    }
                    if (s_[i_] == '\\') {
                        return delimiter_text(take_command());
                    }
                    const std::size_t length = std::min(utf8_length(static_cast<unsigned char>(s_[i_])), s_.size() - i_);
                    const std::string glyph(s_.substr(i_, length));
                    i_ += length;
                    return glyph == "." ? "" : delimiter_text(glyph);
                };
                Node node;
                node.kind = Node::Kind::Delimited;
                node.text = delimiter("\\left");
                node.a = row(Stop::Right);
                if (!error_.empty()) {
                    return node;
                }
                take_command(); // right
                node.text2 = delimiter("\\right");
                return node;
            }

            // \begin{환경} ... \end{환경}. 행렬과 cases는 OMML로 바꿀 수 있고, 그 밖의 환경은 읽기만 한다
            Node environment() {
                const std::string name = raw_group("\\begin");
                Node matrix;
                matrix.kind = Node::Kind::Matrix;
                matrix.text = name;
                static const std::set<std::string> known = {"matrix", "pmatrix", "bmatrix", "Bmatrix", "vmatrix", "Vmatrix", "cases", "smallmatrix"};
                if (!known.contains(name)) {
                    unsupported("\\begin{" + name + "}");
                }
                while (error_.empty()) {
                    std::vector<Nodes> cells;
                    while (error_.empty()) {
                        cells.push_back(row(Stop::Cell));
                        skip_space();
                        if (!at_end() && s_[i_] == '&') {
                            ++i_;
                            continue;
                        }
                        break;
                    }
                    matrix.rows.push_back(std::move(cells));
                    if (!error_.empty()) {
                        break;
                    }
                    if (peek_command() == "\\") {
                        i_ += 2;
                        continue;
                    }
                    break;
                }
                if (error_.empty()) {
                    if (peek_command() != "end") {
                        fail("Missing \\end{" + name + "}");
                    } else {
                        take_command();
                        const std::string closing = raw_group("\\end");
                        if (error_.empty() && closing != name) {
                            fail("\\begin{" + name + "} is closed by \\end{" + closing + "}");
                        }
                    }
                }
                // 맨 끝의 \\ 뒤에 빈 행이 생기면 뺀다
                while (matrix.rows.size() > 1 && matrix.rows.back().size() == 1 && matrix.rows.back()[0].empty()) {
                    matrix.rows.pop_back();
                }
                return matrix;
            }
        };

        // ---------- OMML

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

        class Writer {
        public:
            explicit Writer(const Style& style) : style_(style) {}

            std::string nodes(const Nodes& list) const {
                std::string xml;
                for (const auto& node : list) {
                    xml += one(node);
                }
                return xml;
            }

        private:
            const Style& style_;

            // m:e 같은 상자. 비어 있어도 상자는 있어야 한다
            std::string box(const std::string& tag, const Nodes& list) const {
                return "<m:" + tag + ">" + nodes(list) + "</m:" + tag + ">";
            }

            std::string run(const std::string& text, const std::string& style, bool italic) const {
                std::string properties;
                if (style == "nor") {
                    properties = "<m:nor/>";
                } else if (style == "p" || style == "b" || style == "i" || style == "bi") {
                    properties = "<m:sty m:val=\"" + style + "\"/>";
                } else if (style.rfind("scr:", 0) == 0) {
                    properties = "<m:scr m:val=\"" + style.substr(4) + "\"/>";
                }
                const bool slanted = italic || style == "i" || style == "bi";
                const bool bold = style == "b" || style == "bi";
                const std::string font = "<a:latin typeface=\"Cambria Math\" panose=\"02040503050406030204\" pitchFamily=\"18\" charset=\"0\"/><a:ea typeface=\"Cambria Math\" panose=\"02040503050406030204\" pitchFamily=\"18\" charset=\"0\"/>"
                                         "<a:cs typeface=\"Cambria Math\" panose=\"02040503050406030204\" pitchFamily=\"18\" charset=\"0\"/>";
                // 보통 글자(\text)는 글꼴을 정하지 않아 한글도 보인다
                const std::string face = style == "nor" ? "" : font;
                const std::string fill = style_.color.empty() ? "" : "<a:solidFill>" + style_.color + "</a:solidFill>";
                const bool preserve = !text.empty() && (text.front() == ' ' || text.back() == ' ');
                return "<m:r>" + (properties.empty() ? "" : "<m:rPr>" + properties + "</m:rPr>") + "<a:rPr lang=\"en-US\" sz=\"" + std::to_string(style_.size) + "\"" + (slanted ? " i=\"1\"" : "")
                    + (bold ? " b=\"1\"" : "") + ">" + fill + face + "</a:rPr><m:t" + (preserve ? " xml:space=\"preserve\"" : "") + ">" + escape(text) + "</m:t></m:r>";
            }

            std::string control(const std::string& properties) const {
                // 수식의 글자 크기는 상자의 속성(m:ctrlPr)에도 적어야 PowerPoint가 괄호와 근호를 그 크기로 그린다
                const std::string fill = style_.color.empty() ? "" : "<a:solidFill>" + style_.color + "</a:solidFill>";
                return "<m:" + properties + "Pr><m:ctrlPr><a:rPr lang=\"en-US\" sz=\"" + std::to_string(style_.size) + "\" i=\"1\">" + fill
                    + "<a:latin typeface=\"Cambria Math\" panose=\"02040503050406030204\" pitchFamily=\"18\" charset=\"0\"/></a:rPr></m:ctrlPr></m:" + properties + "Pr>";
            }

            std::string one(const Node& node) const {
                switch (node.kind) {
                    case Node::Kind::Text:
                        return node.text.empty() ? "" : run(node.text, node.style, node.italic);
                    case Node::Kind::Group:
                        return nodes(node.a);
                    case Node::Kind::Frac:
                        return "<m:f>" + (node.flag ? "<m:fPr><m:type m:val=\"noBar\"/>" + ctrl_inner() + "</m:fPr>" : control("f")) + box("num", node.a) + box("den", node.b) + "</m:f>";
                    case Node::Kind::Sqrt:
                        return "<m:rad><m:radPr>" + std::string(node.b.empty() ? "<m:degHide m:val=\"1\"/>" : "") + ctrl_inner() + "</m:radPr>" + box("deg", node.b) + box("e", node.a) + "</m:rad>";
                    case Node::Kind::Script: {
                        const bool below = !node.b.empty();
                        const bool above = !node.c.empty();
                        const std::string base = box("e", node.a);
                        if (below && above) {
                            return "<m:sSubSup>" + control("sSubSup") + base + box("sub", node.b) + box("sup", node.c) + "</m:sSubSup>";
                        }
                        return above ? "<m:sSup>" + control("sSup") + base + box("sup", node.c) + "</m:sSup>" : "<m:sSub>" + control("sSub") + base + box("sub", node.b) + "</m:sSub>";
                    }
                    case Node::Kind::Delimited:
                        return "<m:d><m:dPr><m:begChr m:val=\"" + escape(node.text) + "\"/><m:endChr m:val=\"" + escape(node.text2) + "\"/>" + ctrl_inner() + "</m:dPr>" + box("e", node.a) + "</m:d>";
                    case Node::Kind::NAry:
                        return "<m:nary><m:naryPr><m:chr m:val=\"" + escape(node.text) + "\"/><m:limLoc m:val=\"" + (node.flag ? "undOvr" : "subSup") + "\"/>" + (node.b.empty() ? "<m:subHide m:val=\"1\"/>" : "")
                            + (node.c.empty() ? "<m:supHide m:val=\"1\"/>" : "") + ctrl_inner() + "</m:naryPr>" + box("sub", node.b) + box("sup", node.c) + box("e", node.a) + "</m:nary>";
                    case Node::Kind::Accent:
                        return "<m:acc><m:accPr><m:chr m:val=\"" + escape(node.text) + "\"/>" + ctrl_inner() + "</m:accPr>" + box("e", node.a) + "</m:acc>";
                    case Node::Kind::Bar:
                        return "<m:bar><m:barPr><m:pos m:val=\"" + std::string(node.flag ? "top" : "bot") + "\"/>" + ctrl_inner() + "</m:barPr>" + box("e", node.a) + "</m:bar>";
                    case Node::Kind::LimLow:
                        return "<m:limLow>" + control("limLow") + box("e", node.a) + box("lim", node.b) + "</m:limLow>";
                    case Node::Kind::Func:
                        return "<m:func>" + control("func") + box("fName", node.a) + box("e", node.b) + "</m:func>";
                    case Node::Kind::Matrix:
                        return matrix(node);
                }
                return "";
            }

            std::string ctrl_inner() const {
                const std::string fill = style_.color.empty() ? "" : "<a:solidFill>" + style_.color + "</a:solidFill>";
                return "<m:ctrlPr><a:rPr lang=\"en-US\" sz=\"" + std::to_string(style_.size) + "\" i=\"1\">" + fill
                    + "<a:latin typeface=\"Cambria Math\" panose=\"02040503050406030204\" pitchFamily=\"18\" charset=\"0\"/></a:rPr></m:ctrlPr>";
            }

            std::string matrix(const Node& node) const {
                std::string open;
                std::string close;
                const std::string& name = node.text;
                if (name == "pmatrix") {
                    open = "(";
                    close = ")";
                } else if (name == "bmatrix") {
                    open = "[";
                    close = "]";
                } else if (name == "Bmatrix") {
                    open = "{";
                    close = "}";
                } else if (name == "vmatrix") {
                    open = "|";
                    close = "|";
                } else if (name == "Vmatrix") {
                    open = "‖";
                    close = "‖";
                } else if (name == "cases") {
                    open = "{";
                }
                std::string body;
                if (name == "cases") {
                    body = "<m:eqArr><m:eqArrPr>" + ctrl_inner() + "</m:eqArrPr>";
                    for (const auto& cells : node.rows) {
                        std::string line;
                        for (std::size_t i = 0; i < cells.size(); ++i) {
                            if (i > 0) {
                                line += run("\xE2\x80\x83", "nor", false);
                            }
                            line += nodes(cells[i]);
                        }
                        body += "<m:e>" + line + "</m:e>";
                    }
                    body += "</m:eqArr>";
                } else {
                    std::size_t columns = 0;
                    for (const auto& cells : node.rows) {
                        columns = std::max(columns, cells.size());
                    }
                    body = "<m:m><m:mPr><m:mcs><m:mc><m:mcPr><m:count m:val=\"" + std::to_string(columns) + "\"/><m:mcJc m:val=\"center\"/></m:mcPr></m:mc></m:mcs>" + ctrl_inner() + "</m:mPr>";
                    for (const auto& cells : node.rows) {
                        body += "<m:mr>";
                        for (std::size_t c = 0; c < columns; ++c) {
                            body += c < cells.size() ? box("e", cells[c]) : "<m:e/>";
                        }
                        body += "</m:mr>";
                    }
                    body += "</m:m>";
                }
                if (open.empty() && close.empty()) {
                    return body;
                }
                return "<m:d><m:dPr><m:begChr m:val=\"" + escape(open) + "\"/><m:endChr m:val=\"" + escape(close) + "\"/>" + ctrl_inner() + "</m:dPr><m:e>" + body + "</m:e></m:d>";
            }
        };
    }

    Result parse(std::string_view source) {
        return Parser(source).run();
    }

    std::string to_omml(const Nodes& nodes, const Style& style) {
        return Writer(style).nodes(nodes);
    }
}
