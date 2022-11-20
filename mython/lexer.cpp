#include "lexer.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <charconv>
#include <unordered_map>

using namespace std;

namespace parse {


static const unordered_map<string_view, Token> KEY_WORDS_TOKENS =
{
	{"class"sv, token_type::Class{}},
	{"return"sv, token_type::Return{}},
	{"if"sv, token_type::If{}},
	{"else"sv, token_type::Else{}},
	{"def"sv, token_type::Def{}},
	{"print"sv, token_type::Print{}},
	{"or"sv, token_type::Or{}},
	{"None"sv, token_type::None{}},
	{"and"sv, token_type::And{}},
	{"not"sv, token_type::Not{}},
	{"True"sv, token_type::True{}},
	{"False"sv, token_type::False{}}
};

bool operator==(const Token& lhs, const Token& rhs) {
	using namespace token_type;

	if (lhs.index() != rhs.index()) {
		return false;
	}
	if (lhs.Is<Char>()) {
		return lhs.As<Char>().value == rhs.As<Char>().value;
	}
	if (lhs.Is<Number>()) {
		return lhs.As<Number>().value == rhs.As<Number>().value;
	}
	if (lhs.Is<String>()) {
		return lhs.As<String>().value == rhs.As<String>().value;
	}
	if (lhs.Is<Id>()) {
		return lhs.As<Id>().value == rhs.As<Id>().value;
	}
	return true;
}

bool operator!=(const Token& lhs, const Token& rhs) {
	return !(lhs == rhs);
}

std::ostream& operator<<(std::ostream& os, const Token& rhs) {
	using namespace token_type;

#define VALUED_OUTPUT(type) \
	if (auto p = rhs.TryAs<type>()) return os << #type << '{' << p->value << '}';

	VALUED_OUTPUT(Number);
	VALUED_OUTPUT(Id);
	VALUED_OUTPUT(String);
	VALUED_OUTPUT(Char);

#undef VALUED_OUTPUT

#define UNVALUED_OUTPUT(type) \
	if (rhs.Is<type>()) return os << #type;

	UNVALUED_OUTPUT(Class);
	UNVALUED_OUTPUT(Return);
	UNVALUED_OUTPUT(If);
	UNVALUED_OUTPUT(Else);
	UNVALUED_OUTPUT(Def);
	UNVALUED_OUTPUT(Newline);
	UNVALUED_OUTPUT(Print);
	UNVALUED_OUTPUT(Indent);
	UNVALUED_OUTPUT(Dedent);
	UNVALUED_OUTPUT(And);
	UNVALUED_OUTPUT(Or);
	UNVALUED_OUTPUT(Not);
	UNVALUED_OUTPUT(Eq);
	UNVALUED_OUTPUT(NotEq);
	UNVALUED_OUTPUT(LessOrEq);
	UNVALUED_OUTPUT(GreaterOrEq);
	UNVALUED_OUTPUT(None);
	UNVALUED_OUTPUT(True);
	UNVALUED_OUTPUT(False);
	UNVALUED_OUTPUT(Eof);

#undef UNVALUED_OUTPUT

	return os << "Unknown token :("sv;
}

static bool IsStringOpening(char c) {
	return c == '\'' || c == '"';
}

static bool IsDigit(char c) {
	return '0' <= c && c <= '9';
}

static bool IsIdOpening(char c) {
	return ('A' <= c && c <= 'Z') || ('a' <= c && c <= 'z') || c == '_';
}

static bool IsIdCharacter(char c) {
	return IsIdOpening(c) || IsDigit(c);
}

static Token LoadStringToken(std::istream& input) {
	char open_quote;
	input >> open_quote;
	auto it = std::istreambuf_iterator<char>(input);
	auto end = std::istreambuf_iterator<char>();
	std::string s;
	while (true) {
		if (it == end) {
			throw LexerError("String parsing error");
		}
		const char ch = *it;
		if (ch == open_quote) {
			++it;
			break;
		} else if (ch == '\\') {
			++it;
			if (it == end) {
				throw LexerError("String parsing error");
			}
			const char escaped_char = *(it);
			switch (escaped_char) {
			case 'n':
				s.push_back('\n');
				break;
			case 't':
				s.push_back('\t');
				break;
			case '\'':
				s.push_back('\'');
				break;
			case '"':
				s.push_back('"');
				break;
			default:
				throw LexerError("Unrecognized escape sequence \\"s + escaped_char);
			}
		} else if (ch == '\n' || ch == '\r') {
			throw LexerError("Unexpected end of line"s);
		} else {
			s.push_back(ch);
		}
		++it;
	}
	return Token{ token_type::String{ std::move(s) } };
}

static Token LoadNumberToken(std::istream& input) {
	char c = input.peek();
	std::string s;
	while (IsDigit(c)) {
		c = input.get();
		s.push_back(c);
		c = input.peek();
	}
	return Token{ token_type::Number{ std::stoi(s) } };
}

static Token LoadIdToken(std::istream& input) {
	char c = input.peek();
	std::string s;
	while (IsIdCharacter(c)) {
		c = input.get();
		s.push_back(c);
		c = input.peek();
	}
	if (KEY_WORDS_TOKENS.count(s)) {
		return KEY_WORDS_TOKENS.at(s);
	}
	return Token{ token_type::Id{ std::move(s) } };
}

static Token LoadOperatorsToken(std::istream& input) {
	char c1 = input.get();
	if (c1 == '=' || c1 == '!' || c1 == '>' || c1 == '<') {
		if (char c2 = input.peek(); c2 == '=') {
			input.get();
			switch (c1) {
			case '=':
				return Token{ token_type::Eq{} };
			case '!':
				return Token{ token_type::NotEq{} };
			case '>':
				return Token{ token_type::GreaterOrEq{} };
			case '<':
				return Token{ token_type::LessOrEq{} };
			default:
				assert(false);
				break;
			}
		}
	}
	return	Token{ token_type::Char{ c1 } };
}

// ----- Lexer -----
Lexer::Lexer(std::istream& input)
	: input_(input) {
	while (tokens_.empty() || !tokens_.back().Is<token_type::Eof>()) {
		tokens_.push_back(LoadToken());
	}
}

const Token& Lexer::CurrentToken() const {
	return tokens_.front();
}

Token Lexer::NextToken() {
	if (tokens_.size() > 1) {
		tokens_.pop_front();
	}
	return CurrentToken();
}

void Lexer::CalculateIndent() {
	char c = input_.peek();
	int counter = 0;
	while (c == ' ') {
		input_.get();
		c = input_.peek();
		++counter;
	}
	if (c == '\n') {
		return;
	}
	assert(counter % 2 == 0);
	Token indent = counter / 2 > current_indent_ ? Token{ token_type::Indent{} } : Token{ token_type::Dedent{} };
	for (int i = 0; i < std::abs(counter / 2 - current_indent_); ++i) {
		tokens_.push_back(indent);
	}
	current_indent_ += counter / 2 - current_indent_;
	new_line_ = false;
}


Token Lexer::LoadToken() {
	if (new_line_) {
		CalculateIndent();
	}
	char c = input_.peek();
	while (c == ' ') {
		input_.get();
		c = input_.peek();
	}
	if (c == '#') {
		while (c != '\n' && !input_.eof()) {
			input_.get();
			c = input_.peek();
		}
	}
	c = input_.peek();
	if (c == '\n') {
		input_.get();
		new_line_ = true;
		if (tokens_.empty() || tokens_.back().Is<token_type::Newline>()) {
			return LoadToken();
		} else {
			return Token{ token_type::Newline{} };
		}
	}
	if (input_.eof()) {
		if (tokens_.empty()) {
			return Token{ token_type::Eof{} };
		}
		if (tokens_.back().Is<token_type::Newline>() || tokens_.back().Is<token_type::Dedent>()) {
			return Token{ token_type::Eof{} };
		}
		return Token{ token_type::Newline{} };
	}
	if (IsStringOpening(c)) {
		return LoadStringToken(input_);
	}
	if (IsDigit(c)) {
		return LoadNumberToken(input_);
	}
	if (IsIdOpening(c)) {
		return LoadIdToken(input_);
	}
	return LoadOperatorsToken(input_);
}


}  // namespace parse