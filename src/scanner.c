// External scanner for the morloc grammar.
//
// Morloc's token boundaries depend on preceding tokens (getter dots, unary
// minus, as-patterns, slice colons) and its block structure comes from a
// GHC-style layout pass that inserts virtual braces and semicolons. Neither
// is expressible with tree-sitter's regex lexer, so every token is produced
// here, mirroring Morloc.Frontend.Lexer: lexOne for token boundaries and
// insertLayout for the virtual tokens.

#include <tree_sitter/parser.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// Must match the `externals` array in grammar.js, in order.
enum TokenType {
  VLBRACE,
  VRBRACE,
  VSEMI,
  LPAREN,
  RPAREN,
  LBRACKET,
  RBRACKET,
  LBRACE,
  RBRACE,
  LANGLE,
  RANGLE,
  COMMA,
  BACKSLASH,
  UNDERSCORE,
  BANG,
  QUESTION,
  AT,
  DOT,
  GDOT,
  NSDOT,
  GDOTCHAIN,
  EQUALS,
  DCOLON,
  ARROW,
  FATARROW,
  BIND,
  STAR,
  MINUS,
  PIPE,
  COLON,
  SEMICOLON,
  KW_MODULE,
  KW_IMPORT,
  KW_SOURCE,
  KW_FROM,
  KW_WHERE,
  KW_AS,
  KW_TRUE,
  KW_FALSE,
  KW_TYPE,
  KW_NEWTYPE,
  KW_RECORD,
  KW_OBJECT,
  KW_CLASS,
  KW_INSTANCE,
  KW_EFFECT,
  KW_ESCAPABLE,
  KW_INFIXL,
  KW_INFIXR,
  KW_INFIX,
  KW_LET,
  KW_IN,
  KW_DO,
  KW_NULL,
  KW_EXPORT,
  KW_DATA,
  KW_DERIVING,
  KW_MATCH,
  TOK_INF,
  TOK_NEGINF,
  TOK_NAN,
  LOWER,
  UPPER,
  PLUS,
  SLASH,
  DIAMOND,
  OPERATOR,
  INTEGER,
  FLOAT,
  STRING,
  STRSTART,
  STRMID,
  STREND,
  INTERPOPEN,
  INTERPCLOSE,
  INTRINSIC,
  TICKNAME,
  BACKTICKNAME,
  PRAGMA_INLINE,
  GDOT_AFTER_GROUP,
  COMMENT,
  DOC_COMMENT,
  GROUP_COMMENT,
};

// Internal-only codes, never handed to tree-sitter.
#define TOK_EOF 250
#define TOK_OTHER 251
#define TOK_NONE 252

// Bounds that keep the serialized state within
// TREE_SITTER_SERIALIZATION_BUFFER_SIZE: at most 2 bytes per context, 4
// bracket kinds per byte, 3 bytes per string. Past them the scanner stops,
// so the rest of the file is an error rather than laid out wrongly.
#define MAX_CTX 400
#define MAX_BRK 400
#define MAX_STR 8
#define MAX_COL 0x3FFF

// LayoutContext
#define CTX_INDENT 0
#define CTX_LET 1
#define CTX_EXPLICIT 2
#define CTX_PAREN 3

// Layout driver state.
#define MODE_INIT 0
#define MODE_MODULE_HEADER 1
#define MODE_EXPORT_LIST 2
#define MODE_START_CTX 3
#define MODE_NORMAL 4
#define MODE_DONE 5

// Where the scanner is inside an interpolated string.
#define STR_NONE 0
#define STR_BODY 1
#define STR_EXPECT_INTERP 2

#define SMODE_REGULAR 0
#define SMODE_TRIPLE_DQ 1
#define SMODE_TRIPLE_SQ 2

// Resumption of a multi-step brace close.
// Bracket-stack entry kinds.
#define BRK_PLAIN 0
#define BRK_SLICE 1
#define BRK_GROUP 2

#define CLOSE_NONE 0
#define CLOSE_EXPLICIT 1
#define CLOSE_PAREN 2

typedef struct {
  uint8_t ctx_kind[MAX_CTX];
  uint16_t ctx_col[MAX_CTX];
  uint16_t ctx_len;

  // Per-open-delimiter flag: BRK_SLICE when a '[' opened a slice bracket,
  // BRK_GROUP when a '(' opened a group accessor.
  uint8_t brk[MAX_BRK];
  uint16_t brk_len;

  uint8_t str_mode[MAX_STR];
  uint16_t str_depth[MAX_STR];
  uint8_t str_len;

  uint8_t mode;
  uint8_t start_kw;
  uint16_t hdr_depth;
  uint8_t bypass;
  uint8_t pending_close;
  uint8_t str_state;
  uint8_t skipped_ws;
  uint8_t prev_tok;
  uint8_t prev2_tok;
  uint8_t closed_group;
  // Set after 'do' when a record literal follows it (LEX-4).
  uint8_t do_record;
  // Set when a bound above was exceeded.
  uint8_t overflow;
} Scanner;

typedef struct {
  uint8_t kind;
  uint16_t col;
  uint8_t aux;
  // Leading character already consumed during classification, or 0.
  int32_t pre;
  // True when classification consumed the whole token.
  bool consumed;
} Tok;

static void advance(TSLexer *lexer) { lexer->advance(lexer, false); }
static void skip(TSLexer *lexer) { lexer->advance(lexer, true); }

static bool is_digit(int32_t c) { return c >= '0' && c <= '9'; }

// The compiler classifies characters with Data.Char, which knows all of
// Unicode. Outside ASCII these tables cover the cased letters of Latin,
// Greek, Cyrillic and Armenian; any other character outside the symbol,
// punctuation and combining blocks listed in is_letter_u is an uncased
// letter, which the compiler reads as part of a lower name.
static bool in(int32_t c, int32_t lo, int32_t hi) { return c >= lo && c <= hi; }

// Within a block of case pairs, the upper letter is at the even code point
// (or the odd one, when `odd_upper`).
static bool paired_upper(int32_t c, bool odd_upper) {
  return ((c & 1) == 1) == odd_upper;
}

