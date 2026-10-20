#pragma once

#include <string>
#include <string_view>
#include <vector>

/**
 * math object의 LaTeX 수식. 자주 쓰는 부분집합을 읽어 문법 오류를 찾고, PowerPoint 수식(OMML)으로 바꿀 수 있는지 알아보고, OMML을 만든다.
 * html, pdf, png는 KaTeX가 LaTeX를 직접 읽으므로 이 파서는 오류 검사에만 쓴다.
 */
namespace templide::latex {
    struct Node;
    using Nodes = std::vector<Node>;

    struct Node {
        enum class Kind {
            Text,      // 글자 하나 이상. text와 style, italic
            Frac,      // a / b. flag가 true면 가운데 선이 없다 (binom)
            Sqrt,      // 근호. a는 안, b는 차수(없으면 제곱근)
            Script,    // 첨자. a는 밑, b는 아래 첨자, c는 위 첨자 (없는 것은 비어 있다)
            Delimited, // 괄호. text는 여는 글자, text2는 닫는 글자(비면 없음), a는 안
            NAry,      // 합, 곱, 적분. text는 연산자 글자, b는 아래, c는 위, a는 피연산자, flag는 아래위(true) 또는 옆(false)에 첨자를 두는지
            Accent,    // 글자 위의 부호. text는 부호, a는 안
            Bar,       // 위줄(flag가 true) 또는 밑줄. a는 안
            Matrix,    // 행렬. rows[행][열]
            LimLow,    // lim처럼 아래에 식이 붙는 이름. a는 이름, b는 아래
            Func,      // sin x처럼 함수 이름과 그 뒤의 피연산자. a는 이름(첨자가 붙을 수 있다), b는 피연산자
            Group      // 중괄호로 묶은 것. a는 안
        };

        Kind kind = Kind::Text;
        std::string text;
        std::string text2;
        std::string style;    // Text: ""(자동), p(똑바로), b, i, bi, nor(보통 글자), scr:double-struck 등
        bool italic = false;  // Text: 변수처럼 기울여 쓰는 글자
        bool flag = false;
        Nodes a;
        Nodes b;
        Nodes c;
        std::vector<std::vector<Nodes>> rows;
    };

    struct Result {
        Nodes nodes;
        std::string error;       // 문법 오류. 비어 있으면 읽을 수 있다
        std::string unsupported; // PowerPoint 수식으로 쓸 수 없는 첫 명령(예: \xrightarrow). 없으면 비어 있다
    };

    Result parse(std::string_view source);

    /** OMML 글자 모양. size는 100분의 1pt, color는 <a:srgbClr .../> 같은 글(비어 있으면 색을 정하지 않는다). */
    struct Style {
        int size = 3200;
        std::string color;
    };

    /** nodes를 <m:oMath> 안에 들어갈 OMML로 바꾼다. */
    std::string to_omml(const Nodes& nodes, const Style& style);
}
