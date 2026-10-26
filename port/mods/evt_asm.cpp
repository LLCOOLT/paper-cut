// A mod's text scripts into the game's script words (port/mods/evt_asm.h).
#include "evt_asm.h"
#include "evt_table_gen.h"

#include <cctype>
#include <cmath>
#include <cstring>

namespace port_evt {

namespace {

constexpr int kOpEndScript = 1;
// include/evt/evt_cmd.h's EVTDAT_*_BASE, by the macro naming them
struct VarKind {
  const char* name;
  long long base;
};
constexpr VarKind kVarKinds[] = {{"LW", 30000000},   {"GW", 50000000},   {"LF", 70000000},   {"GF", 90000000},
                                 {"LSWF", 110000000}, {"GSWF", 130000000}, {"LSW", 150000000}, {"GSW", 170000000},
                                 {"UW", 190000000},   {"UF", 210000000}};
constexpr long long kFloatBase = 230000000;

const PortEvtCommand* command(const std::string& name) {
  size_t lo = 0, hi = sizeof(kPortEvtCommands) / sizeof(kPortEvtCommands[0]);
  while (lo < hi) {
    const size_t mid = (lo + hi) / 2;
    const int c = std::strcmp(kPortEvtCommands[mid].name, name.c_str());
    if (c == 0) {
      return &kPortEvtCommands[mid];
    }
    (c < 0 ? lo = mid + 1 : hi = mid);
  }
  return nullptr;
}

const PortEvtConstant* constant(const std::string& name) {
  size_t lo = 0, hi = sizeof(kPortEvtConstants) / sizeof(kPortEvtConstants[0]);
  while (lo < hi) {
    const size_t mid = (lo + hi) / 2;
    const int c = std::strcmp(kPortEvtConstants[mid].name, name.c_str());
    if (c == 0) {
      return &kPortEvtConstants[mid];
    }
    (c < 0 ? lo = mid + 1 : hi = mid);
  }
  return nullptr;
}

// --- tokens ---
enum class Tok { End, Ident, Number, String, Punct };

struct Token {
  Tok type = Tok::End;
  std::string text; // an identifier, a string's bytes, a punctuator
  double number = 0;
  bool isFloat = false;
  int line = 0;
};

struct Lexer {
  const std::string& s;
  size_t i = 0;
  int line = 1;
  std::string error;

  explicit Lexer(const std::string& text) : s(text) {}

  void skip() {
    for (;;) {
      while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) {
        line += s[i++] == '\n';
      }
      if (i < s.size() && (s[i] == '#' || s.compare(i, 2, "//") == 0)) {
        while (i < s.size() && s[i] != '\n') {
          i++;
        }
      } else if (s.compare(i, 2, "/*") == 0) {
        const size_t end = s.find("*/", i + 2);
        for (size_t k = i; k < (end == std::string::npos ? s.size() : end); k++) {
          line += s[k] == '\n';
        }
        i = end == std::string::npos ? s.size() : end + 2;
      } else {
        return;
      }
    }
  }

