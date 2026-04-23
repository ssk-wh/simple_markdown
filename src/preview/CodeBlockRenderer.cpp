#include "CodeBlockRenderer.h"

CodeBlockRenderer::CodeBlockRenderer() = default;
CodeBlockRenderer::~CodeBlockRenderer() = default;

QColor CodeBlockRenderer::colorForToken(TokenType type, bool isDark)
{
    if (isDark) {
        switch (type) {
        case Keyword:      return QColor("#569CD6");
        case String:       return QColor("#CE9178");
        case Comment:      return QColor("#6A9955");
        case Number:       return QColor("#B5CEA8");
        case Type:         return QColor("#4EC9B0");
        case Preprocessor: return QColor("#C586C0");
        default:           return QColor("#D4D4D4");
        }
    } else {
        switch (type) {
        case Keyword:      return QColor("#0000FF");
        case String:       return QColor("#A31515");
        case Comment:      return QColor("#008000");
        case Number:       return QColor("#098658");
        case Type:         return QColor("#267F99");
        case Preprocessor: return QColor("#AF00DB");
        default:           return QColor("#333333");
        }
    }
}

static QString buildWordPattern(const QStringList& words)
{
    return "\\b(" + words.join("|") + ")\\b";
}

CodeBlockRenderer::LangDef CodeBlockRenderer::getLangDef(const QString& language)
{
    LangDef def;
    QString lang = language.toLower().trimmed();

    if (lang == "c" || lang == "cpp" || lang == "c++" || lang == "cxx" || lang == "h" || lang == "hpp") {
        def.keywords.setPattern(buildWordPattern({
            "auto","break","case","catch","class","const","constexpr","continue",
            "default","delete","do","else","enum","explicit","extern","false",
            "for","friend","goto","if","inline","namespace","new","nullptr",
            "operator","override","private","protected","public","return",
            "sizeof","static","static_cast","struct","switch","template",
            "this","throw","true","try","typedef","typename","union",
            "using","virtual","void","volatile","while","final","noexcept"}));
        def.types.setPattern(buildWordPattern({
            "int","long","short","char","float","double","bool","unsigned",
            "signed","size_t","string","vector","map","set","unique_ptr",
            "shared_ptr","QString","QVector","QMap","QList","qreal",
            "int8_t","int16_t","int32_t","int64_t","uint8_t","uint16_t","uint32_t","uint64_t"}));
        def.singleComment.setPattern("//.*$");
        def.blockCommentStart = "/*";
        def.blockCommentEnd = "*/";
        def.hasPreprocessor = true;

    } else if (lang == "python" || lang == "py") {
        def.keywords.setPattern(buildWordPattern({
            "and","as","assert","async","await","break","class","continue",
            "def","del","elif","else","except","finally","for","from",
            "global","if","import","in","is","lambda","nonlocal","not",
            "or","pass","raise","return","try","while","with","yield",
            "True","False","None"}));
        def.types.setPattern(buildWordPattern({
            "int","float","str","list","dict","set","tuple","bool","bytes",
            "type","object","range","print","len","isinstance","super","self"}));
        def.singleComment.setPattern("#.*$");

    } else if (lang == "javascript" || lang == "js" || lang == "typescript" || lang == "ts"
               || lang == "jsx" || lang == "tsx") {
        def.keywords.setPattern(buildWordPattern({
            "async","await","break","case","catch","class","const","continue",
            "debugger","default","delete","do","else","export","extends",
            "finally","for","from","function","if","import","in","instanceof",
            "let","new","of","return","super","switch","this","throw","try",
            "typeof","var","void","while","with","yield","enum","implements",
            "interface","package","private","protected","public","static",
            "true","false","null","undefined"}));
        def.types.setPattern(buildWordPattern({
            "Array","Boolean","Date","Error","Function","Map","Number",
            "Object","Promise","RegExp","Set","String","Symbol",
            "console","document","window","module","require","process"}));
        def.singleComment.setPattern("//.*$");
        def.blockCommentStart = "/*";
        def.blockCommentEnd = "*/";

    } else if (lang == "java" || lang == "kotlin" || lang == "kt") {
        def.keywords.setPattern(buildWordPattern({
            "abstract","assert","break","case","catch","class","const","continue",
            "default","do","else","enum","extends","final","finally","for",
            "goto","if","implements","import","instanceof","interface","native",
            "new","package","private","protected","public","return","static",
            "strictfp","super","switch","synchronized","this","throw","throws",
            "transient","try","void","volatile","while","true","false","null",
            "var","val","fun","when","object","companion","data","sealed"}));
        def.types.setPattern(buildWordPattern({
            "int","long","short","byte","float","double","char","boolean",
            "String","Integer","Long","Double","Float","Boolean","List","Map",
            "Set","ArrayList","HashMap","Object","Class","Void"}));
        def.singleComment.setPattern("//.*$");
        def.blockCommentStart = "/*";
        def.blockCommentEnd = "*/";

    } else if (lang == "rust" || lang == "rs") {
        def.keywords.setPattern(buildWordPattern({
            "as","async","await","break","const","continue","crate","dyn",
            "else","enum","extern","false","fn","for","if","impl","in",
            "let","loop","match","mod","move","mut","pub","ref","return",
            "self","Self","static","struct","super","trait","true","type",
            "unsafe","use","where","while"}));
        def.types.setPattern(buildWordPattern({
            "i8","i16","i32","i64","i128","isize","u8","u16","u32","u64",
            "u128","usize","f32","f64","bool","char","str","String",
            "Vec","Box","Option","Result","Some","None","Ok","Err"}));
        def.singleComment.setPattern("//.*$");
        def.blockCommentStart = "/*";
        def.blockCommentEnd = "*/";

    } else if (lang == "go" || lang == "golang") {
        def.keywords.setPattern(buildWordPattern({
            "break","case","chan","const","continue","default","defer",
            "else","fallthrough","for","func","go","goto","if","import",
            "interface","map","package","range","return","select","struct",
            "switch","type","var","true","false","nil"}));
        def.types.setPattern(buildWordPattern({
            "bool","byte","complex64","complex128","error","float32","float64",
            "int","int8","int16","int32","int64","rune","string","uint",
            "uint8","uint16","uint32","uint64","uintptr","fmt","make","len",
            "cap","append","copy","delete","new","panic","recover","close"}));
        def.singleComment.setPattern("//.*$");
        def.blockCommentStart = "/*";
        def.blockCommentEnd = "*/";

    } else if (lang == "bash" || lang == "sh" || lang == "shell" || lang == "zsh") {
        def.keywords.setPattern(buildWordPattern({
            "if","then","else","elif","fi","for","while","do","done",
            "case","esac","in","function","return","exit","local","export",
            "source","set","unset","readonly","shift","true","false"}));
        def.types.setPattern(buildWordPattern({
            "echo","cd","ls","rm","cp","mv","mkdir","cat","grep","sed",
            "awk","find","xargs","chmod","chown","curl","wget","tar",
            "git","docker","npm","pip","python","make","cmake"}));
        def.singleComment.setPattern("#.*$");

    } else if (lang == "json") {
        // JSON：仅有字符串、数字和关键字（true/false/null）
        def.keywords.setPattern(buildWordPattern({"true","false","null"}));

    } else if (lang == "sql") {
        def.keywords = QRegularExpression(buildWordPattern({
            "SELECT","FROM","WHERE","INSERT","INTO","VALUES","UPDATE","SET",
            "DELETE","CREATE","TABLE","ALTER","DROP","INDEX","JOIN","LEFT",
            "RIGHT","INNER","OUTER","ON","AND","OR","NOT","NULL","IS",
            "IN","LIKE","BETWEEN","ORDER","BY","GROUP","HAVING","LIMIT",
            "OFFSET","AS","DISTINCT","COUNT","SUM","AVG","MAX","MIN",
            "UNION","ALL","EXISTS","CASE","WHEN","THEN","ELSE","END",
            "PRIMARY","KEY","FOREIGN","REFERENCES","CONSTRAINT","DEFAULT"}),
            QRegularExpression::CaseInsensitiveOption);
        def.singleComment.setPattern("--.*$");
        def.blockCommentStart = "/*";
        def.blockCommentEnd = "*/";

    } else if (lang == "css" || lang == "scss" || lang == "less") {
        def.keywords.setPattern(buildWordPattern({
            "important","inherit","initial","unset","none","auto","block",
            "inline","flex","grid","absolute","relative","fixed","sticky",
            "solid","dashed","dotted","hidden","visible","scroll","wrap",
            "nowrap","center","left","right","top","bottom"}));
        def.singleComment.setPattern("//.*$");
        def.blockCommentStart = "/*";
        def.blockCommentEnd = "*/";

    } else if (lang == "html" || lang == "xml" || lang == "svg") {
        // 最小化实现：仅处理 < > 标签和属性
        def.blockCommentStart = "<!--";
        def.blockCommentEnd = "-->";

    } else if (lang == "cmake") {
        def.keywords.setPattern(buildWordPattern({
            "if","else","elseif","endif","foreach","endforeach","while",
            "endwhile","function","endfunction","macro","endmacro",
            "return","break","continue","set","unset","option","message",
            "add_executable","add_library","target_link_libraries",
            "target_include_directories","target_compile_definitions",
            "find_package","include","install","project","cmake_minimum_required",
            "add_subdirectory","file","string","list","math","get_filename_component",
            "REQUIRED","COMPONENTS","PRIVATE","PUBLIC","INTERFACE",
            "STATIC","SHARED","MODULE","IMPORTED","ALIAS"}));
        def.singleComment.setPattern("#.*$");
    }

    return def;
}

