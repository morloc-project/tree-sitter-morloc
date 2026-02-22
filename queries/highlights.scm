(declaration lhs: (_) @function)
(declaration arg: (_) @variable.parameter)

(signature name: (_) @type)

(integer) @number
(float) @number.float
(string) @string
(interpString (string_start) @string)
(interpString (string_content) @string)
(interpString (string_end) @string)
(interpString (interp_open) @punctuation.special)
(interpString (interp_close) @punctuation.special)
(true) @constant.builtin
(false) @constant.builtin

(lineComment) @comment
(blockComment) @comment
(docComment) @comment.documentation
(groupComment) @comment.documentation
(tag) @keyword.directive

(identifier) @variable
(identifierU) @type
(operator) @operator

(holeExpr) @variable.builtin

[
  "="
  "->"
  "=>"
  "::"
  "<-"
  "!"
] @operator

[
  "module"
  "import"
  "source"
  "from"
  "as"
] @keyword.import

[
  "where"
  "type"
  "class"
  "instance"
  "let"
  "in"
  "do"
  "infixl"
  "infixr"
  "infix"
] @keyword

(recordDef) @keyword
(objectDef) @keyword
(tableDef) @keyword

"," @punctuation.delimiter

[
  "{"
  "}"
  "["
  "]"
  "("
  ")"
  "<"
  ">"
] @punctuation.bracket