static bool is_upper_u(int32_t c) {
  if (in(c, 0xC0, 0xD6) || in(c, 0xD8, 0xDE)) return true;
  if (in(c, 0x100, 0x137) || in(c, 0x14A, 0x177)) return paired_upper(c, false);
  if (in(c, 0x139, 0x148) || in(c, 0x179, 0x17E)) return paired_upper(c, true);
  if (c == 0x178) return true;
  if (in(c, 0x200, 0x24F)) return paired_upper(c, false);
  if (c == 0x386 || in(c, 0x388, 0x38A) || c == 0x38C || in(c, 0x38E, 0x38F) ||
      in(c, 0x391, 0x3A1) || in(c, 0x3A3, 0x3AB)) {
    return true;
  }
  if (in(c, 0x400, 0x42F)) return true;
  if (in(c, 0x460, 0x481) || in(c, 0x48A, 0x4BF)) return paired_upper(c, false);
  if (in(c, 0x531, 0x556)) return true;
  if (in(c, 0x1E00, 0x1EFF)) return paired_upper(c, false);
  if (in(c, 0xFF21, 0xFF3A)) return true;
  return false;
}

static bool is_letter_u(int32_t c) {
  if (c < 0xAA) return false;
  if (in(c, 0xAB, 0xB4) || in(c, 0xB6, 0xB9) || in(c, 0xBB, 0xBF)) return false;
  if (c == 0xD7 || c == 0xF7) return false;
  if (in(c, 0x300, 0x36F)) return false;
  if (in(c, 0x2000, 0x2BFF) || in(c, 0x2E00, 0x2E7F)) return false;
  if (in(c, 0x3000, 0x3003) || in(c, 0x3008, 0x3020)) return false;
  if (in(c, 0xFE00, 0xFE4F) || in(c, 0xFF00, 0xFF0F) || in(c, 0xFF1A, 0xFF20) ||
      in(c, 0xFF3B, 0xFF40) || in(c, 0xFF5B, 0xFF65) || in(c, 0xFFF0, 0xFFFF)) {
    return false;
  }
  if (in(c, 0x1F000, 0x1FAFF)) return false;
  return true;
}

static bool is_cased_u(int32_t c) {
  return in(c, 0xC0, 0x24F) || in(c, 0x370, 0x3FF) || in(c, 0x400, 0x52F) ||
         in(c, 0x531, 0x587) || in(c, 0x1E00, 0x1EFF) || in(c, 0xFF21, 0xFF5A) ||
         c == 0xAA || c == 0xB5 || c == 0xBA;
}

// isUpper, isLower, isAlpha
static bool is_upper(int32_t c) {
  if (c < 0x80) return c >= 'A' && c <= 'Z';
  return is_upper_u(c);
}
static bool is_lower(int32_t c) {
  if (c < 0x80) return c >= 'a' && c <= 'z';
  return is_letter_u(c) && is_cased_u(c) && !is_upper_u(c);
}
static bool is_alpha(int32_t c) {
  if (c < 0x80) return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
  return is_letter_u(c);
}
static bool is_alnum(int32_t c) { return is_alpha(c) || is_digit(c); }
static bool is_hex(int32_t c) {
  return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}
static bool is_ident_body(int32_t c) {
  return is_alnum(c) || c == '_' || c == '\'';
}

// isOperatorChar
static bool is_op_char(int32_t c) {
  return c == ':' || c == '!' || c == '$' || c == '%' || c == '&' ||
         c == '*' || c == '+' || c == '.' || c == '/' || c == '<' ||
         c == '=' || c == '>' || c == '?' || c == '@' || c == '\\' ||
         c == '^' || c == '|' || c == '-' || c == '~' || c == '#';
}

// isOperandFinishing
static bool is_operand_finishing(uint8_t k) {
  switch (k) {
    case INTEGER:
    case FLOAT:
    case STRING:
    case STREND:
    case LOWER:
    case UPPER:
    case TICKNAME:
    case INTRINSIC:
    case RPAREN:
    case RBRACKET:
    case RBRACE:
    case RANGLE:
    case KW_TRUE:
    case KW_FALSE:
    case KW_NULL:
    case UNDERSCORE:
      return true;
    default:
      return false;
  }
}

static bool is_block_closer(uint8_t k) {
  return k == KW_MODULE || k == KW_WHERE;
}

static bool is_getter_dot(uint8_t k) { return k == GDOT || k == GDOTCHAIN; }

static void ctx_push(Scanner *s, uint8_t kind, uint16_t col) {
  if (s->ctx_len >= MAX_CTX || col > MAX_COL) {
    s->overflow = 1;
    return;
  }
  s->ctx_kind[s->ctx_len] = kind;
  s->ctx_col[s->ctx_len] = col;
  s->ctx_len++;
}

static void ctx_pop(Scanner *s) {
  if (s->ctx_len > 0) s->ctx_len--;
}

static bool has_paren_ctx(const Scanner *s) {
  for (uint16_t i = 0; i < s->ctx_len; i++) {
    if (s->ctx_kind[i] == CTX_PAREN) return true;
  }
  return false;
}

static void brk_push(Scanner *s, uint8_t slice) {
  if (s->brk_len < MAX_BRK) {
    s->brk[s->brk_len++] = slice;
  } else {
    s->overflow = 1;
  }
}

static void brk_pop(Scanner *s) {
  if (s->brk_len > 0) s->brk_len--;
}

static bool in_slice_bracket(const Scanner *s) {
  return s->brk_len > 0 && s->brk[s->brk_len - 1] == BRK_SLICE;
}

//--------------------------------------------------------------------
// Word and operator classification
//--------------------------------------------------------------------

// classifyWord, for the ASCII letters of a word; the case of its first
// letter is decided by the caller.
static uint8_t classify_word(const char *w, uint32_t n) {
  switch (n) {
    case 2:
      if (!memcmp(w, "as", 2)) return KW_AS;
      if (!memcmp(w, "in", 2)) return KW_IN;
      if (!memcmp(w, "do", 2)) return KW_DO;
      break;
    case 3:
      if (!memcmp(w, "let", 3)) return KW_LET;
      if (!memcmp(w, "Inf", 3)) return TOK_INF;
      if (!memcmp(w, "NaN", 3)) return TOK_NAN;
      break;
    case 4:
      if (!memcmp(w, "from", 4)) return KW_FROM;
      if (!memcmp(w, "True", 4)) return KW_TRUE;
      if (!memcmp(w, "type", 4)) return KW_TYPE;
      if (!memcmp(w, "Null", 4)) return KW_NULL;
      if (!memcmp(w, "data", 4)) return KW_DATA;
      break;
    case 5:
      if (!memcmp(w, "where", 5)) return KW_WHERE;
      if (!memcmp(w, "False", 5)) return KW_FALSE;
      if (!memcmp(w, "class", 5)) return KW_CLASS;
      if (!memcmp(w, "infix", 5)) return KW_INFIX;
      if (!memcmp(w, "match", 5)) return KW_MATCH;
      break;
    case 6:
      if (!memcmp(w, "module", 6)) return KW_MODULE;
      if (!memcmp(w, "import", 6)) return KW_IMPORT;
      if (!memcmp(w, "source", 6)) return KW_SOURCE;
      if (!memcmp(w, "export", 6)) return KW_EXPORT;
      if (!memcmp(w, "record", 6)) return KW_RECORD;
      if (!memcmp(w, "object", 6)) return KW_OBJECT;
      if (!memcmp(w, "effect", 6)) return KW_EFFECT;
      if (!memcmp(w, "infixl", 6)) return KW_INFIXL;
      if (!memcmp(w, "infixr", 6)) return KW_INFIXR;
      break;
    case 7:
      if (!memcmp(w, "newtype", 7)) return KW_NEWTYPE;
      break;
    case 8:
      if (!memcmp(w, "instance", 8)) return KW_INSTANCE;
      if (!memcmp(w, "deriving", 8)) return KW_DERIVING;
      break;
    case 9:
      if (!memcmp(w, "escapable", 9)) return KW_ESCAPABLE;
      break;
    default:
      break;
  }
  return LOWER;
}

