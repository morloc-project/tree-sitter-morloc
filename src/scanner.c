// scanner.c
#include <tree_sitter/parser.h>
#include <wctype.h>
#include <string.h>
#include <stdbool.h>

#define MAX_INDENT 127

// String modes
#define SMODE_REGULAR      0
#define SMODE_TRIPLE_DQ    1  // """..."""
#define SMODE_TRIPLE_SQ    2  // '''...'''

enum TokenType {
  INDENT,
  DEDENT,
  LEFT_ALIGNED,
  NEWLINE,
  GDOT,
  STRING_START,
  STRING_MID,
  STRING_END,
  INTERP_OPEN,
  INTERP_CLOSE,
  STRING_CONTENT,
};

typedef struct {
  uint32_t indents[MAX_INDENT];
  uint32_t level;
  uint32_t dedents;
  bool in_whitespace;
  // String interpolation state: depth > 0 means inside a string
  uint32_t string_depth;
  uint32_t interp_level;
  // 0=regular "...", 1=triple-double """...""", 2=triple-single '''...'''
  uint8_t string_mode;
} Scanner;

static void advance(TSLexer *lexer) {
  lexer->advance(lexer, false);
}

static void skip(TSLexer *lexer) {
  lexer->advance(lexer, true);
}

static bool is_lower(int32_t c) {
  return c >= 'a' && c <= 'z';
}

static bool is_digit(int32_t c) {
  return c >= '0' && c <= '9';
}

// Scan content for regular strings (with interpolation support).
static bool scan_regular_string_content(Scanner *scanner, TSLexer *lexer, const bool *valid_symbols) {
  bool has_content = false;

  while (true) {
    if (lexer->lookahead == '\0') {
      if (has_content && valid_symbols[STRING_CONTENT]) {
        lexer->result_symbol = STRING_CONTENT;
        return true;
      }
      return false;
    }

    if (lexer->lookahead == '\\') {
      has_content = true;
      advance(lexer);
      if (lexer->lookahead != '\0') {
        advance(lexer);
      }
      continue;
    }

    if (lexer->lookahead == '#') {
      lexer->mark_end(lexer);
      advance(lexer);
      if (lexer->lookahead == '{') {
        if (has_content && valid_symbols[STRING_CONTENT]) {
          lexer->result_symbol = STRING_CONTENT;
          return true;
        }
        advance(lexer);
        lexer->mark_end(lexer);
        if (valid_symbols[INTERP_OPEN]) {
          scanner->interp_level++;
          lexer->result_symbol = INTERP_OPEN;
          return true;
        }
        return false;
      }
      has_content = true;
      continue;
    }

    if (lexer->lookahead == '"') {
      if (has_content && valid_symbols[STRING_CONTENT]) {
        lexer->mark_end(lexer);
        lexer->result_symbol = STRING_CONTENT;
        return true;
      }
      return false;
    }

    has_content = true;
    advance(lexer);
  }
}

// Scan content for triple-quoted strings (no interpolation).
// delimiter is either '"' or '\'' depending on the mode.
// May also emit STRING_END when closing delimiter is found with no content.
static bool scan_triple_string_content(Scanner *scanner, TSLexer *lexer,
                                       const bool *valid_symbols, int32_t delim) {
  bool has_content = false;

  while (true) {
    if (lexer->lookahead == '\0') {
      if (has_content && valid_symbols[STRING_CONTENT]) {
        lexer->result_symbol = STRING_CONTENT;
        return true;
      }
      return false;
    }

    if (lexer->lookahead == '\\') {
      has_content = true;
      advance(lexer);
      if (lexer->lookahead != '\0') {
        advance(lexer);
      }
      continue;
    }

    if (lexer->lookahead == delim) {
      // Check for triple delimiter
      lexer->mark_end(lexer);
      advance(lexer);
      if (lexer->lookahead == delim) {
        advance(lexer);
        if (lexer->lookahead == delim) {
          // Found closing triple delimiter
          if (has_content && valid_symbols[STRING_CONTENT]) {
            // Emit content first (mark_end before first delim)
            lexer->result_symbol = STRING_CONTENT;
            return true;
          }
          // No content before closing delimiter - emit STRING_END directly
          // (We already advanced past 2 delims, consume the 3rd)
          advance(lexer);
          lexer->mark_end(lexer);
          if (valid_symbols[STRING_END]) {
            scanner->string_depth--;
            scanner->string_mode = SMODE_REGULAR;
            lexer->result_symbol = STRING_END;
            return true;
          }
          return false;
        }
        // Only two delims - they're part of content
        has_content = true;
        continue;
      }
      // Only one delim - it's part of content
      has_content = true;
      continue;
    }

    has_content = true;
    advance(lexer);
  }
}

