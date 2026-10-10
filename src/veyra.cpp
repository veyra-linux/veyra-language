#if !defined(__linux__)
#error "VEYRA runtime is Linux-only. Build and run it on Linux."
#endif

#include <openssl/evp.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace veyra {

constexpr std::size_t MAX_SOURCE_BYTES = 4 * 1024 * 1024;
constexpr std::size_t MAX_TOKENS = 250'000;
constexpr std::size_t MAX_PARSE_DEPTH = 256;
constexpr std::size_t MAX_STRING_BYTES = 4 * 1024 * 1024;
constexpr std::size_t MAX_OUTPUT_BYTES = 16 * 1024 * 1024;
constexpr std::uint64_t MAX_LOOP_STEPS = 1'000'000;
constexpr std::uint64_t MAX_EXECUTION_STEPS = 5'000'000;
constexpr std::uint64_t MAX_EVALUATION_STEPS = 20'000'000;
constexpr std::uintmax_t MAX_HASH_BYTES = 256ULL * 1024ULL * 1024ULL;
constexpr std::size_t MAX_CALL_DEPTH = 256;

struct Error : std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct Value {
    using Data = std::variant<std::monostate, double, std::string, bool>;
    Data data;

    Value() : data(std::monostate{}) {}
    Value(double v) : data(v) {}
    Value(int v) : data(static_cast<double>(v)) {}
    Value(std::string v) : data(std::move(v)) {}
    Value(const char* v) : data(std::string(v)) {}
    Value(bool v) : data(v) {}

    std::string type_name() const {
        if (std::holds_alternative<double>(data)) return "number";
        if (std::holds_alternative<std::string>(data)) return "string";
        if (std::holds_alternative<bool>(data)) return "bool";
        return "none";
    }
    std::string str() const {
        if (std::holds_alternative<std::monostate>(data)) return "none";
        if (auto p = std::get_if<double>(&data)) {
            std::ostringstream out;
            if (std::isfinite(*p) && std::floor(*p) == *p && std::fabs(*p) < 9e15)
                out << std::fixed << std::setprecision(0) << *p;
            else out << *p;
            return out.str();
        }
        if (auto p = std::get_if<std::string>(&data)) return *p;
        return std::get<bool>(data) ? "true" : "false";
    }
    bool truthy() const {
        if (std::holds_alternative<std::monostate>(data)) return false;
        if (auto p = std::get_if<bool>(&data)) return *p;
        if (auto p = std::get_if<double>(&data)) return *p != 0.0;
        if (auto p = std::get_if<std::string>(&data)) return !p->empty();
        return false;
    }
    double number() const {
        if (auto p = std::get_if<double>(&data)) return *p;
        throw Error("ожидалось число, получено " + type_name());
    }
};

enum class TokenKind { End, Identifier, Number, String, Symbol };
struct Token {
    TokenKind kind;
    std::string text;
    std::size_t line;
    std::size_t column;
};

class Lexer {
public:
    explicit Lexer(std::string source) : source_(std::move(source)) {}

    std::vector<Token> scan() {
        std::vector<Token> tokens;
        while (!at_end()) {
            skip_space_and_comments();
            if (at_end()) break;
            if (tokens.size() >= MAX_TOKENS)
                throw Error("VEYRA: превышен лимит токенов (250 000)");
            const auto start_line = line_, start_col = column_;
            char c = peek();
            if (is_alpha(c) || c == '_') {
                std::string s;
                while (!at_end() && (is_alpha(peek()) || is_digit(peek()) || peek() == '_')) s += advance();
                tokens.push_back({TokenKind::Identifier, s, start_line, start_col});
            } else if (is_digit(c)) {
                std::string s;
                while (!at_end() && is_digit(peek())) s += advance();
                if (!at_end() && peek() == '.' && is_digit(peek_next())) {
                    s += advance();
                    while (!at_end() && is_digit(peek())) s += advance();
                }
                tokens.push_back({TokenKind::Number, s, start_line, start_col});
            } else if (c == '"' || c == '\'') {
                tokens.push_back(read_string(start_line, start_col, advance()));
            } else {
                std::string two;
                two += c;
                if (!at_end(1)) two += peek_next();
                if (two == "==" || two == "!=" || two == "<=" || two == ">=" || two == "&&" || two == "||") {
                    advance(); advance();
                    tokens.push_back({TokenKind::Symbol, two, start_line, start_col});
                } else if (std::string("{}(),;.+-*/%!<>=").find(c) != std::string::npos) {
                    tokens.push_back({TokenKind::Symbol, std::string(1, advance()), start_line, start_col});
                } else {
                    throw Error(where(start_line, start_col) + "неизвестный символ: '" + std::string(1, c) + "'");
                }
            }
        }
        tokens.push_back({TokenKind::End, "<конец>", line_, column_});
        return tokens;
    }
private:
    std::string source_;
    std::size_t pos_ = 0, line_ = 1, column_ = 1;
    bool at_end(std::size_t ahead = 0) const { return pos_ + ahead >= source_.size(); }
    char peek() const { return at_end() ? '\0' : source_[pos_]; }
    char peek_next() const { return at_end(1) ? '\0' : source_[pos_ + 1]; }
    char advance() {
        char c = source_[pos_++];
        if (c == '\n') { ++line_; column_ = 1; } else ++column_;
        return c;
    }
    static bool is_alpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
    static bool is_digit(char c) { return c >= '0' && c <= '9'; }
    static std::string where(std::size_t l, std::size_t c) {
        return "VEYRA:" + std::to_string(l) + ":" + std::to_string(c) + ": ";
    }
    void skip_space_and_comments() {
        for (;;) {
            while (!at_end() && (peek() == ' ' || peek() == '\r' || peek() == '\t' || peek() == '\n')) advance();
            if (!at_end() && peek() == '#') {
                while (!at_end() && peek() != '\n') advance();
            } else if (!at_end(1) && peek() == '/' && peek_next() == '/') {
                while (!at_end() && peek() != '\n') advance();
            } else break;
        }
    }
    Token read_string(std::size_t l, std::size_t c, char quote) {
        std::string value;
        while (!at_end() && peek() != quote) {
            char ch = advance();
            if (ch == '\n') throw Error(where(l, c) + "строка не может пересекать строку кода; используй \\n");
            if (ch == '\\') {
                if (at_end()) throw Error(where(l, c) + "незавершённая escape-последовательность");
                char e = advance();
                switch (e) {
                    case 'n': value += '\n'; break;
                    case 't': value += '\t'; break;
                    case 'r': value += '\r'; break;
                    case '\\': value += '\\'; break;
                    case '"': value += '"'; break;
                    case '\'': value += '\''; break;
                    default: throw Error(where(line_, column_) + "неизвестная escape-последовательность \\" + std::string(1, e));
                }
            } else value += ch;
        }
        if (at_end()) throw Error(where(l, c) + "незакрытая строка");
        advance();
        return {TokenKind::String, value, l, c};
    }
};