  Token next() {
    skip();
    Token t;
    t.line = line;
    if (i >= s.size()) {
      return t;
    }
    const char c = s[i];
    if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
      size_t j = i;
      for (;;) {
        while (j < s.size() && (std::isalnum(static_cast<unsigned char>(s[j])) || s[j] == '_')) {
          j++;
        }
        if (s.compare(j, 2, "::") == 0) { // file::name, a file's static
          j += 2;
          continue;
        }
        break;
      }
      t.type = Tok::Ident;
      t.text = s.substr(i, j - i);
      i = j;
      return t;
    }
    if (std::isdigit(static_cast<unsigned char>(c)) || (c == '.' && i + 1 < s.size() && std::isdigit(static_cast<unsigned char>(s[i + 1])))) {
      size_t j = i;
      if (s.compare(i, 2, "0x") == 0 || s.compare(i, 2, "0X") == 0) {
        j += 2;
        while (j < s.size() && std::isxdigit(static_cast<unsigned char>(s[j]))) {
          j++;
        }
        t.number = double(std::stoull(s.substr(i + 2, j - i - 2), nullptr, 16));
      } else {
        while (j < s.size() && (std::isdigit(static_cast<unsigned char>(s[j])) || s[j] == '.' || s[j] == 'e' || s[j] == 'E' ||
                                ((s[j] == '-' || s[j] == '+') && (s[j - 1] == 'e' || s[j - 1] == 'E')))) {
          t.isFloat |= s[j] == '.' || s[j] == 'e' || s[j] == 'E';
          j++;
        }
        t.number = std::stod(s.substr(i, j - i));
        if (j < s.size() && (s[j] == 'f' || s[j] == 'F')) {
          t.isFloat = true;
          j++;
        }
      }
      while (j < s.size() && (s[j] == 'u' || s[j] == 'U' || s[j] == 'l' || s[j] == 'L')) {
        j++;
      }
      t.type = Tok::Number;
      i = j;
      return t;
    }
    if (c == '"') {
      size_t j = i + 1;
      while (j < s.size() && s[j] != '"') {
        if (s[j] == '\\' && j + 1 < s.size()) {
          const char e = s[++j];
          if (e == 'n') {
            t.text += '\n';
          } else if (e == 't') {
            t.text += '\t';
          } else if (e == 'x') {
            size_t k = j + 1;
            while (k < s.size() && k < j + 3 && std::isxdigit(static_cast<unsigned char>(s[k]))) {
              k++;
            }
            t.text += char(std::stoi(s.substr(j + 1, k - j - 1), nullptr, 16));
            j = k - 1;
          } else {
            t.text += e;
          }
        } else {
          line += s[j] == '\n';
          t.text += s[j];
        }
        j++;
      }
      if (j >= s.size()) {
        error = "a string doesn't end";
      }
      t.type = Tok::String;
      i = j + 1;
      return t;
    }
    static const char* const kPuncts[] = {"<<", ">>", "(", ")", ",", ";", "+", "-", "*", "/", "%", "|", "&", "^", "~"};
    for (const char* p : kPuncts) {
      if (s.compare(i, std::strlen(p), p) == 0) {
        t.type = Tok::Punct;
        t.text = p;
        i += std::strlen(p);
        return t;
      }
    }
    error = std::string("unexpected '") + c + "'";
    t.type = Tok::End;
    return t;
  }
};

} // namespace

// --- expressions ---
struct Expr {
  enum Kind { Number, String, Name, Call, Unary, Binary } kind;
  double number = 0;
  bool isFloat = false;
  std::string text;                           // a name, an operator, a function
  const std::string* string = nullptr;        // the mod's string
  std::vector<std::shared_ptr<Expr>> args;    // a call's or an operator's
};

namespace {

struct Parser {
  Lexer lex;
  Token tok;
  std::deque<std::string>& strings;
  std::string error;

  Parser(const std::string& text, std::deque<std::string>& s) : lex(text), strings(s) { tok = lex.next(); }

  void advance() { tok = lex.next(); }
  bool isPunct(const char* p) const { return tok.type == Tok::Punct && tok.text == p; }
  bool fail(const std::string& why) {
    if (error.empty()) {
      error = std::to_string(tok.line) + ": " + (lex.error.empty() ? why : lex.error);
    }
    return false;
  }

  std::shared_ptr<Expr> primary() {
    auto e = std::make_shared<Expr>();
    if (tok.type == Tok::Number) {
      e->kind = Expr::Number;
      e->number = tok.number;
      e->isFloat = tok.isFloat;
      advance();
    } else if (tok.type == Tok::String) {
      e->kind = Expr::String;
      strings.push_back(tok.text);
      e->string = &strings.back();
      advance();
    } else if (tok.type == Tok::Ident) {
      e->text = tok.text;
      advance();
      if (isPunct("(")) {
        e->kind = Expr::Call;
        advance();
        if (!isPunct(")")) {
          for (;;) {
            auto a = expr();
            if (!a) {
              return nullptr;
            }
            e->args.push_back(a);
            if (!isPunct(",")) {
              break;
            }
            advance();
          }
        }
        if (!isPunct(")")) {
          fail("expected ')'");
          return nullptr;
        }
        advance();
      } else {
        e->kind = Expr::Name;
      }
    } else if (isPunct("(")) {
      advance();
      e = expr();
      if (!e || !isPunct(")")) {
        fail("expected ')'");
        return nullptr;
      }
      advance();
    } else {
      fail("expected a value");
      return nullptr;
    }
    return e;
  }

  std::shared_ptr<Expr> unary() {
    if (isPunct("-") || isPunct("~") || isPunct("+")) {
      auto e = std::make_shared<Expr>();
      e->kind = Expr::Unary;
      e->text = tok.text;
      advance();
      auto a = unary();
      if (!a) {
        return nullptr;
      }
      e->args.push_back(a);
      return e;
    }
    return primary();
  }