// Consume an identifier, whose first character (a letter or '_') is the
// lookahead, and classify it.
static uint8_t read_word(TSLexer *lexer) {
  int32_t first = lexer->lookahead;
  char word[16];
  uint32_t n = 0;
  do {
    int32_t ch = lexer->lookahead;
    // Keywords are ASCII; any other character makes the word a name.
    if (n < sizeof(word)) word[n] = (ch < 0x80) ? (char)ch : 0;
    n++;
    advance(lexer);
  } while (is_ident_body(lexer->lookahead));
  if (n == 1 && first == '_') return UNDERSCORE;
  uint8_t kind = (n <= sizeof(word)) ? classify_word(word, n) : LOWER;
  if (kind == LOWER && is_upper(first)) return UPPER;
  return kind;
}

// classifyOp, plus the '+' and '/' operators the parser matches by text.
static uint8_t classify_op(const char *op, uint32_t n) {
  if (n == 1) {
    switch (op[0]) {
      case '=': return EQUALS;
      case ':': return COLON;
      case '*': return STAR;
      case '-': return MINUS;
      case '|': return PIPE;
      case '<': return LANGLE;
      case '>': return RANGLE;
      case '+': return PLUS;
      case '/': return SLASH;
      default: return OPERATOR;
    }
  }
  if (n == 2) {
    if (!memcmp(op, "::", 2)) return DCOLON;
    if (!memcmp(op, "->", 2)) return ARROW;
    if (!memcmp(op, "=>", 2)) return FATARROW;
    if (!memcmp(op, "<-", 2)) return BIND;
    if (!memcmp(op, "<>", 2)) return DIAMOND;
  }
  return OPERATOR;
}

// Consume a run of operator characters, the first of which may already have
// been consumed and passed in as `first`.
static uint8_t lex_operator_run(TSLexer *lexer, int32_t first) {
  char op[4];
  uint32_t n = 0;
  if (first != 0) {
    op[n++] = (char)first;
  }
  while (is_op_char(lexer->lookahead)) {
    if (n < sizeof(op)) op[n] = (char)lexer->lookahead;
    n++;
    advance(lexer);
  }
  lexer->mark_end(lexer);
  if (n == 0) return OPERATOR;
  return n <= sizeof(op) ? classify_op(op, n) : OPERATOR;
}

//--------------------------------------------------------------------
// Strings
//--------------------------------------------------------------------

// Scan string content up to the next "#{" or the closing delimiter, which is
// consumed. Returns on_interp or on_close accordingly. The opening delimiter
// must already be consumed.
static uint8_t scan_string_content(TSLexer *lexer, uint8_t mode,
                                   uint8_t on_interp, uint8_t on_close) {
  int32_t delim = (mode == SMODE_TRIPLE_SQ) ? '\'' : '"';
  bool triple = (mode != SMODE_REGULAR);
  for (;;) {
    if (lexer->lookahead == 0) {
      lexer->mark_end(lexer);
      return on_close;
    }
    if (lexer->lookahead == '\\') {
      advance(lexer);
      if (lexer->lookahead != 0) advance(lexer);
      continue;
    }
    if (!triple && lexer->lookahead == '\n') {
      // Unterminated single-line string: stop before the newline so the
      // error stays on one line.
      lexer->mark_end(lexer);
      return on_close;
    }
    if (lexer->lookahead == '#') {
      lexer->mark_end(lexer);
      advance(lexer);
      if (lexer->lookahead == '{') return on_interp;
      continue;
    }
    if (lexer->lookahead == delim) {
      advance(lexer);
      if (!triple) {
        lexer->mark_end(lexer);
        return on_close;
      }
      uint32_t run = 1;
      while (lexer->lookahead == delim) {
        advance(lexer);
        run++;
      }
      // A run of three or more closes the string with its last three, so the
      // leading delimiters of the run are content and the token ends here.
      if (run >= 3) {
        lexer->mark_end(lexer);
        return on_close;
      }
      continue;
    }
    advance(lexer);
  }
}

//--------------------------------------------------------------------
// Numbers
//--------------------------------------------------------------------

// Consume a scientific-notation exponent whose 'e' or 'E' is unconsumed.
// Returns true when the exponent was well formed.
static bool scan_exponent(TSLexer *lexer) {
  advance(lexer);
  if (lexer->lookahead == '+' || lexer->lookahead == '-') {
    advance(lexer);
    if (!is_digit(lexer->lookahead)) return false;
  } else if (!is_digit(lexer->lookahead)) {
    return false;
  }
  while (is_digit(lexer->lookahead)) advance(lexer);
  lexer->mark_end(lexer);
  return true;
}