struct Expr;
struct Stmt;
using ExprPtr = std::shared_ptr<Expr>;
using StmtPtr = std::shared_ptr<Stmt>;

struct Expr {
    enum class Kind { Literal, Variable, Unary, Binary, Call } kind;
    Value literal;
    std::string text;
    ExprPtr left, right;
    std::vector<ExprPtr> args;
};
struct Stmt {
    enum class Kind { Let, Const, Assign, Print, If, While, For, Break, Continue, Function, Return, Expression } kind;
    std::string name;
    std::vector<std::string> params;
    ExprPtr expr;
    StmtPtr initializer, increment;
    std::vector<StmtPtr> body, else_body;
};

class Parser {
public:
    explicit Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}
    std::vector<StmtPtr> parse_program() {
        std::vector<StmtPtr> out;
        while (!check_end()) out.push_back(statement());
        return out;
    }
private:
    std::vector<Token> tokens_;
    std::size_t current_ = 0;
    std::size_t nesting_ = 0;
    struct NestingGuard {
        std::size_t& depth;
        explicit NestingGuard(std::size_t& d) : depth(d) {
            if (++depth > MAX_PARSE_DEPTH) { --depth; throw Error("VEYRA: превышена максимальная глубина синтаксиса (256)"); }
        }
        ~NestingGuard() { --depth; }
    };
    const Token& peek() const { return tokens_[current_]; }
    const Token& previous() const { return tokens_[current_ - 1]; }
    bool check_end() const { return peek().kind == TokenKind::End; }
    bool check(const std::string& s) const {
        const bool keyword = s == "let" || s == "const" || s == "print" || s == "if" || s == "else" ||
                             s == "while" || s == "for" || s == "break" || s == "continue" ||
                             s == "fn" || s == "return" || s == "true" ||
                             s == "false" || s == "none";
        return peek().text == s && peek().kind == (keyword ? TokenKind::Identifier : TokenKind::Symbol);
    }
    bool match(const std::string& s) { if (check(s)) { ++current_; return true; } return false; }
    const Token& advance() { if (!check_end()) ++current_; return previous(); }
    const Token& consume(const std::string& s, const std::string& message) {
        if (check(s)) return advance();
        fail(peek(), message + "; найдено '" + peek().text + "'");
    }
    const Token& consume_identifier(const std::string& message) {
        if (peek().kind == TokenKind::Identifier) return advance();
        fail(peek(), message);
    }
    [[noreturn]] static void fail(const Token& t, const std::string& message) {
        throw Error("VEYRA:" + std::to_string(t.line) + ":" + std::to_string(t.column) + ": " + message);
    }
    static ExprPtr literal(Value v) { auto e=std::make_shared<Expr>(); e->kind=Expr::Kind::Literal; e->literal=std::move(v); return e; }
    static ExprPtr node(Expr::Kind k, std::string s, ExprPtr a={}, ExprPtr b={}) {
        auto e=std::make_shared<Expr>(); e->kind=k; e->text=std::move(s); e->left=std::move(a); e->right=std::move(b); return e;
    }
    StmtPtr statement() {
        if (match("let") || match("const")) {
            const bool immutable=previous().text=="const";
            auto s=std::make_shared<Stmt>(); s->kind=immutable?Stmt::Kind::Const:Stmt::Kind::Let;
            s->name=consume_identifier(immutable?"после const ожидается имя константы":"после let ожидается имя переменной").text;
            consume("=", "после имени переменной ожидается '='"); s->expr=expression(); optional_semicolon(); return s;
        }
        if (match("print")) {
            auto s=std::make_shared<Stmt>(); s->kind=Stmt::Kind::Print;
            s->expr=expression(); optional_semicolon(); return s;
        }
        if (match("if")) {
            auto s=std::make_shared<Stmt>(); s->kind=Stmt::Kind::If;
            consume("(", "после if ожидается '('"); s->expr=expression(); consume(")", "ожидается ')'");
            s->body=block(); if (match("else")) s->else_body=block(); return s;
        }
        if (match("while")) {
            auto s=std::make_shared<Stmt>(); s->kind=Stmt::Kind::While;
            consume("(", "после while ожидается '('"); s->expr=expression(); consume(")", "ожидается ')'"); s->body=block(); return s;
        }
        if (match("for")) {
            auto s=std::make_shared<Stmt>(); s->kind=Stmt::Kind::For;
            consume("(", "после for ожидается '('");
            if (!match(";")) {
                if (match("let")) {
                    auto init=std::make_shared<Stmt>(); init->kind=Stmt::Kind::Let;
                    init->name=consume_identifier("после let ожидается имя переменной").text;
                    consume("=", "после имени переменной ожидается '='"); init->expr=expression();
                    s->initializer=init;
                } else {
                    s->initializer=for_clause_statement();
                }
                consume(";", "после инициализации for ожидается ';'");
            }
            if (!check(";")) s->expr=expression();
            consume(";", "после условия for ожидается ';'");
            if (!check(")")) s->increment=for_clause_statement();
            consume(")", "после частей for ожидается ')'");
            s->body=block(); return s;
        }
        if (match("break")) { auto s=std::make_shared<Stmt>(); s->kind=Stmt::Kind::Break; optional_semicolon(); return s; }
        if (match("continue")) { auto s=std::make_shared<Stmt>(); s->kind=Stmt::Kind::Continue; optional_semicolon(); return s; }
        if (match("fn")) {
            auto s=std::make_shared<Stmt>(); s->kind=Stmt::Kind::Function;
            s->name=consume_identifier("после fn ожидается имя функции").text;
            consume("(", "после имени функции ожидается '('");
            if (!check(")")) {
                do {
                    const auto parameter=consume_identifier("ожидается имя параметра").text;
                    if (std::find(s->params.begin(),s->params.end(),parameter)!=s->params.end())
                        throw Error("VEYRA: повторяющийся параметр функции '" + parameter + "'");
                    s->params.push_back(parameter);
                } while (match(","));
            }
            consume(")", "ожидается ')' после параметров"); s->body=block(); return s;
        }
        if (match("return")) {
            auto s=std::make_shared<Stmt>(); s->kind=Stmt::Kind::Return;
            if (!check(";") && !check("}") && peek().kind != TokenKind::End) s->expr=expression();
            optional_semicolon(); return s;
        }
        if (peek().kind == TokenKind::Identifier && current_ + 1 < tokens_.size() && tokens_[current_+1].text == "=") {
            auto s=std::make_shared<Stmt>(); s->kind=Stmt::Kind::Assign; s->name=advance().text; advance(); s->expr=expression(); optional_semicolon(); return s;
        }
        auto s=std::make_shared<Stmt>(); s->kind=Stmt::Kind::Expression; s->expr=expression(); optional_semicolon(); return s;
    }
    StmtPtr for_clause_statement() {
        if (peek().kind == TokenKind::Identifier && current_ + 1 < tokens_.size() && tokens_[current_+1].text == "=") {
            auto s=std::make_shared<Stmt>(); s->kind=Stmt::Kind::Assign;
            s->name=advance().text; advance(); s->expr=expression(); return s;
        }
        auto s=std::make_shared<Stmt>(); s->kind=Stmt::Kind::Expression; s->expr=expression(); return s;
    }
    std::vector<StmtPtr> block() {
        NestingGuard guard(nesting_);
        consume("{", "ожидается блок '{ ... }'"); std::vector<StmtPtr> out;
        while (!check("}") && !check_end()) out.push_back(statement());
        consume("}", "незакрытый блок: ожидается '}'"); return out;
    }
    void optional_semicolon() { match(";"); }
    ExprPtr expression() { return logical_or(); }
    ExprPtr logical_or() {
        auto e=logical_and(); while (match("||")) e=node(Expr::Kind::Binary, "||", e, logical_and()); return e;
    }
    ExprPtr logical_and() {
        auto e=equality(); while (match("&&")) e=node(Expr::Kind::Binary, "&&", e, equality()); return e;
    }
    ExprPtr equality() {
        auto e=comparison(); while (check("==") || check("!=")) { auto op=advance().text; e=node(Expr::Kind::Binary, op, e, comparison()); } return e;
    }
    ExprPtr comparison() {
        auto e=term(); while (check("<") || check(">") || check("<=") || check(">=")) { auto op=advance().text; e=node(Expr::Kind::Binary, op, e, term()); } return e;
    }
    ExprPtr term() {
        auto e=factor(); while (check("+") || check("-")) { auto op=advance().text; e=node(Expr::Kind::Binary, op, e, factor()); } return e;
    }
    ExprPtr factor() {
        auto e=unary(); while (check("*") || check("/") || check("%")) { auto op=advance().text; e=node(Expr::Kind::Binary, op, e, unary()); } return e;
    }
    ExprPtr unary() {
        NestingGuard guard(nesting_);
        if (check("!") || check("-")) { auto op=advance().text; return node(Expr::Kind::Unary, op, unary()); }
        return primary();
    }
    ExprPtr primary() {
        NestingGuard guard(nesting_);
        if (match("true")) return literal(Value(true));
        if (match("false")) return literal(Value(false));
        if (match("none")) return literal(Value());
        if (peek().kind == TokenKind::Number) {
            auto t=advance(); try { const double n=std::stod(t.text); if(!std::isfinite(n)) fail(t,"число вне допустимого диапазона"); return literal(Value(n)); } catch (const Error&) { throw; } catch (...) { fail(t, "неверное число"); }
        }
        if (peek().kind == TokenKind::String) return literal(Value(advance().text));
        if (match("(")) { auto e=expression(); consume(")", "ожидается ')'"); return e; }
        if (peek().kind == TokenKind::Identifier) {
            std::string name=advance().text;
            while (match(".")) name += "." + consume_identifier("после '.' ожидается имя").text;
            if (match("(")) {
                auto e=node(Expr::Kind::Call, name);
                if (!check(")")) do { e->args.push_back(expression()); } while (match(","));
                consume(")", "ожидается ')' после аргументов"); return e;
            }
            return node(Expr::Kind::Variable, name);
        }
        fail(peek(), "ожидалось выражение, найдено '" + peek().text + "'");
    }
};