  static int precedence(const std::string& op) {
    if (op == "|") return 1;
    if (op == "^") return 2;
    if (op == "&") return 3;
    if (op == "<<" || op == ">>") return 4;
    if (op == "+" || op == "-") return 5;
    if (op == "*" || op == "/" || op == "%") return 6;
    return 0;
  }

  std::shared_ptr<Expr> binary(int min) {
    auto left = unary();
    while (left && tok.type == Tok::Punct && precedence(tok.text) >= min && precedence(tok.text) > 0) {
      const std::string op = tok.text;
      advance();
      auto right = binary(precedence(op) + 1);
      if (!right) {
        return nullptr;
      }
      auto e = std::make_shared<Expr>();
      e->kind = Expr::Binary;
      e->text = op;
      e->args = {left, right};
      left = e;
    }
    return left;
  }

  std::shared_ptr<Expr> expr() { return binary(1); }
};

using Lookup = std::function<bool(const std::string&, Word&, std::string&)>;

struct Eval {
  const Lookup& lookup;
  std::string error;

  bool name(const std::string& n, Word& out) {
    if (const PortEvtConstant* c = constant(n)) {
      out = Word(c->value);
      return true;
    }
    std::string why;
    if (lookup(n, out, why)) {
      return true;
    }
    if (error.empty()) {
      error = why.empty() ? "no game name or constant " + n : why;
    }
    return false;
  }

  bool number(const Expr& e, double& out) {
    switch (e.kind) {
    case Expr::Number:
      out = e.number;
      return true;
    case Expr::Unary: {
      double a;
      if (!number(*e.args[0], a)) {
        return false;
      }
      out = e.text == "-" ? -a : e.text == "~" ? double(~(long long)a) : a;
      return true;
    }
    case Expr::Binary: {
      double a, b;
      if (!number(*e.args[0], a) || !number(*e.args[1], b)) {
        return false;
      }
      const std::string& op = e.text;
      out = op == "+" ? a + b : op == "-" ? a - b : op == "*" ? a * b : op == "/" ? (b != 0 ? a / b : 0) : 0;
      if (op == "%" || op == "|" || op == "&" || op == "^" || op == "<<" || op == ">>") {
        Word w;
        if (!word(e, w)) {
          return false;
        }
        out = double(w);
      }
      return true;
    }
    default: {
      Word w;
      if (!word(e, w)) {
        return false;
      }
      out = double(w);
      return true;
    }
    }
  }

  bool word(const Expr& e, Word& out) {
    switch (e.kind) {
    case Expr::Number:
      out = Word((long long)e.number);
      return true;
    case Expr::String:
      out = reinterpret_cast<Word>(e.string->c_str());
      return true;
    case Expr::Name:
      return name(e.text, out);
    case Expr::Unary: {
      Word a;
      if (!word(*e.args[0], a)) {
        return false;
      }
      out = e.text == "-" ? -a : e.text == "~" ? ~a : a;
      return true;
    }
    case Expr::Binary: {
      if (e.text == "/" || e.text == "*" || e.text == "+" || e.text == "-") {
        // floats stay floats until the end (FLOAT(1.5 * 2))
        bool anyFloat = false;
        std::function<void(const Expr&)> scan = [&](const Expr& x) {
          anyFloat |= x.kind == Expr::Number && x.isFloat;
          if (x.kind == Expr::Unary || x.kind == Expr::Binary) {
            for (auto& a : x.args) scan(*a);
          }
        };
        scan(e);
        if (anyFloat) {
          double d;
          if (!number(e, d)) {
            return false;
          }
          out = Word((long long)d);
          return true;
        }
      }
      Word a, b;
      if (!word(*e.args[0], a) || !word(*e.args[1], b)) {
        return false;
      }
      const std::string& op = e.text;
      if ((op == "/" || op == "%") && b == 0) {
        error = "divided by 0";
        return false;
      }
      out = op == "+" ? a + b : op == "-" ? a - b : op == "*" ? a * b : op == "/" ? a / b : op == "%" ? a % b
          : op == "|" ? (a | b) : op == "&" ? (a & b) : op == "^" ? (a ^ b) : op == "<<" ? (a << b) : (a >> b);
      return true;
    }
    case Expr::Call: {
      const std::string& f = e.text;
      if (e.args.size() != 1) {
        error = f + "() takes one value";
        return false;
      }
      if (f == "FLOAT") {
        double v;
        if (!number(*e.args[0], v)) {
          return false;
        }
        out = Word((int32_t)(float(v) * 1024.0f) - kFloatBase);
        return true;
      }
      if (f == "PTR" || f == "STRING") {
        return word(*e.args[0], out);
      }
      for (const VarKind& k : kVarKinds) {
        if (f == k.name) {
          Word id;
          if (!word(*e.args[0], id)) {
            return false;
          }
          out = id - Word(k.base);
          return true;
        }
      }
      error = "no function " + f + "() in a script's values (LW(), GSW(), FLOAT(), PTR()...)";
      return false;
    }
    }
    return false;
  }
};

} // namespace