// Lex a numeric literal starting at a digit. With integer_only the literal
// stops at the first non-digit, matching the compiler's ".<digits>" getter
// rule, which forbids a float from forming after a getter dot.
static uint8_t lex_number(TSLexer *lexer, bool integer_only) {
  if (!integer_only && lexer->lookahead == '0') {
    advance(lexer);
    int32_t c = lexer->lookahead;
    if (c == 'x' || c == 'X' || c == 'o' || c == 'O' || c == 'b' || c == 'B') {
      advance(lexer);
      if (c == 'x' || c == 'X') {
        while (is_hex(lexer->lookahead)) advance(lexer);
      } else {
        while (is_digit(lexer->lookahead)) advance(lexer);
      }
      // A trailing alphanumeric run makes the literal malformed; absorb it
      // so the error lands on one node rather than two.
      while (is_alnum(lexer->lookahead) || lexer->lookahead == '_') {
        advance(lexer);
      }
      lexer->mark_end(lexer);
      return INTEGER;
    }
  }
  while (is_digit(lexer->lookahead)) advance(lexer);
  lexer->mark_end(lexer);
  if (integer_only) return INTEGER;

  if (lexer->lookahead == '.') {
    advance(lexer);
    if (is_digit(lexer->lookahead)) {
      while (is_digit(lexer->lookahead)) advance(lexer);
      lexer->mark_end(lexer);
      if (lexer->lookahead == 'e' || lexer->lookahead == 'E') {
        scan_exponent(lexer);
      }
      return FLOAT;
    }
    // The '.' starts the next token; mark_end already excludes it.
    return INTEGER;
  }
  if (lexer->lookahead == 'e' || lexer->lookahead == 'E') {
    if (scan_exponent(lexer)) return FLOAT;
    return INTEGER;
  }
  return INTEGER;
}

//--------------------------------------------------------------------
// Dash
//--------------------------------------------------------------------

// Lex the token that begins with a '-' that has already been consumed.
// Comments are handled before this point, so "--" cannot reach here.
static uint8_t lex_dash_rest(Scanner *s, TSLexer *lexer) {
  // isUnaryDashPosition
  bool unary = s->skipped_ws || s->prev_tok == TOK_NONE ||
               !is_operand_finishing(s->prev_tok);
  if (unary && is_digit(lexer->lookahead)) {
    return lex_number(lexer, false);
  }
  if (unary && (lexer->lookahead == 'I' || lexer->lookahead == 'N')) {
    const char *want = (lexer->lookahead == 'I') ? "Inf" : "NaN";
    uint8_t result = (lexer->lookahead == 'I') ? TOK_NEGINF : TOK_NAN;
    lexer->mark_end(lexer);
    uint32_t i = 0;
    while (i < 3 && lexer->lookahead == (int32_t)want[i]) {
      advance(lexer);
      i++;
    }
    if (i == 3 && !is_ident_body(lexer->lookahead)) {
      lexer->mark_end(lexer);
      return result;
    }
    // Not a non-finite literal: the name is a separate token, and mark_end
    // still sits right after the dash.
    return MINUS;
  }
  lexer->mark_end(lexer);
  if (!is_op_char(lexer->lookahead)) return MINUS;
  return lex_operator_run(lexer, '-');
}

//--------------------------------------------------------------------
// Full lexing
//--------------------------------------------------------------------

static uint8_t lex_full(Scanner *s, TSLexer *lexer, Tok *t) {
  if (t->pre == '-') return lex_dash_rest(s, lexer);

  // String continuations run first: whitespace inside a string is content.
  if (s->str_state == STR_EXPECT_INTERP) {
    advance(lexer);
    advance(lexer);
    lexer->mark_end(lexer);
    return INTERPOPEN;
  }
  if (s->str_state == STR_BODY) {
    uint8_t mode = s->str_len > 0 ? s->str_mode[s->str_len - 1] : SMODE_REGULAR;
    return scan_string_content(lexer, mode, STRMID, STREND);
  }

  int32_t c = lexer->lookahead;

  // Inside an interpolation splice, the '}' that brings brace depth back to
  // zero closes the splice rather than a record literal.
  if (s->str_len > 0 && c == '}' && s->str_depth[s->str_len - 1] == 0) {
    advance(lexer);
    lexer->mark_end(lexer);
    return INTERPCLOSE;
  }

  switch (c) {
    case '(':
      advance(lexer);
      lexer->mark_end(lexer);
      // A '(' straight after a getter dot opens a group accessor.
      t->aux = is_getter_dot(s->prev_tok) ? BRK_GROUP : BRK_PLAIN;
      return LPAREN;
    case ')': advance(lexer); lexer->mark_end(lexer); return RPAREN;
    case ']': advance(lexer); lexer->mark_end(lexer); return RBRACKET;
    case '}': advance(lexer); lexer->mark_end(lexer); return RBRACE;
    case ',': advance(lexer); lexer->mark_end(lexer); return COMMA;
    case ';': advance(lexer); lexer->mark_end(lexer); return SEMICOLON;
    case '\\': advance(lexer); lexer->mark_end(lexer); return BACKSLASH;
    case '[':
      advance(lexer);
      lexer->mark_end(lexer);
      // A '[' straight after a getter dot opens a slice bracket.
      t->aux = is_getter_dot(s->prev_tok) ? BRK_SLICE : BRK_PLAIN;
      return LBRACKET;
    default:
      break;
  }

  if (c == '\'') {
    advance(lexer);
    if (lexer->lookahead == '\'') {
      advance(lexer);
      if (lexer->lookahead == '\'') {
        advance(lexer);
        t->aux = SMODE_TRIPLE_SQ;
        return scan_string_content(lexer, SMODE_TRIPLE_SQ, STRSTART, STRING);
      }
      lexer->mark_end(lexer);
      return OPERATOR;
    }
    if (is_alpha(lexer->lookahead) || lexer->lookahead == '_') {
      while (is_ident_body(lexer->lookahead)) advance(lexer);
      lexer->mark_end(lexer);
      return TICKNAME;
    }
    lexer->mark_end(lexer);
    return OPERATOR;
  }

  if (c == '`') {
    advance(lexer);
    while (lexer->lookahead != 0 && lexer->lookahead != '`' &&
           lexer->lookahead != '\n') {
      advance(lexer);
    }
    if (lexer->lookahead == '`') advance(lexer);
    lexer->mark_end(lexer);
    return BACKTICKNAME;
  }

  if (c == '"') {
    advance(lexer);
    if (lexer->lookahead == '"') {
      advance(lexer);
      if (lexer->lookahead == '"') {
        advance(lexer);
        t->aux = SMODE_TRIPLE_DQ;
        return scan_string_content(lexer, SMODE_TRIPLE_DQ, STRSTART, STRING);
      }
      lexer->mark_end(lexer);
      return STRING;
    }
    t->aux = SMODE_REGULAR;
    return scan_string_content(lexer, SMODE_REGULAR, STRSTART, STRING);
  }

  if (is_digit(c)) {
    bool after_getter = is_getter_dot(s->prev_tok) && !s->skipped_ws;
    return lex_number(lexer, after_getter);
  }

  if (c == '.') {
    bool glued = !s->skipped_ws;
    advance(lexer);
    int32_t next = lexer->lookahead;
    // A group accessor has no tail slot, so '.(.a,.b).0' is ambiguous and
    // the compiler rejects it outright.
    if (glued && s->closed_group &&
        (is_digit(next) || is_lower(next) || next == '(' || next == '[')) {
      lexer->mark_end(lexer);
      return GDOT_AFTER_GROUP;
    }
    if (is_digit(next)) {
      lexer->mark_end(lexer);
      return glued ? GDOTCHAIN : GDOT;
    }
    // A dot glued to a lowercase name is a namespace separator, unless that
    // name was itself reached through a getter dot. Its tail is a term
    // (`f.map`) or a constructor (`p.Red`).
    if (is_alpha(next) && glued && s->prev_tok == LOWER &&
        !is_getter_dot(s->prev2_tok)) {
      lexer->mark_end(lexer);
      return NSDOT;
    }
    if (is_lower(next) || next == '(' || next == '[') {
      lexer->mark_end(lexer);
      return glued ? GDOTCHAIN : GDOT;
    }
    if (!is_op_char(next)) {
      lexer->mark_end(lexer);
      return DOT;
    }
    return lex_operator_run(lexer, '.');
  }

  if (c == '!') {
    advance(lexer);
    if (lexer->lookahead == '!') {
      // "!!" is two eval-sugar bangs unless a third operator char follows.
      lexer->mark_end(lexer);
      advance(lexer);
      if (!is_op_char(lexer->lookahead)) return BANG;
      while (is_op_char(lexer->lookahead)) advance(lexer);
      lexer->mark_end(lexer);
      return OPERATOR;
    }
    if (!is_op_char(lexer->lookahead)) {
      lexer->mark_end(lexer);
      return BANG;
    }
    return lex_operator_run(lexer, '!');
  }

  if (c == '?') {
    advance(lexer);
    if (!is_op_char(lexer->lookahead)) {
      lexer->mark_end(lexer);
      return QUESTION;
    }
    return lex_operator_run(lexer, '?');
  }

  if (c == '%') {
    advance(lexer);
    const char *want = "inline";
    uint32_t i = 0;
    while (i < 6 && lexer->lookahead == (int32_t)want[i]) {
      advance(lexer);
      i++;
    }
    if (i == 6 && !is_ident_body(lexer->lookahead)) {
      lexer->mark_end(lexer);
      return PRAGMA_INLINE;
    }
    return lex_operator_run(lexer, '%');
  }

  if (c == '@') {
    bool glued = !s->skipped_ws;
    advance(lexer);
    // Glued to an operand, '@' binds an as-pattern; otherwise '@name' names
    // an intrinsic.
    if (glued && is_operand_finishing(s->prev_tok)) {
      lexer->mark_end(lexer);
      return AT;
    }
    if (is_lower(lexer->lookahead)) {
      while (is_ident_body(lexer->lookahead)) advance(lexer);
      lexer->mark_end(lexer);
      return INTRINSIC;
    }
    return lex_operator_run(lexer, '@');
  }

  // Inside a slice bracket every ':' is a single token, so that .[::-1] and
  // friends do not coalesce into '::' or ':-'.
  if (c == ':' && in_slice_bracket(s)) {
    advance(lexer);
    lexer->mark_end(lexer);
    return COLON;
  }

  if (is_op_char(c)) {
    return lex_operator_run(lexer, 0);
  }

  if (is_alpha(c) || c == '_') {
    uint8_t kind = read_word(lexer);
    lexer->mark_end(lexer);
    return kind;
  }

  // Unknown character: consume it so the parser reports an error and the
  // scanner still makes progress.
  advance(lexer);
  lexer->mark_end(lexer);
  return OPERATOR;
}

