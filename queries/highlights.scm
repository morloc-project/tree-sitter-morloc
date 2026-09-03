; Comments

(comment) @comment
(doc_comment) @comment.documentation
(group_comment) @comment.documentation

; Names
;
; The generic captures come first so the more specific ones below win.

(lower) @variable
(upper) @type
(operator) @operator
(operator_name) @operator

; Literals

(integer) @number
(float) @number.float
(inf) @number.float
(neg_inf) @number.float
(nan) @number.float
(boolean) @constant.builtin
(null_expr) @constant.builtin
(string) @string
(interp_string (string_start) @string)
(interp_string (string_mid) @string)
(interp_string (string_end) @string)
(tick_name) @string.special
(backtick_name) @string.special
(wildcard) @variable.builtin
(intrinsic) @function.builtin

; Types

(typedef_term name: (upper) @type.definition)
(constraint class: (upper) @type)
(instance_head class: (upper) @type)
(effect_decl (upper) @type.definition)
(effect_row (upper) @attribute)
(source_decl language: (_) @type.builtin)
(typedef_decl language: (_) @type.builtin)
(labeled_type (lower) @property)
(record_type_entry key: (lower) @property)
(nam_entry key: (lower) @property)

; Declarations

(signature name: (lower) @function)
(declaration name: (lower) @function)
(guarded_declaration name: (lower) @function)
(pattern_declaration name: (lower) @function)
(declaration parameter: (var_expr (lower) @variable.parameter))
(guarded_declaration parameter: (var_expr (lower) @variable.parameter))
(lambda_expr parameter: (var_expr (lower) @variable.parameter))
(let_binding name: (var_expr (lower) @variable))
(do_bind name: (var_expr (lower) @variable))
(as_expr name: (lower) @variable)

; Fields

(record_entry key: (lower) @property)
(accessor_body key: (lower) @property)
(accessor_body index: (integer) @property)
(grouped_accessor (accessor_body key: (lower) @property))

; Modules

(module name: (module_name) @module)
(import_decl module: (_) @module)
(import_decl alias: (lower) @module)

; Keywords

[
  "module"
  "import"
  "source"
  "from"
  "as"
] @keyword.import

[
  "type"
  "newtype"
  "record"
  "object"
] @keyword.type

[
  "class"
  "instance"
  "effect"
  "escapable"
  "where"
  "infixl"
  "infixr"
  "infix"
  "%inline"
] @keyword

[
  "let"
  "in"
  "do"
] @keyword

; Operators and punctuation

[
  "="
  "->"
  "=>"
  "::"
  "<-"
  "!"
  "?"
  "@"
  "|"
  "+"
  "-"
  "*"
  "/"
  "."
  "\\"
] @operator

[
  ","
  ";"
  ":"
] @punctuation.delimiter

[
  "("
  ")"
  "["
  "]"
  "{"
  "}"
  "<"
  ">"
] @punctuation.bracket