struct Environment {
    std::unordered_map<std::string, Value> values;
    std::unordered_set<std::string> constants;
    std::shared_ptr<Environment> parent;
    explicit Environment(std::shared_ptr<Environment> p={}) : parent(std::move(p)) {}
    void define(const std::string& n, Value v, bool immutable=false) {
        if (values.contains(n)) throw Error("имя '" + n + "' уже объявлено в этой области видимости");
        values.emplace(n,std::move(v));
        if (immutable) constants.insert(n);
    }
    void assign(const std::string& n, Value v) {
        if (values.contains(n)) {
            if (constants.contains(n)) throw Error("нельзя изменить const '" + n + "'");
            values[n]=std::move(v); return;
        }
        if (parent) { parent->assign(n, std::move(v)); return; }
        throw Error("переменная '" + n + "' не объявлена; сначала используй let " + n + " = ...");
    }
    Value get(const std::string& n) const {
        auto it=values.find(n); if (it!=values.end()) return it->second;
        if (parent) return parent->get(n);
        throw Error("неизвестная переменная '" + n + "'");
    }
};
struct Function { std::vector<std::string> params; std::vector<StmtPtr> body; };
struct ReturnSignal { Value value; };
struct BreakSignal {};
struct ContinueSignal {};

class Interpreter {
public:
    Interpreter() : globals_(std::make_shared<Environment>()) {}
    void run(const std::vector<StmtPtr>& program) { execute_block(program, globals_); }
private:
    std::shared_ptr<Environment> globals_;
    std::unordered_map<std::string, Function> functions_;
    std::uint64_t loop_steps_=0;
    std::uint64_t execution_steps_=0;
    std::uint64_t evaluation_steps_=0;
    std::size_t output_bytes_=0;
    std::size_t call_depth_=0;
    std::size_t loop_depth_=0;