//--------------------------------------------------------------------
// Commit
//--------------------------------------------------------------------

// Apply the lexer-level state a real token carries. Layout-context pushes
// belong to emit_token, mirroring the split between lexOne and emitToken.
static void commit(Scanner *s, uint8_t kind, uint8_t aux) {
  if (s->str_len > 0 && s->str_state == STR_NONE) {
    if (kind == LBRACE) {
      s->str_depth[s->str_len - 1]++;
    } else if (kind == RBRACE && s->str_depth[s->str_len - 1] > 0) {
      s->str_depth[s->str_len - 1]--;
    }
  }

  uint8_t closed_group = 0;
  switch (kind) {
    case LBRACE:
      brk_push(s, BRK_PLAIN);
      break;
    case LPAREN:
    case LBRACKET:
      brk_push(s, aux);
      break;
    case RPAREN:
      closed_group = (s->brk_len > 0 && s->brk[s->brk_len - 1] == BRK_GROUP);
      brk_pop(s);
      break;
    case RBRACKET:
    case RBRACE:
      brk_pop(s);
      break;
    case STRSTART:
      if (s->str_len < MAX_STR) {
        s->str_mode[s->str_len] = aux;
        s->str_depth[s->str_len] = 0;
        s->str_len++;
      } else {
        s->overflow = 1;
      }
      s->str_state = STR_EXPECT_INTERP;
      break;
    case STRMID:
      s->str_state = STR_EXPECT_INTERP;
      break;
    case INTERPOPEN:
      s->str_state = STR_NONE;
      if (s->str_len > 0) s->str_depth[s->str_len - 1] = 0;
      brk_push(s, BRK_PLAIN);
      break;
    case INTERPCLOSE:
      s->str_state = STR_BODY;
      brk_pop(s);
      break;
    case STREND:
      if (s->str_len > 0) s->str_len--;
      s->str_state = STR_NONE;
      break;
    default:
      break;
  }

  s->closed_group = closed_group;
  s->prev2_tok = s->prev_tok;
  s->prev_tok = kind;
  s->skipped_ws = 0;
}

//--------------------------------------------------------------------
// Layout driver
//--------------------------------------------------------------------

typedef struct {
  Scanner *s;
  TSLexer *lexer;
  const bool *valid;
  Tok tok;
} Ctx;

// Finish lexing the token if classification did not, apply its state, and
// hand the symbol to tree-sitter.
static bool emit_real(Ctx *c) {
  uint8_t kind;
  if (c->tok.consumed) {
    c->lexer->mark_end(c->lexer);
    kind = c->tok.kind;
  } else {
    kind = lex_full(c->s, c->lexer, &c->tok);
  }
  commit(c->s, kind, c->tok.aux);
  c->lexer->result_symbol = kind;
  return true;
}