bool tree_sitter_morloc_external_scanner_scan(
    void *payload, TSLexer *lexer,
    const bool *valid_symbols)
{
    Scanner *scanner = (Scanner *)payload;

    // --- Inside a string (string_depth > 0) ---
    if (scanner->string_depth > 0) {

      // INTERP_CLOSE: } that closes an interpolation
      if (valid_symbols[INTERP_CLOSE] && scanner->interp_level > 0
          && lexer->lookahead == '}') {
        advance(lexer);
        lexer->mark_end(lexer);
        scanner->interp_level--;
        lexer->result_symbol = INTERP_CLOSE;
        return true;
      }

      // STRING_CONTENT and INTERP_OPEN scanning
      if (valid_symbols[STRING_CONTENT] || valid_symbols[INTERP_OPEN]) {
        bool matched;
        if (scanner->string_mode == SMODE_REGULAR) {
          matched = scan_regular_string_content(scanner, lexer, valid_symbols);
        } else {
          int32_t delim = (scanner->string_mode == SMODE_TRIPLE_DQ) ? '"' : '\'';
          matched = scan_triple_string_content(scanner, lexer, valid_symbols, delim);
        }
        if (matched) return true;
        // Fell through: no content before closing delimiter
      }

      // STRING_END: closing delimiter
      if (valid_symbols[STRING_END]) {
        if (scanner->string_mode == SMODE_REGULAR && lexer->lookahead == '"') {
          advance(lexer);
          lexer->mark_end(lexer);
          scanner->string_depth--;
          scanner->string_mode = SMODE_REGULAR;
          lexer->result_symbol = STRING_END;
          return true;
        }
        if (scanner->string_mode == SMODE_TRIPLE_DQ && lexer->lookahead == '"') {
          advance(lexer);
          if (lexer->lookahead == '"') {
            advance(lexer);
            if (lexer->lookahead == '"') {
              advance(lexer);
              lexer->mark_end(lexer);
              scanner->string_depth--;
              scanner->string_mode = SMODE_REGULAR;
              lexer->result_symbol = STRING_END;
              return true;
            }
          }
          return false;
        }
        if (scanner->string_mode == SMODE_TRIPLE_SQ && lexer->lookahead == '\'') {
          advance(lexer);
          if (lexer->lookahead == '\'') {
            advance(lexer);
            if (lexer->lookahead == '\'') {
              advance(lexer);
              lexer->mark_end(lexer);
              scanner->string_depth--;
              scanner->string_mode = SMODE_REGULAR;
              lexer->result_symbol = STRING_END;
              return true;
            }
          }
          return false;
        }
      }

      // Inside an interpolation expression: fall through to normal scanning
      if (scanner->interp_level > 0) {
        // Fall through to whitespace/indent/GDOT handling below
      } else {
        return false;
      }
    }

    // --- Outside a string (or inside interpolation expression) ---

    // skip whitespace and track newlines for indent handling
    bool has_space = true;
    while(has_space){
        switch (lexer->lookahead) {
            case '\r':
            case '\n':
                scanner->in_whitespace = true;
                skip(lexer);
                scanner->dedents = 0;
                break;
            case ' ':
            case '\t':
                skip(lexer);
                break;
            default:
                has_space = false;
        }
    }

    // either we just passed a newline OR we are at the start of the file
    if(scanner->in_whitespace){
        if( scanner->dedents == 0 &&
            lexer->get_column(lexer) < scanner->indents[scanner->level]
        ){
            for(int i = scanner->level; i >= 0; i--){
                if(lexer->get_column(lexer) == scanner->indents[i]){
                    scanner->dedents = scanner->level - i;
                }
            }
        }
    }

    // loop through all the required dedents, once per scanner call
    if( valid_symbols[DEDENT] ){
        if(lexer->lookahead == '\0'){
            lexer->result_symbol = DEDENT;
            return true;
        }
        if(scanner->level > 0 && scanner->dedents > 0){
            scanner->indents[scanner->level] = 0;
            scanner->level--;
            scanner->dedents--;
            lexer->result_symbol = DEDENT;
            return true;
        }
    }

    // check if we are left aligned to the current indent level
    if(valid_symbols[LEFT_ALIGNED] &&
       scanner->in_whitespace &&
       lexer->get_column(lexer) == scanner->indents[scanner->level]
    ){
        lexer->result_symbol = LEFT_ALIGNED;
        scanner->in_whitespace = false;
        return true;
    }

    // set a new indent level
    if( valid_symbols[INDENT] &&
        scanner->in_whitespace &&
        lexer->get_column(lexer) > scanner->indents[scanner->level]
    ){
        scanner->level++;
        scanner->indents[scanner->level] = lexer->get_column(lexer);
        lexer->result_symbol = INDENT;
        return true;
    }

    scanner->in_whitespace = false;

    // STRING_START: opening delimiter for strings
    if (valid_symbols[STRING_START]) {
      if (lexer->lookahead == '"') {
        advance(lexer);
        lexer->mark_end(lexer);  // Mark after first " (regular string boundary)
        // Check for triple double-quote """
        if (lexer->lookahead == '"') {
          advance(lexer);
          if (lexer->lookahead == '"') {
            // Triple quote """ - re-mark after all three
            advance(lexer);
            lexer->mark_end(lexer);
            scanner->string_depth++;
            scanner->string_mode = SMODE_TRIPLE_DQ;
            lexer->result_symbol = STRING_START;
            return true;
          }
          // Just "" (empty string) - mark_end stays after first "
          // Second " will be re-read as STRING_END
        }
        // Regular string start (mark_end already set after first ")
        scanner->string_depth++;
        scanner->string_mode = SMODE_REGULAR;
        lexer->result_symbol = STRING_START;
        return true;
      }
      if (lexer->lookahead == '\'') {
        // Only handle triple single-quote '''
        lexer->mark_end(lexer);  // Mark before any consumption
        advance(lexer);
        if (lexer->lookahead == '\'') {
          advance(lexer);
          if (lexer->lookahead == '\'') {
            advance(lexer);
            lexer->mark_end(lexer);
            scanner->string_depth++;
            scanner->string_mode = SMODE_TRIPLE_SQ;
            lexer->result_symbol = STRING_START;
            return true;
          }
        }
        // Not a triple quote - don't match (mark_end before ' means nothing consumed)
        return false;
      }
    }

    // GDOT: getter dot
    if (valid_symbols[GDOT] && lexer->lookahead == '.') {
      lexer->mark_end(lexer);
      advance(lexer);
      if (is_lower(lexer->lookahead) || is_digit(lexer->lookahead) || lexer->lookahead == '(') {
        lexer->mark_end(lexer);
        lexer->result_symbol = GDOT;
        return true;
      }
      return false;
    }

    return false;
}

void *tree_sitter_morloc_external_scanner_create() {
  Scanner *scanner = malloc(sizeof(Scanner));
  memset(scanner, 0, sizeof(Scanner));
  scanner->in_whitespace = true;
  return scanner;
}

void tree_sitter_morloc_external_scanner_destroy(void *payload) {
  free(payload);
}

void tree_sitter_morloc_external_scanner_reset(void *payload) {
  Scanner *scanner = (Scanner *)payload;
  memset(scanner->indents, 0, sizeof(scanner->indents));
  scanner->level = 0;
  scanner->dedents = 0;
  scanner->in_whitespace = true;
  scanner->string_depth = 0;
  scanner->interp_level = 0;
  scanner->string_mode = SMODE_REGULAR;
}

unsigned tree_sitter_morloc_external_scanner_serialize(void *payload, char *buffer) {
  Scanner *scanner = (Scanner *)payload;
  memcpy(buffer, scanner, sizeof(Scanner));
  return sizeof(Scanner);
}

void tree_sitter_morloc_external_scanner_deserialize(void *payload, const char *buffer, unsigned length) {
  Scanner *scanner = (Scanner *)payload;
  if (length == sizeof(Scanner)) {
    memcpy(scanner, buffer, sizeof(Scanner));
  }
}