Program::Program() = default;
Program::~Program() = default;

bool Program::add(const std::string& text, const std::string& file, std::string& error) {
  Parser p(text, mStrings);
  auto fail = [&](const std::string& why) {
    p.fail(why);
    error = file + ":" + p.error;
    return false;
  };
  while (p.tok.type != Tok::End) {
    if (p.isPunct(",") || p.isPunct(";")) {
      p.advance();
      continue;
    }
    if (p.tok.type != Tok::Ident || p.tok.text != "EVT_BEGIN") {
      return fail("expected EVT_BEGIN(name)");
    }
    p.advance();
    if (!p.isPunct("(")) {
      return fail("expected EVT_BEGIN(name)");
    }
    p.advance();
    if (p.tok.type != Tok::Ident) {
      return fail("expected a script's name");
    }
    const std::string name = p.tok.text;
    if (mByName.count(name)) {
      return fail("a second script " + name);
    }
    p.advance();
    if (!p.isPunct(")")) {
      return fail("expected ')'");
    }
    p.advance();
    const size_t index = mScripts.size();
    mScripts.push_back({name, {}});
    mByName[name] = index;
    std::vector<Word>& code = mScripts[index].code;
    for (;;) {
      if (p.isPunct(",") || p.isPunct(";")) {
        p.advance();
        continue;
      }
      if (p.tok.type != Tok::Ident) {
        return fail(p.tok.type == Tok::End ? "a script without EVT_END()" : "expected a command");
      }
      const std::string cmd = p.tok.text;
      const int line = p.tok.line;
      p.advance();
      std::vector<std::shared_ptr<Expr>> args;
      if (p.isPunct("(")) {
        p.advance();
        if (!p.isPunct(")")) {
          for (;;) {
            auto a = p.expr();
            if (!a) {
              return fail("");
            }
            args.push_back(a);
            if (!p.isPunct(",")) {
              break;
            }
            p.advance();
          }
        }
        if (!p.isPunct(")")) {
          return fail("expected ')'");
        }
        p.advance();
      }
      if (cmd == "EVT_END" || cmd == "EVT_END_PAD" || cmd == "EVT_END_PAD4") {
        code.push_back(kOpEndScript);
        code.insert(code.end(), cmd == "EVT_END_PAD" ? 1 : cmd == "EVT_END_PAD4" ? 4 : 0, Word(0));
        break;
      }
      if (cmd == "END") { // include/evt/evt_cmd.h's END is empty
        continue;
      }
      const PortEvtCommand* c = command(cmd);
      if (c == nullptr) {
        p.tok.line = line;
        return fail("no script command " + cmd);
      }
      if (c->args >= 0 && int(args.size()) != c->args) {
        p.tok.line = line;
        return fail(cmd + " takes " + std::to_string(c->args) + " values, not " + std::to_string(args.size()));
      }
      code.push_back(Word((int(args.size()) << 16) | c->opcode));
      for (auto& a : args) {
        mSlots.push_back({index, code.size(), a, file + ":" + std::to_string(line)});
        code.push_back(0);
      }
    }
  }
  if (!p.lex.error.empty()) {
    return fail(p.lex.error);
  }
  return true;
}

bool Program::link(const Lookup& lookup, std::vector<std::string>& errors) {
  const Lookup withScripts = [&](const std::string& n, Word& out, std::string& why) {
    if (const auto it = mByName.find(n); it != mByName.end()) {
      out = reinterpret_cast<Word>(mScripts[it->second].code.data());
      return true;
    }
    return lookup(n, out, why);
  };
  for (Slot& s : mSlots) {
    Eval ev{withScripts, {}};
    Word w = 0;
    if (!ev.word(*s.expr, w)) {
      errors.push_back(s.where + ": " + ev.error);
      w = 0;
    }
    mScripts[s.script].code[s.index] = w;
  }
  return errors.empty();
}

Word* Program::find(const std::string& name) {
  const auto it = mByName.find(name);
  return it == mByName.end() ? nullptr : mScripts[it->second].code.data();
}

} // namespace port_evt