// recordLiteralAhead: after 'do', '{' then a lowercase name and '='. The
// token before has been marked, so the look ahead consumes nothing.
static bool record_literal_ahead(TSLexer *lexer) {
  for (;;) {
    int32_t ch = lexer->lookahead;
    if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
      advance(lexer);
    } else {
      break;
    }
  }
  if (lexer->lookahead != '{') return false;
  advance(lexer);
  if (lexer->lookahead == '-') return false;
  for (;;) {
    int32_t ch = lexer->lookahead;
    if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
      advance(lexer);
    } else {
      break;
    }
  }
  if (!is_alpha(lexer->lookahead) && lexer->lookahead != '_') return false;
  if (read_word(lexer) != LOWER) return false;
  while (lexer->lookahead == ' ' || lexer->lookahead == '\t' ||
         lexer->lookahead == '\r' || lexer->lookahead == '\n') {
    advance(lexer);
  }
  if (lexer->lookahead != '=') return false;
  advance(lexer);
  return !is_op_char(lexer->lookahead);
}

// closingBraces
static bool close_all(Ctx *c) {
  Scanner *s = c->s;
  while (s->ctx_len > 0) {
    uint8_t k = s->ctx_kind[s->ctx_len - 1];
    ctx_pop(s);
    // An unbalanced '(' or '[' gets no virtual close brace.
    if (k == CTX_PAREN) continue;
    c->lexer->result_symbol = VRBRACE;
    return true;
  }
  s->mode = MODE_DONE;
  return false;
}

static bool close_to_explicit(Ctx *c);

// emitToken
static bool emit_token(Ctx *c) {
  Scanner *s = c->s;
  if (c->tok.kind == TOK_EOF) return close_all(c);
  // An explicit '}' closes every layout block opened inside its '{', as the
  // '}' ending a string's hole closes every block opened inside the hole.
  if (c->tok.kind == RBRACE || c->tok.kind == INTERPCLOSE) {
    return close_to_explicit(c);
  }

  uint16_t col = c->tok.col;
  emit_real(c);
  uint8_t kind = (uint8_t)c->lexer->result_symbol;

  switch (kind) {
    case KW_MODULE:
      s->mode = MODE_MODULE_HEADER;
      break;
    case KW_DO:
      s->do_record = record_literal_ahead(c->lexer);
      s->mode = MODE_START_CTX;
      s->start_kw = kind;
      break;
    case KW_WHERE:
    case KW_LET:
      s->mode = MODE_START_CTX;
      s->start_kw = kind;
      break;
    case LBRACE:
    case INTERPOPEN:
      ctx_push(s, CTX_EXPLICIT, col);
      break;
    case LPAREN:
    case LBRACKET:
      ctx_push(s, CTX_PAREN, col);
      break;
    case PRAGMA_INLINE:
      // The pragma prefixes a declaration; no VSEMI may split them.
      s->bypass = 1;
      break;
    default:
      break;
  }
  return true;
}

// closeToExplicit
static bool close_to_explicit(Ctx *c) {
  Scanner *s = c->s;
  for (;;) {
    if (s->ctx_len == 0) {
      s->pending_close = CLOSE_NONE;
      return emit_real(c);
    }
    uint8_t k = s->ctx_kind[s->ctx_len - 1];
    if (k == CTX_EXPLICIT) {
      ctx_pop(s);
      s->pending_close = CLOSE_NONE;
      return emit_real(c);
    }
    if (k == CTX_PAREN) {
      ctx_pop(s);
      continue;
    }
    ctx_pop(s);
    s->pending_close = CLOSE_EXPLICIT;
    c->lexer->result_symbol = VRBRACE;
    return true;
  }
}

// closeToParen
static bool close_to_paren(Ctx *c) {
  Scanner *s = c->s;
  for (;;) {
    if (s->ctx_len == 0) {
      s->pending_close = CLOSE_NONE;
      return emit_real(c);
    }
    uint8_t k = s->ctx_kind[s->ctx_len - 1];
    if (k == CTX_PAREN || k == CTX_EXPLICIT) {
      ctx_pop(s);
      s->pending_close = CLOSE_NONE;
      return emit_real(c);
    }
    ctx_pop(s);
    s->pending_close = CLOSE_PAREN;
    c->lexer->result_symbol = VRBRACE;
    return true;
  }
}

// indentCheck
static bool indent_check(Ctx *c) {
  Scanner *s = c->s;
  if (s->ctx_len == 0) return emit_token(c);
  uint8_t k = s->ctx_kind[s->ctx_len - 1];
  if (k == CTX_EXPLICIT || k == CTX_PAREN) return emit_token(c);

  uint16_t n = s->ctx_col[s->ctx_len - 1];
  uint8_t kind = c->tok.kind;

  // 'in' and ';' end a let block regardless of indentation.
  if (k == CTX_LET && kind == KW_IN) {
    ctx_pop(s);
    s->bypass = 1;
    c->lexer->result_symbol = VRBRACE;
    return true;
  }
  if (k == CTX_LET && kind == SEMICOLON) {
    ctx_pop(s);
    c->lexer->result_symbol = VRBRACE;
    return true;
  }
  if (c->tok.col == n && is_block_closer(kind)) {
    ctx_pop(s);
    c->lexer->result_symbol = VRBRACE;
    return true;
  }
  if (c->tok.col == n) {
    s->bypass = 1;
    c->lexer->result_symbol = VSEMI;
    return true;
  }
  if (c->tok.col > n) {
    // LEX-4: an implicit block other than the module body ends before a
    // token that cannot continue it.
    if (s->ctx_len >= 2 && c->tok.kind != TOK_OTHER && c->tok.kind != TOK_EOF &&
        c->valid[VRBRACE] && !c->valid[c->tok.kind]) {
      ctx_pop(s);
      c->lexer->result_symbol = VRBRACE;
      return true;
    }
    return emit_token(c);
  }
  ctx_pop(s);
  c->lexer->result_symbol = VRBRACE;
  return true;
}

// startLayoutCtx
static bool start_layout_ctx(Ctx *c) {
  Scanner *s = c->s;
  // An explicit brace after a layout keyword opens a non-indented block,
  // unless it opens a record literal after 'do' (LEX-4).
  bool do_record = s->start_kw == KW_DO && s->do_record;
  s->do_record = 0;
  if (c->tok.kind == LBRACE && !do_record) {
    s->mode = MODE_NORMAL;
    uint16_t col = c->tok.col;
    emit_real(c);
    ctx_push(s, CTX_EXPLICIT, col);
    return true;
  }
  uint8_t kind = (s->start_kw == KW_LET) ? CTX_LET : CTX_INDENT;
  uint16_t col = (c->tok.kind == TOK_EOF) ? 0 : c->tok.col;
  ctx_push(s, kind, col);
  s->mode = MODE_NORMAL;
  // The token that opens a block never takes a VSEMI. A 'module' keyword is
  // the exception: it closes the empty block it would otherwise open.
  s->bypass = (c->tok.kind == KW_MODULE) ? 0 : 1;
  c->lexer->result_symbol = VLBRACE;
  return true;
}