    static Value finite_number(double n, const std::string& operation) {
        if (!std::isfinite(n)) throw Error(operation + ": результат вне допустимого диапазона числа");
        return Value(n);
    }
    static bool same_value(const Value& a, const Value& b) {
        if (a.data.index()!=b.data.index()) return false;
        if (std::holds_alternative<std::monostate>(a.data)) return true;
        if (auto p=std::get_if<double>(&a.data)) return *p==std::get<double>(b.data);
        if (auto p=std::get_if<std::string>(&a.data)) return *p==std::get<std::string>(b.data);
        return std::get<bool>(a.data)==std::get<bool>(b.data);
    }
    static std::string arg_string(const std::vector<Value>& a, std::size_t i, const std::string& fn) {
        if (i>=a.size()) throw Error(fn + ": не хватает аргументов");
        if (!std::holds_alternative<std::string>(a[i].data)) throw Error(fn + ": аргумент " + std::to_string(i+1) + " должен быть строкой");
        return std::get<std::string>(a[i].data);
    }
    static void arity(const std::vector<Value>& a, std::size_t n, const std::string& fn) {
        if (a.size()!=n) throw Error(fn + ": ожидалось аргументов: " + std::to_string(n) + ", получено: " + std::to_string(a.size()));
    }
    Value call_builtin(const std::string& name, const std::vector<Value>& args) {
        if (name=="text.len" || name=="text.lower" || name=="text.upper" || name=="text.trim" ||
            name=="text.contains" || name=="text.starts_with" || name=="text.ends_with") {
            if (name=="text.contains" || name=="text.starts_with" || name=="text.ends_with") {
                arity(args,2,name); const auto value=arg_string(args,0,name); const auto needle=arg_string(args,1,name);
                if (name=="text.contains") return Value(value.find(needle)!=std::string::npos);
                if (name=="text.starts_with") return Value(value.rfind(needle,0)==0);
                return Value(value.size()>=needle.size() && value.compare(value.size()-needle.size(),needle.size(),needle)==0);
            }
            arity(args,1,name); std::string value=arg_string(args,0,name);
            if (name=="text.len") return Value(static_cast<double>(value.size()));
            if (name=="text.trim") {
                auto first=std::find_if_not(value.begin(),value.end(),[](unsigned char c){return std::isspace(c)!=0;});
                auto last=std::find_if_not(value.rbegin(),value.rend(),[](unsigned char c){return std::isspace(c)!=0;}).base();
                if (first>=last) return Value("");
                return Value(std::string(first,last));
            }
            for (char& c:value) {
                const auto uc=static_cast<unsigned char>(c);
                c=static_cast<char>(name=="text.lower"?std::tolower(uc):std::toupper(uc));
            }
            return Value(value);
        }
        if (name=="math.abs" || name=="math.floor" || name=="math.ceil" || name=="math.round" || name=="math.sqrt") {
            arity(args,1,name); const double n=args[0].number(); double result=n;
            if (name=="math.abs") result=std::fabs(n);
            else if (name=="math.floor") result=std::floor(n);
            else if (name=="math.ceil") result=std::ceil(n);
            else if (name=="math.round") result=std::round(n);
            else { if (n<0) throw Error(name + ": число не может быть отрицательным"); result=std::sqrt(n); }
            if (!std::isfinite(result)) throw Error(name + ": результат вне допустимого диапазона");
            return Value(result);
        }
        if (name=="math.min" || name=="math.max" || name=="math.pow") {
            arity(args,2,name); const double a=args[0].number(), b=args[1].number(); double result=0;
            if (name=="math.min") result=std::min(a,b);
            else if (name=="math.max") result=std::max(a,b);
            else result=std::pow(a,b);
            if (!std::isfinite(result)) throw Error(name + ": результат вне допустимого диапазона");
            return Value(result);
        }
        if (name=="linux.os") { arity(args,0,name); return Value("Linux"); }
        if (name=="linux.kernel" || name=="linux.arch" || name=="linux.hostname") {
            arity(args,0,name); struct utsname u{};
            if (uname(&u)!=0) throw Error(name + ": uname завершился ошибкой");
            if (name=="linux.kernel") return Value(u.release);
            if (name=="linux.arch") return Value(u.machine);
            return Value(u.nodename);
        }
        if (name=="linux.pid") { arity(args,0,name); return Value(static_cast<double>(getpid())); }
        if (name=="linux.page_size") { arity(args,0,name); const long n=sysconf(_SC_PAGESIZE); if (n<=0) throw Error(name + ": sysconf завершился ошибкой"); return Value(static_cast<double>(n)); }
        if (name=="system.cwd") { arity(args,0,name); std::error_code ec; auto p=std::filesystem::current_path(ec); if (ec) throw Error(name + ": не удалось получить рабочий каталог: " + ec.message()); return Value(p.string()); }
        if (name=="security.is_root") { arity(args,0,name); return Value(geteuid()==0); }
        if (name=="security.uid") { arity(args,0,name); return Value(static_cast<double>(getuid())); }
        if (name=="security.exists" || name=="security.readable" || name=="security.writable" || name=="security.executable") {
            arity(args,1,name); auto p=arg_string(args,0,name);
            if (name=="security.exists") return Value(::access(p.c_str(), F_OK)==0);
            int mode=name=="security.readable"?R_OK:(name=="security.writable"?W_OK:X_OK);
            return Value(::access(p.c_str(),mode)==0);
        }
        if (name=="security.is_file" || name=="security.is_directory" || name=="security.is_symlink" ||
            name=="security.size" || name=="security.owner_uid" || name=="security.world_writable" || name=="security.setuid") {
            arity(args,1,name); const auto path=arg_string(args,0,name); struct stat st{};
            if (::lstat(path.c_str(),&st)!=0) {
                if (errno==ENOENT || errno==ENOTDIR) return Value(false);
                throw Error(name + ": lstat не выполнен для '" + path + "': " + std::string(std::strerror(errno)));
            }
            if (name=="security.is_file") return Value(S_ISREG(st.st_mode));
            if (name=="security.is_directory") return Value(S_ISDIR(st.st_mode));
            if (name=="security.is_symlink") return Value(S_ISLNK(st.st_mode));
            if (name=="security.size") return Value(static_cast<double>(st.st_size));
            if (name=="security.owner_uid") return Value(static_cast<double>(st.st_uid));
            if (name=="security.world_writable") return Value((st.st_mode&S_IWOTH)!=0);
            return Value((st.st_mode&S_ISUID)!=0);
        }
        if (name=="security.mode" || name=="security.audit" || name=="security.sha256") {
            arity(args,1,name); auto path=arg_string(args,0,name);
            if (name=="security.sha256") {
                const int fd=::open(path.c_str(),O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);
                if (fd<0) throw Error(name + ": невозможно безопасно открыть обычный файл '" + path + "': " + std::string(std::strerror(errno)));
                struct FdGuard { int fd; ~FdGuard(){ if(fd>=0) ::close(fd); } } fd_guard{fd};
                struct stat fst{};
                if (::fstat(fd,&fst)!=0) throw Error(name + ": fstat завершился ошибкой: " + std::string(std::strerror(errno)));
                if (!S_ISREG(fst.st_mode)) throw Error(name + ": разрешено хешировать только обычные файлы без символических ссылок");
                if (fst.st_size<0 || static_cast<std::uintmax_t>(fst.st_size)>MAX_HASH_BYTES)
                    throw Error(name + ": размер файла превышает лимит 256 MiB");
                EVP_MD_CTX* raw=EVP_MD_CTX_new();
                if (!raw) throw Error(name + ": не удалось создать SHA-256 context");
                struct CtxGuard { EVP_MD_CTX* p; ~CtxGuard(){EVP_MD_CTX_free(p);} } guard{raw};
                if (EVP_DigestInit_ex(raw,EVP_sha256(),nullptr)!=1) throw Error(name + ": ошибка инициализации SHA-256");
                unsigned char buffer[65536]; std::uintmax_t hashed_bytes=0;
                for (;;) {
                    const ssize_t got=::read(fd,buffer,sizeof(buffer));
                    if (got==0) break;
                    if (got<0) { if (errno==EINTR) continue; throw Error(name + ": ошибка чтения файла: " + std::string(std::strerror(errno))); }
                    hashed_bytes+=static_cast<std::uintmax_t>(got);
                    if (hashed_bytes>MAX_HASH_BYTES) throw Error(name + ": файл вырос и превысил лимит 256 MiB");
                    if (EVP_DigestUpdate(raw,buffer,static_cast<std::size_t>(got))!=1) throw Error(name + ": ошибка SHA-256");
                }
                unsigned char digest[EVP_MAX_MD_SIZE]; unsigned int length=0;
                if (EVP_DigestFinal_ex(raw,digest,&length)!=1) throw Error(name + ": ошибка завершения SHA-256");
                std::ostringstream out; out<<std::hex<<std::setfill('0');
                for (unsigned int i=0;i<length;++i) out<<std::setw(2)<<static_cast<unsigned>(digest[i]);
                return Value(out.str());
            }
            struct stat st{};
            if (::lstat(path.c_str(), &st)!=0) throw Error(name + ": не удалось открыть метаданные '" + path + "': " + std::string(std::strerror(errno)));
            if (name=="security.mode") {
                std::ostringstream out; out << std::oct << std::setw(4) << std::setfill('0') << (st.st_mode & 07777); return Value(out.str());
            }
            if (name=="security.audit") {
                std::ostringstream out;
                out << "VEYRA security audit: " << path << "\n";
                out << "type: " << (S_ISREG(st.st_mode)?"regular file":(S_ISDIR(st.st_mode)?"directory":(S_ISLNK(st.st_mode)?"symbolic link":"other"))) << "\n";
                out << "mode: " << std::oct << std::setw(4) << std::setfill('0') << (st.st_mode & 07777) << std::dec << "\n";
                out << "owner uid: " << st.st_uid << "\n";
                out << "size: " << static_cast<long long>(st.st_size) << " bytes\n";
                if (st.st_mode & S_IWOTH) out << "WARNING: world-writable\n";
                if (st.st_mode & S_ISUID) out << "WARNING: setuid bit set\n";
                if (st.st_mode & S_ISGID) out << "NOTICE: setgid bit set\n";
                if (!(st.st_mode & S_IWOTH) && !(st.st_mode & S_ISUID) && !(st.st_mode & S_ISGID)) out << "no basic mode-bit warning detected\n";
                out << "readable-by-current-user: " << (::access(path.c_str(),R_OK)==0?"true":"false") << "\n";
                out << "writable-by-current-user: " << (::access(path.c_str(),W_OK)==0?"true":"false");
                return Value(out.str());
            }
            throw Error("внутренняя ошибка маршрутизации security.sha256");
        }
        throw Error("неизвестная функция '" + name + "'");
    }
    Value eval(const ExprPtr& e, const std::shared_ptr<Environment>& env) {
        if (++evaluation_steps_ > MAX_EVALUATION_STEPS)
            throw Error("превышен общий лимит вычислений выражений (20 000 000 операций)");
        switch(e->kind) {
            case Expr::Kind::Literal: return e->literal;
            case Expr::Kind::Variable: return env->get(e->text);
            case Expr::Kind::Unary: {
                Value v=eval(e->left,env);
                if (e->text=="!") return Value(!v.truthy());
                return finite_number(-v.number(), "унарный минус");
            }
            case Expr::Kind::Binary: {
                Value a=eval(e->left,env);
                if (e->text=="&&") return Value(a.truthy() && eval(e->right,env).truthy());
                if (e->text=="||") return Value(a.truthy() || eval(e->right,env).truthy());
                Value b=eval(e->right,env);
                if (e->text=="==") return Value(same_value(a,b));
                if (e->text=="!=") return Value(!same_value(a,b));
                if (e->text=="+") {
                    if (std::holds_alternative<std::string>(a.data) || std::holds_alternative<std::string>(b.data)) {
                        const std::string left=a.str(), right=b.str();
                        if (right.size()>MAX_STRING_BYTES || left.size()>MAX_STRING_BYTES-right.size())
                            throw Error("сложение строк: превышен лимит строки (4 MiB)");
                        return Value(left+right);
                    }
                    return finite_number(a.number()+b.number(), "сложение");
                }
                if (e->text=="-") return finite_number(a.number()-b.number(), "вычитание");
                if (e->text=="*") return finite_number(a.number()*b.number(), "умножение");
                if (e->text=="/") { double d=b.number(); if (d==0.0) throw Error("деление на ноль"); return finite_number(a.number()/d, "деление"); }
                if (e->text=="%") { double d=b.number(); if (d==0.0) throw Error("остаток от деления на ноль"); return finite_number(std::fmod(a.number(),d), "остаток от деления"); }
                if (e->text=="<" || e->text==">" || e->text=="<=" || e->text==">=") {
                    bool result=false;
                    if (std::holds_alternative<std::string>(a.data) && std::holds_alternative<std::string>(b.data)) {
                        auto x=a.str(), y=b.str();
                        if (e->text=="<") result=x<y; else if (e->text==">") result=x>y; else if (e->text=="<=") result=x<=y; else result=x>=y;
                    } else {
                        double x=a.number(), y=b.number();
                        if (e->text=="<") result=x<y; else if (e->text==">") result=x>y; else if (e->text=="<=") result=x<=y; else result=x>=y;
                    }
                    return Value(result);
                }
                throw Error("неподдерживаемый оператор '"+e->text+"'");
            }
            case Expr::Kind::Call: {
                std::vector<Value> args; args.reserve(e->args.size()); for (auto& a:e->args) args.push_back(eval(a,env));
                if (e->text.rfind("linux.",0)==0 || e->text.rfind("security.",0)==0 ||
                    e->text.rfind("text.",0)==0 || e->text.rfind("math.",0)==0 || e->text=="system.cwd")
                    return call_builtin(e->text,args);
                auto it=functions_.find(e->text); if (it==functions_.end()) throw Error("неизвестная функция '"+e->text+"'");
                if (args.size()!=it->second.params.size()) throw Error("функция '"+e->text+"': ожидалось аргументов " + std::to_string(it->second.params.size()) + ", получено " + std::to_string(args.size()));
                if (++call_depth_>MAX_CALL_DEPTH) { --call_depth_; throw Error("превышена максимальная глубина вызовов функций"); }
                struct DepthGuard { std::size_t& depth; ~DepthGuard(){ --depth; } } depth_guard{call_depth_};
                struct FunctionLoopGuard { std::size_t& depth; std::size_t saved; explicit FunctionLoopGuard(std::size_t& d):depth(d),saved(d){depth=0;} ~FunctionLoopGuard(){depth=saved;} } function_loop_guard{loop_depth_};
                auto local=std::make_shared<Environment>(globals_);
                for (std::size_t i=0;i<args.size();++i) local->define(it->second.params[i],args[i]);
                Value result;
                try { execute_block(it->second.body,local); }
                catch (const ReturnSignal& r) { result=r.value; }
                return result;
            }
        }
        return Value();
    }
    void execute_block(const std::vector<StmtPtr>& body, const std::shared_ptr<Environment>& env) {
        for (const auto& s:body) execute(s,env);
    }
    void execute(const StmtPtr& s, const std::shared_ptr<Environment>& env) {
        if (++execution_steps_ > MAX_EXECUTION_STEPS)
            throw Error("превышен общий лимит исполнения (5 000 000 инструкций)");
        switch(s->kind) {
            case Stmt::Kind::Let: env->define(s->name,eval(s->expr,env)); break;
            case Stmt::Kind::Const: env->define(s->name,eval(s->expr,env),true); break;
            case Stmt::Kind::Assign: env->assign(s->name,eval(s->expr,env)); break;
            case Stmt::Kind::Print: {
                const std::string output=eval(s->expr,env).str();
                if (output.size()>=MAX_OUTPUT_BYTES || output_bytes_>MAX_OUTPUT_BYTES-output.size()-1)
                    throw Error("превышен лимит вывода (16 MiB)");
                output_bytes_+=output.size()+1;
                std::cout << output << '\n';
                break;
            }
            case Stmt::Kind::If: if (eval(s->expr,env).truthy()) execute_block(s->body,std::make_shared<Environment>(env)); else execute_block(s->else_body,std::make_shared<Environment>(env)); break;
            case Stmt::Kind::While:
                while (eval(s->expr,env).truthy()) {
                    if (++loop_steps_>MAX_LOOP_STEPS) throw Error("превышен лимит итераций (1 000 000); защита от бесконечного цикла");
                    try {
                        struct LoopGuard { std::size_t& depth; explicit LoopGuard(std::size_t& d):depth(d){++depth;} ~LoopGuard(){--depth;} } guard{loop_depth_};
                        execute_block(s->body,std::make_shared<Environment>(env));
                    }
                    catch (const ContinueSignal&) { continue; }
                    catch (const BreakSignal&) { break; }
                }
                break;
            case Stmt::Kind::For: {
                auto loop_env=std::make_shared<Environment>(env);
                if (s->initializer) execute(s->initializer,loop_env);
                while (!s->expr || eval(s->expr,loop_env).truthy()) {
                    if (++loop_steps_>MAX_LOOP_STEPS) throw Error("превышен лимит итераций (1 000 000); защита от бесконечного цикла");
                    bool should_break=false;
                    try {
                        struct LoopGuard { std::size_t& depth; explicit LoopGuard(std::size_t& d):depth(d){++depth;} ~LoopGuard(){--depth;} } guard{loop_depth_};
                        execute_block(s->body,std::make_shared<Environment>(loop_env));
                    }
                    catch (const ContinueSignal&) {}
                    catch (const BreakSignal&) { should_break=true; }
                    if (should_break) break;
                    if (s->increment) execute(s->increment,loop_env);
                }
                break;
            }
            case Stmt::Kind::Break: if (loop_depth_==0) throw Error("break разрешён только внутри цикла"); throw BreakSignal{};
            case Stmt::Kind::Continue: if (loop_depth_==0) throw Error("continue разрешён только внутри цикла"); throw ContinueSignal{};
            case Stmt::Kind::Function:
                if (functions_.contains(s->name)) throw Error("функция '"+s->name+"' уже объявлена");
                functions_.emplace(s->name,Function{s->params,s->body}); break;
            case Stmt::Kind::Return: throw ReturnSignal{s->expr?eval(s->expr,env):Value()};
            case Stmt::Kind::Expression: (void)eval(s->expr,env); break;
        }
    }
};