CodeBlockRenderer::HighlightedLine
CodeBlockRenderer::tokenizeLine(const QString& line, const LangDef& def, bool isDark, bool& inBlockComment)
{
    HighlightedLine result;

    if (line.isEmpty()) {
        result.push_back({QString(), colorForToken(Default, isDark)});
        return result;
    }

    // 记录每个字符已分配的 token 类型
    std::vector<int> tokens(line.length(), (int)Default);

    // 1. 优先处理块注释
    if (!def.blockCommentStart.isEmpty()) {
        int pos = 0;
        while (pos < line.length()) {
            if (inBlockComment) {
                int endIdx = line.indexOf(def.blockCommentEnd, pos);
                if (endIdx >= 0) {
                    int endPos = endIdx + def.blockCommentEnd.length();
                    for (int i = pos; i < endPos && i < line.length(); ++i)
                        tokens[i] = (int)Comment;
                    pos = endPos;
                    inBlockComment = false;
                } else {
                    for (int i = pos; i < line.length(); ++i)
                        tokens[i] = (int)Comment;
                    break;
                }
            } else {
                int startIdx = line.indexOf(def.blockCommentStart, pos);
                if (startIdx >= 0) {
                    inBlockComment = true;
                    for (int i = startIdx; i < startIdx + def.blockCommentStart.length() && i < line.length(); ++i)
                        tokens[i] = (int)Comment;
                    pos = startIdx + def.blockCommentStart.length();
                } else {
                    break;
                }
            }
        }
    }

    // 如果整行都在块注释中，直接输出并返回
    bool allComment = true;
    for (int i = 0; i < tokens.size(); ++i)
        if (tokens[i] != (int)Comment) { allComment = false; break; }
    if (allComment && !tokens.empty()) {
        result.push_back({line, colorForToken(Comment, isDark)});
        return result;
    }

    // 2. 单行注释
    if (def.singleComment.isValid() && !def.singleComment.pattern().isEmpty()) {
        auto m = def.singleComment.match(line);
        if (m.hasMatch()) {
            int start = m.capturedStart();
            // 仅在不处于块注释中时应用
            bool inBC = false;
            for (int i = 0; i < start; ++i)
                if (tokens[i] == (int)Comment) { inBC = true; break; }
            if (!inBC) {
                for (int i = start; i < line.length(); ++i)
                    tokens[i] = (int)Comment;
            }
        }
    }

    // 3. 字符串（双引号和单引号）- 跳过已标记为注释的区域
    {
        static QRegularExpression stringRx(R"("(?:[^"\\]|\\.)*"|'(?:[^'\\]|\\.)*')");
        auto it = stringRx.globalMatch(line);
        while (it.hasNext()) {
            auto m = it.next();
            int start = m.capturedStart();
            int end = start + m.capturedLength();
            bool skip = false;
            if (start < tokens.size() && tokens[start] == Comment) skip = true;
            if (!skip) {
                for (int i = start; i < end && i < tokens.size(); ++i)
                    tokens[i] = (int)String;
            }
        }
    }

    // 4. 预处理指令（C/C++）
    if (def.hasPreprocessor) {
        static QRegularExpression ppRx(R"(^\s*#\w+)");
        auto m = ppRx.match(line);
        if (m.hasMatch() && tokens[m.capturedStart()] == Default) {
            for (int i = m.capturedStart(); i < m.capturedStart() + m.capturedLength(); ++i)
                tokens[i] = (int)Preprocessor;
        }
    }

    // 5. 数字
    {
        static QRegularExpression numRx(R"(\b\d+\.?\d*[fFuUlL]?\b|0x[0-9a-fA-F]+\b)");
        auto it = numRx.globalMatch(line);
        while (it.hasNext()) {
            auto m = it.next();
            int start = m.capturedStart();
            if (start < tokens.size() && tokens[start] == (int)Default) {
                for (int i = start; i < start + m.capturedLength() && i < tokens.size(); ++i)
                    tokens[i] = (int)Number;
            }
        }
    }

    // 6. 关键字
    if (def.keywords.isValid() && !def.keywords.pattern().isEmpty()) {
        auto it = def.keywords.globalMatch(line);
        while (it.hasNext()) {
            auto m = it.next();
            int start = m.capturedStart();
            if (start < tokens.size() && tokens[start] == (int)Default) {
                for (int i = start; i < start + m.capturedLength() && i < tokens.size(); ++i)
                    tokens[i] = (int)Keyword;
            }
        }
    }

    // 7. 类型名
    if (def.types.isValid() && !def.types.pattern().isEmpty()) {
        auto it = def.types.globalMatch(line);
        while (it.hasNext()) {
            auto m = it.next();
            int start = m.capturedStart();
            if (start < tokens.size() && tokens[start] == (int)Default) {
                for (int i = start; i < start + m.capturedLength() && i < tokens.size(); ++i)
                    tokens[i] = (int)Type;
            }
        }
    }

    // 根据 token 数组构建段落
    if (tokens.empty()) {
        result.push_back({line, colorForToken(Default, isDark)});
        return result;
    }

    TokenType currentType = (TokenType)tokens[0];
    int segStart = 0;
    for (int i = 1; i <= tokens.size(); ++i) {
        TokenType t = (i < tokens.size()) ? (TokenType)tokens[i] : Default;
        if (t != currentType || i == tokens.size()) {
            HighlightedSegment seg;
            seg.text = line.mid(segStart, i - segStart);
            seg.color = colorForToken(currentType, isDark);
            seg.bold = (currentType == Keyword);
            result.push_back(std::move(seg));
            currentType = t;
            segStart = i;
        }
    }

    return result;
}

std::vector<CodeBlockRenderer::HighlightedLine>
CodeBlockRenderer::highlight(const QString& code, const QString& language, bool isDark)
{
    std::vector<HighlightedLine> result;
    LangDef def = getLangDef(language);
    bool inBlockComment = false;

    const QStringList lines = code.split('\n');
    for (const auto& line : lines) {
        result.push_back(tokenizeLine(line, def, isDark, inBlockComment));
    }

    return result;
}