//--------------------------------------------------------------------
// Entry point
//--------------------------------------------------------------------

bool tree_sitter_morloc_external_scanner_scan(void *payload, TSLexer *lexer,
                                              const bool *valid_symbols) {
  Scanner *s = (Scanner *)payload;
  // Every token comes from this scanner, so the parser's expectations never
  // change what is lexed; an unexpected token is reported by the parser.
  // They decide only where an indented block ends early (LEX-4).
  if (s->mode == MODE_DONE || s->overflow) return false;

  Ctx c;
  c.s = s;
  c.lexer = lexer;
  c.valid = valid_symbols;
  c.tok.kind = TOK_OTHER;
  c.tok.aux = 0;
  c.tok.pre = 0;
  c.tok.consumed = false;

  if (s->str_state == STR_NONE) {
    for (;;) {
      int32_t ch = lexer->lookahead;
      if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
        s->skipped_ws = 1;
        skip(lexer);
        continue;
      }
      break;
    }
  }

  // Anchor for zero-width virtual tokens. Everything below may look ahead
  // freely; only mark_end moves the token's end.
  lexer->mark_end(lexer);
  c.tok.col = (uint16_t)lexer->get_column(lexer);

  if (s->str_state == STR_EXPECT_INTERP) {
    c.tok.kind = INTERPOPEN;
  } else if (s->str_state == STR_BODY) {
    // The rest of a string after a hole; lex_full decides mid or end.
    c.tok.kind = STRMID;
  } else {
    int32_t ch = lexer->lookahead;
    if (ch == 0) {
      c.tok.kind = TOK_EOF;
    } else if (ch == '-') {
      advance(lexer);
      if (lexer->lookahead == '-') {
        // Comments never reach the layout pass, exactly as in the compiler,
        // where they are filtered before insertLayout runs.
        advance(lexer);
        uint8_t kind = COMMENT;
        if (lexer->lookahead == '\'') {
          kind = DOC_COMMENT;
          advance(lexer);
        } else if (lexer->lookahead == '*') {
          kind = GROUP_COMMENT;
          advance(lexer);
        }
        while (lexer->lookahead != 0 && lexer->lookahead != '\n') {
          advance(lexer);
        }
        lexer->mark_end(lexer);
        s->skipped_ws = 1;
        lexer->result_symbol = kind;
        return true;
      }
      c.tok.pre = '-';
      c.tok.kind = TOK_OTHER;
    } else if (ch == '{') {
      advance(lexer);
      if (lexer->lookahead == '-') {
        advance(lexer);
        uint32_t depth = 1;
        while (depth > 0 && lexer->lookahead != 0) {
          if (lexer->lookahead == '{') {
            advance(lexer);
            if (lexer->lookahead == '-') {
              advance(lexer);
              depth++;
            }
          } else if (lexer->lookahead == '-') {
            advance(lexer);
            if (lexer->lookahead == '}') {
              advance(lexer);
              depth--;
            }
          } else {
            advance(lexer);
          }
        }
        lexer->mark_end(lexer);
        s->skipped_ws = 1;
        lexer->result_symbol = COMMENT;
        return true;
      }
      c.tok.kind = LBRACE;
      c.tok.consumed = true;
    } else {
      switch (ch) {
        case '(': c.tok.kind = LPAREN; break;
        case ')': c.tok.kind = RPAREN; break;
        case '[': c.tok.kind = LBRACKET; break;
        case ']': c.tok.kind = RBRACKET; break;
        case ';': c.tok.kind = SEMICOLON; break;
        case ',': c.tok.kind = COMMA; break;
        case '}':
          // The '}' that ends an interpolation splice is its own token, not
          // a layout brace.
          c.tok.kind = (s->str_len > 0 && s->str_depth[s->str_len - 1] == 0)
                           ? INTERPCLOSE
                           : RBRACE;
          break;
        default: c.tok.kind = TOK_OTHER; break;
      }
      // Identifiers are the only multi-character tokens the layout pass must
      // recognise. Their extent is exact, so consuming one here cannot
      // disturb the anchor a virtual token needs.
      if (is_alpha(ch) || ch == '_') {
        c.tok.kind = read_word(lexer);
        c.tok.consumed = true;
      } else if (is_op_char(ch) && ch != '.' && ch != '!' && ch != '?' &&
                 ch != '%' && ch != '@' && ch != '\\' &&
                 !(ch == ':' && in_slice_bracket(s))) {
        // A plain operator run, classified so the layout pass knows it.
        char op[4];
        uint32_t n = 0;
        while (is_op_char(lexer->lookahead)) {
          if (n < sizeof(op)) op[n] = (char)lexer->lookahead;
          n++;
          advance(lexer);
        }
        c.tok.consumed = true;
        c.tok.kind = n <= sizeof(op) ? classify_op(op, n) : OPERATOR;
      }
    }
  }

  switch (s->mode) {
    case MODE_INIT:
      // A file that opens with 'module' has its header skipped; anything
      // else is an implicit main whose body starts at the first token.
      if (c.tok.kind == KW_MODULE) {
        emit_real(&c);
        s->mode = MODE_MODULE_HEADER;
        return true;
      }
      s->mode = MODE_START_CTX;
      s->start_kw = KW_MODULE;
      return start_layout_ctx(&c);

    case MODE_MODULE_HEADER:
      if (c.tok.kind == TOK_EOF) return close_all(&c);
      emit_real(&c);
      if (c.lexer->result_symbol == LPAREN) {
        s->mode = MODE_EXPORT_LIST;
        s->hdr_depth = 1;
      }
      return true;

    case MODE_EXPORT_LIST:
      if (c.tok.kind == TOK_EOF) return close_all(&c);
      emit_real(&c);
      if (c.lexer->result_symbol == LPAREN) {
        s->hdr_depth++;
      } else if (c.lexer->result_symbol == RPAREN) {
        if (s->hdr_depth <= 1) {
          s->mode = MODE_START_CTX;
          s->start_kw = KW_MODULE;
        } else {
          s->hdr_depth--;
        }
      }
      return true;

    case MODE_START_CTX:
      return start_layout_ctx(&c);

    default:
      break;
  }

  if (s->pending_close == CLOSE_EXPLICIT) return close_to_explicit(&c);
  if (s->pending_close == CLOSE_PAREN) return close_to_paren(&c);
  if (s->bypass) {
    s->bypass = 0;
    return emit_token(&c);
  }
  if (c.tok.kind == TOK_EOF) return close_all(&c);
  // A string's hole is an explicit block, and the rest of the string after
  // it continues the string whatever its column.
  if (c.tok.kind == INTERPCLOSE) return close_to_explicit(&c);
  if (c.tok.kind == INTERPOPEN || c.tok.kind == STRMID) return emit_token(&c);
  // A closing bracket force-closes any layout block opened inside it.
  if ((c.tok.kind == RPAREN || c.tok.kind == RBRACKET) && has_paren_ctx(s)) {
    return close_to_paren(&c);
  }
  return indent_check(&c);
}