void usage() {
    std::cout << "VEYRA language 0.2.0 — Linux-only runtime\n"
              << "Usage: veyra <file.vey>       Run program\n"
              << "       veyra --check <file.vey> Validate syntax without running\n"
              << "       veyra --builtins         List standard functions\n"
              << "       veyra --version          Show version\n"
              << "       veyra --help             Show this help\n\n"
              << "Limits: source 4 MiB, tokens 250k, parser depth 256, loops 1M,\n"
              << "        execution 5M statements, evaluation 20M operations,\n"
              << "        output 16 MiB, string 4 MiB.\n"
              << "Safety: no shell execution, network API, or arbitrary file-write builtin.\n";
}

} // namespace veyra

int main(int argc, char** argv) {
    using namespace veyra;
    if (argc==1) { usage(); return 0; }
    if (argc==2 && (std::string(argv[1])=="--help" || std::string(argv[1])=="-h")) { usage(); return 0; }
    if (argc==2 && (std::string(argv[1])=="--version" || std::string(argv[1])=="-v")) { std::cout << "VEYRA 0.2.0 (Linux-only, C++20)\n"; return 0; }
    if (argc==2 && std::string(argv[1])=="--builtins") {
        std::cout << "text.len, text.lower, text.upper, text.trim, text.contains, text.starts_with, text.ends_with\n"
                  << "math.abs, math.floor, math.ceil, math.round, math.sqrt, math.min, math.max, math.pow\n"
                  << "linux.os, linux.kernel, linux.arch, linux.hostname, linux.pid, linux.page_size, system.cwd\n"
                  << "security.is_root, security.uid, security.exists, security.readable, security.writable, security.executable\n"
                  << "security.mode, security.audit, security.sha256, security.is_file, security.is_directory, security.is_symlink\n"
                  << "security.size, security.owner_uid, security.world_writable, security.setuid\n";
        return 0;
    }
    const bool check_only=argc==3 && std::string(argv[1])=="--check";
    if ((!check_only && argc!=2) || (check_only && argc!=3)) { usage(); return 2; }
    const std::string arg=argv[check_only?2:1];
    if (arg.size()<4 || arg.substr(arg.size()-4)!=".vey") {
        std::cerr << "VEYRA: ожидается исходный файл с расширением .vey\n"; return 2;
    }
    std::ifstream file(arg,std::ios::binary);
    if (!file) { std::cerr << "VEYRA: не удалось открыть файл: " << arg << '\n'; return 1; }
    file.seekg(0,std::ios::end); auto size=file.tellg();
    if (size<0 || static_cast<std::uintmax_t>(size)>MAX_SOURCE_BYTES) {
        std::cerr << "VEYRA: файл слишком большой (лимит 4 MiB)\n"; return 1;
    }
    file.seekg(0,std::ios::beg); std::string source(static_cast<std::size_t>(size),'\0');
    if (!source.empty()) file.read(source.data(),static_cast<std::streamsize>(source.size()));
    if (!file && !source.empty()) { std::cerr << "VEYRA: ошибка чтения файла\n"; return 1; }
    try {
        Lexer lexer(std::move(source));
        Parser parser(lexer.scan());
        auto program=parser.parse_program();
        if (check_only) { std::cout << "VEYRA: синтаксис OK — " << arg << "\n"; return 0; }
        Interpreter interpreter;
        interpreter.run(program);
    } catch (const ReturnSignal&) {
        std::cerr << "VEYRA: return разрешён только внутри функции\n"; return 1;
    } catch (const BreakSignal&) {
        std::cerr << "VEYRA: break разрешён только внутри цикла\n"; return 1;
    } catch (const ContinueSignal&) {
        std::cerr << "VEYRA: continue разрешён только внутри цикла\n"; return 1;
    } catch (const Error& e) {
        std::cerr << e.what() << '\n'; return 1;
    } catch (const std::exception& e) {
        std::cerr << "VEYRA: внутренняя ошибка: " << e.what() << '\n'; return 1;
    }
    return 0;
}
