# tree-sitter-morloc

[![discord][discord]](https://discord.gg/w7nTvsVJhm)

A Tree-sitter parser for [Morloc](https://github.com/morloc-project/morloc)
source files (`.loc`).

## How this grammar relates to the compiler

Morloc is not a language a regex lexer can tokenize. Token boundaries depend on
what came before -- a dot is a getter, a namespace separator, or a composition
operator depending on the previous token and on whether whitespace separates
them -- and block structure comes from a GHC-style layout pass that inserts
virtual braces and semicolons from indentation.

So this parser mirrors the compiler rather than approximating it:

- `src/scanner.c` reimplements `library/Morloc/Frontend/Lexer.hs`, both the
  token rules (`lexOne`) and the layout pass (`insertLayout`). Every terminal,
  including the virtual layout tokens, comes from the external scanner.
- `grammar.js` is a transcription of `library/Morloc/Frontend/Parser.y` and
  keeps that file's nonterminal names, so the two can be diffed when the
  language changes.

When the language moves, re-read those two compiler files and port the delta.

## Verifying

The compiler's golden test suite is the reference corpus. Every file it builds
must parse here with no `ERROR` or `MISSING` node:

```sh
test/parse-corpus.sh                        # the compiler's golden tests
test/parse-corpus.sh /path/to/some/tree     # any other tree of .loc files
```

The unit corpus covers each construct in isolation:

```sh
tree-sitter test
```

<!-- [discord]: https://img.shields.io/discord/1063097320771698699?logo=discord&label=discord -->