//--------------------------------------------------------------------
// Lifecycle
//--------------------------------------------------------------------

static void scanner_reset(Scanner *s) {
  memset(s, 0, sizeof(Scanner));
  s->mode = MODE_INIT;
  s->prev_tok = TOK_NONE;
  s->prev2_tok = TOK_NONE;
  s->skipped_ws = 1;
}

void *tree_sitter_morloc_external_scanner_create(void) {
  Scanner *s = (Scanner *)malloc(sizeof(Scanner));
  if (s) scanner_reset(s);
  return s;
}

void tree_sitter_morloc_external_scanner_destroy(void *payload) {
  free(payload);
}

// A context is one byte, its kind in the top two bits; an indentation
// context adds the low byte of its column to the six high bits held there.
unsigned tree_sitter_morloc_external_scanner_serialize(void *payload,
                                                       char *buffer) {
  Scanner *s = (Scanner *)payload;
  unsigned i = 0;

  buffer[i++] = (char)(s->ctx_len & 0xFF);
  buffer[i++] = (char)(s->ctx_len >> 8);
  for (uint16_t j = 0; j < s->ctx_len; j++) {
    uint8_t k = s->ctx_kind[j];
    if (k == CTX_INDENT || k == CTX_LET) {
      buffer[i++] = (char)((k << 6) | ((s->ctx_col[j] >> 8) & 0x3F));
      buffer[i++] = (char)(s->ctx_col[j] & 0xFF);
    } else {
      buffer[i++] = (char)(k << 6);
    }
  }
  buffer[i++] = (char)(s->brk_len & 0xFF);
  buffer[i++] = (char)(s->brk_len >> 8);
  for (uint16_t j = 0; j < s->brk_len; j += 4) {
    uint8_t b = 0;
    for (uint16_t q = 0; q < 4 && j + q < s->brk_len; q++) {
      b |= (uint8_t)(s->brk[j + q] << (2 * q));
    }
    buffer[i++] = (char)b;
  }
  buffer[i++] = (char)s->str_len;
  for (uint8_t j = 0; j < s->str_len; j++) {
    buffer[i++] = (char)s->str_mode[j];
    buffer[i++] = (char)(s->str_depth[j] & 0xFF);
    buffer[i++] = (char)((s->str_depth[j] >> 8) & 0xFF);
  }
  buffer[i++] = (char)s->mode;
  buffer[i++] = (char)s->start_kw;
  buffer[i++] = (char)(s->hdr_depth & 0xFF);
  buffer[i++] = (char)((s->hdr_depth >> 8) & 0xFF);
  buffer[i++] = (char)s->bypass;
  buffer[i++] = (char)s->pending_close;
  buffer[i++] = (char)s->str_state;
  buffer[i++] = (char)s->skipped_ws;
  buffer[i++] = (char)s->prev_tok;
  buffer[i++] = (char)s->prev2_tok;
  buffer[i++] = (char)s->closed_group;
  buffer[i++] = (char)s->do_record;
  buffer[i++] = (char)s->overflow;
  return i;
}

void tree_sitter_morloc_external_scanner_deserialize(void *payload,
                                                      const char *buffer,
                                                      unsigned length) {
  Scanner *s = (Scanner *)payload;
  scanner_reset(s);
  if (length == 0) return;

  unsigned i = 0;
  uint16_t ctx_len = (uint16_t)((uint8_t)buffer[i] | ((uint8_t)buffer[i + 1] << 8));
  i += 2;
  s->ctx_len = ctx_len > MAX_CTX ? MAX_CTX : ctx_len;
  for (uint16_t j = 0; j < s->ctx_len; j++) {
    uint8_t b = (uint8_t)buffer[i++];
    uint8_t k = b >> 6;
    s->ctx_kind[j] = k;
    s->ctx_col[j] = 0;
    if (k == CTX_INDENT || k == CTX_LET) {
      s->ctx_col[j] = (uint16_t)(((b & 0x3F) << 8) | (uint8_t)buffer[i++]);
    }
  }
  uint16_t brk_len = (uint16_t)((uint8_t)buffer[i] | ((uint8_t)buffer[i + 1] << 8));
  i += 2;
  s->brk_len = brk_len > MAX_BRK ? MAX_BRK : brk_len;
  for (uint16_t j = 0; j < s->brk_len; j += 4) {
    uint8_t b = (uint8_t)buffer[i++];
    for (uint16_t q = 0; q < 4 && j + q < s->brk_len; q++) {
      s->brk[j + q] = (uint8_t)((b >> (2 * q)) & 0x3);
    }
  }
  s->str_len = (uint8_t)buffer[i++];
  if (s->str_len > MAX_STR) s->str_len = MAX_STR;
  for (uint8_t j = 0; j < s->str_len; j++) {
    s->str_mode[j] = (uint8_t)buffer[i++];
    uint16_t lo = (uint8_t)buffer[i++];
    uint16_t hi = (uint8_t)buffer[i++];
    s->str_depth[j] = (uint16_t)(lo | (hi << 8));
  }
  s->mode = (uint8_t)buffer[i++];
  s->start_kw = (uint8_t)buffer[i++];
  uint16_t dlo = (uint8_t)buffer[i++];
  uint16_t dhi = (uint8_t)buffer[i++];
  s->hdr_depth = (uint16_t)(dlo | (dhi << 8));
  s->bypass = (uint8_t)buffer[i++];
  s->pending_close = (uint8_t)buffer[i++];
  s->str_state = (uint8_t)buffer[i++];
  s->skipped_ws = (uint8_t)buffer[i++];
  s->prev_tok = (uint8_t)buffer[i++];
  s->prev2_tok = (uint8_t)buffer[i++];
  s->closed_group = (uint8_t)buffer[i++];
  s->do_record = (uint8_t)buffer[i++];
  s->overflow = (uint8_t)buffer[i++];
}
