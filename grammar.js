function sepBy1(rule, separator) {
  return seq(rule, repeat(seq(separator, rule)));
}

function sepBy(rule, separator) {
  return optional(sepBy1(rule, separator));
}

function sepBy2(rule, separator) {
  return seq(rule, repeat1(seq(separator, rule)));
}

function parens(rule) {
  return seq("(", rule, ")");
}

function brackets(rule) {
  return seq("[", rule, "]");
}

function braces(rule) {
  return seq("{", rule, "}");
}

module.exports = grammar({

  name: 'morloc',

  externals: $ => [
    $._indent,
    $._dedent,
    $._left_aligned,
    /\n/,
    $.gdot,
    $.string_start,
    $.string_mid,
    $.string_end,
    $.interp_open,
    $.interp_close,
    $.string_content,
  ],

  extras: $ => [/\s/, /\n/, /\r/, $.docComment, $.groupComment, $.lineComment, $.blockComment],

  conflicts: $ => [
    [$.signature, $.declaration],
    [$.typedef, $._typeAtom],
    [$.constraint, $._typeHead],
    [$.constraint, $.typeclass],
    [$._typeHead, $._typeAtom],
    [$._expression, $._atomExpr],
  ],

  rules: {
    source_file: $ => seq($._toplevel, repeat(seq($._left_aligned, optional($._toplevel)))),

    word: $ => $.identifier,

    _toplevel: $ => choice(
      $.module,
      $.import,
      $.source,
      $.typeclass,
      $.instance,
      $.typedef,
      $.recdef,
      $.fixity,
      $.signature,
      $.declaration
    ),


    // --------- COMMENT ------------------------------------------------------

    // Doc/group annotations have higher token precedence than lineComment
    // so --' and --* are matched before the generic -- pattern
    docComment: $ => token(prec(1, /--'[^\n\r]*/)),
    groupComment: $ => token(prec(1, /--\*[^\n\r]*/)),
    lineComment: $ => /--[^\n\r]*/,

    blockComment: $ => seq(
      "{-",
      repeat(choice(
        /[^-][}]/,
        /.[^}]/,
      )),
      "-}"
    ),

    // --------- MODULE --------------------------------------------------------

    module: $ => seq(
      "module",
      field("modulePath", $.modulePath),
      parens(
        choice(
          $.star,
          sepBy($.exportItem, ",")
        )
      )
    ),

    star: $ => "*",

    modulePath: $ => sepBy1(field("segment", $.moduleComponent), "."),

    moduleComponent: $ => seq(
      $.identifier,
      repeat(seq("-", $.identifier))
    ),

    exportItem: $ => choice(
      $.identifier,
      $.identifierU,
      $.operatorExport
    ),

    operatorExport: $ => choice(
      seq("(", $.operator, ")"),
      seq("(", "-", ")"),
      seq("(", ".", ")")
    ),

    // --------- IMPORT --------------------------------------------------------

    import: $ => seq(
      "import",
      field("modulePath", $.modulePath),
      optional(parens(sepBy($.importTerm, ",")))
    ),

    importTerm: $ => choice(
      seq(
        field("term", $.identifier),
        optional(seq("as", field("alias", $.identifier)))
      ),
      seq(
        field("type", $.identifierU),
        optional(seq("as", field("alias", $.identifierU)))
      ),
      $.operatorImport
    ),

    operatorImport: $ => seq(
      choice(
        seq("(", $.operator, ")"),
        seq("(", "-", ")"),
        seq("(", ".", ")")
      ),
      optional(seq("as", field("alias", choice(
        $.identifier,
        seq("(", $.operator, ")"),
        seq("(", "-", ")"),
        seq("(", ".", ")")
      ))))
    ),


    // --------- SOURCE --------------------------------------------------------

    source: $ => choice(
      // Old form: source Lang opt_from (terms)
      seq(
        "source",
        $.language,
        field("sourceFile", optional(seq("from", $.string))),
        parens(sepBy($.sourceTerm, ","))
      ),
      // New WHERE form: source Lang opt_from where { names }
      seq(
        "source",
        $.language,
        field("sourceFile", optional(seq("from", $.string))),
        "where",
        $._indent,
        repeat1(seq($._left_aligned, field("name", $.identifier))),
        $._dedent
      )
    ),

    sourceTerm: $ => seq(
      field("name", $.string),
      optional(seq("as", field("alias", choice(
        $.identifier,
        $.identifierU,
        seq("(", $.operator, ")"),
        seq("(", "-", ")"),
        seq("(", ".", ")")
      ))))
    ),

    // --------- FIXITY --------------------------------------------------------

    fixity: $ => seq(
      field("assoc", choice("infixl", "infixr", "infix")),
      field("precedence", $.integer),
      sepBy1($.operatorRef, ",")
    ),

    operatorRef: $ => choice(
      seq("(", $.operator, ")"),
      seq("(", "-", ")"),
      seq("(", ".", ")"),
      $.operator,
      ".",
      "-",
      $.identifier
    ),

    // --------- SIGNATURE -----------------------------------------------------

    signature: $ => seq(
      field("name", $._evarOrOp),
      field("arg", repeat($.identifier)),
      "::",
      field("value", $.sigType)
    ),

    _evarOrOp: $ => choice(
      $.identifier,
      seq("(", $.operator, ")"),
      seq("(", "-", ")"),
      seq("(", ".", ")")
    ),

    sigType: $ => choice(
      seq($.constraints, "=>", $._type),
      $._type
    ),

    constraints: $ => choice(
      $.constraint,
      parens(sepBy1($.constraint, ","))
    ),

    constraint: $ => seq($.identifierU, repeat1($._typeAtom)),

    // --------- TYPEDEF -------------------------------------------------------

    typedef: $ => seq(
      "type",
      optional(seq($.language, "=>")),
      $._type,
      optional(seq(
        "=",
        choice(
          $._type,
          field("native", seq($.string, repeat($._typeAtom)))
        )
      ))
    ),

    recdef: $ => choice(
      // Inline form: record Name = Constructor { fields }
      seq(
        $._recform,
        optional(seq($.language, "=>")),
        $._recordName,
        "=",
        $._recordType
      ),
      // WHERE form: record Name where { field :: Type; ... }
      seq(
        $._recform,
        $._recordName,
        "where",
        $._indent,
        repeat1(seq($._left_aligned, $.recordTypeEntry)),
        $._dedent
      )
    ),

    _recform: $ => choice(
        $.recordDef,
        $.tableDef,
        $.objectDef
    ),

    recordDef: $ => "record",
    tableDef: $ => "table",
    objectDef: $ => "object",

    _recordType: $ => seq(
      field("constructor", choice(
        $.identifierU,
        $.identifier,
        $.string
      )),
      optional(seq(
      "{",
      field("entry", sepBy($.recordTypeEntry, ",")),
      "}"
      ))
    ),

    recordTypeEntry: $ => seq(
      field("key", $.identifier),
      "::",
      field("value", $._type)
    ),

    _recordName: $ => choice(
      parens($._recordName),
      seq(
        field("name", $.identifierU),
        repeat(field("param", $.identifier))
      )
    ),

    // --------- TYPE -------------------------------------------------------

    _type: $ => choice(
      $._typeAtom,
      $.paramT,
      $.functionT
    ),

    paramT: $ => prec.left(seq($._typeHead, repeat1($._typeAtom))),

    _typeHead: $ => choice(
      field("generic", $.identifier),
      field("concrete", $.identifierU),
      $.taggedType
    ),

    // types allowed as parameters (atoms)
    _typeAtom: $ => choice(
      field("generic", $.identifier),
      field("concrete", $.identifierU),
      $.taggedType,
      $.existentialType,
      $.thunkType,
      $.listType,
      $.tupleType,
      parens($._type),
      seq("(", ")"),
      $.string  // native type names like "int"
    ),

    functionT: $ => sepBy2($._typeFunArg, "->"),

    _typeFunArg: $ => choice(
      $._typeAtom,
      $.paramT
    ),

    existentialType: $ => seq("<", $.identifier, ">"),

    thunkType: $ => seq("{", $._type, "}"),

    listType: $ => brackets($._type),
    tupleType: $ => parens(sepBy2($._type, ",")),

    taggedType: $ => seq(
      $.tag,
      choice(
        field("generic", $.identifier),
        field("concrete", $.identifierU)
      )
    ),

    // --------- TYPECLASS -----------------------------------------------------

    typeclass: $ => seq(
      "class",
      optional(seq($.constraints, "=>")),
      field("class", $.identifierU),
      repeat(field("param", $._typeAtom)),
      optional(field("where", $.interface))
    ),

    interface: $ => seq(
      "where",
      $._indent,
      repeat1(seq($._left_aligned, $.signature)),
      $._dedent
    ),


    // --------- INSTANCE ------------------------------------------------------

    instance: $ => seq(
      "instance",
      optional(seq($.constraints, "=>")),
      field("class", $.identifierU),
      repeat(field("param", $._typeAtom)),
      optional(field("where", $.implementation))
    ),

    implementation: $ => seq(
      "where",
      $._indent,
      repeat1(seq($._left_aligned, choice($.source, $.declaration))),
      $._dedent
    ),

    // --------- DECLARATION ---------------------------------------------------

    declaration: $ => seq(
      field("lhs", $._evarOrOp),
      repeat(field("arg", $.identifier)),
      '=',
      field("rhs", $._expression),
      field("where", optional($.block))
    ),

    block: $ => choice(
      // Layout form (multi-line where block)
      seq(
        "where",
        $._indent,
        repeat1(seq($._left_aligned, choice($.declaration, $.signature))),
        $._dedent
      ),
      // Inline form (single binding on same line)
      seq("where", choice($.declaration, $.signature))
    ),

    // --------- EXPRESSIONS ---------------------------------------------------

    _expression: $ => choice(
      $.letExpr,
      $.doExpr,
      $.lambdaExpr,
      $.typeAnnotation,
      $._infixExpr
    ),

    typeAnnotation: $ => prec.right(1, seq(
      $._infixExpr,
      "::",
      $._type
    )),

    _infixExpr: $ => choice(
      $._operand,
      $.infixExpr
    ),

    infixExpr: $ => prec.right(2, seq(
      $._operand,
      $._infixOp,
      $._expression
    )),

    _infixOp: $ => choice(
      $.operator,
      alias("-", $.operator),
      alias(".", $.operator)
    ),

    _operand: $ => choice(
      $.application,
      $._atomExpr,
      $.negativeExpr
    ),

    negativeExpr: $ => seq("-", choice($.integer, $.float)),

    application: $ => prec.left(3, seq(
      $._atomExpr,
      repeat1($._atomExpr)
    )),

    _atomExpr: $ => choice(
      $.identifier,
      $._primitiveExpr,
      $.listExpr,
      $.tupleExpr,
      $.recordExpr,
      $.getterExpr,
      $.forceExpr,
      $.suspendExpr,
      $.holeExpr,
      $.unitExpr,
      $.operatorSection,
      $.doExpr,
      parens($._expression)
    ),

    forceExpr: $ => seq("!", $._atomExpr),

    suspendExpr: $ => seq("{", $._expression, "}"),

    holeExpr: $ => "_",

    unitExpr: $ => seq("(", ")"),

    operatorSection: $ => choice(
      seq("(", $.operator, ")"),
      seq("(", "-", ")"),
      seq("(", ".", ")")
    ),

    // --------- GETTER/ACCESSOR -----------------------------------------------

    getterExpr: $ => seq($.gdot, $.accessorBody),

    accessorBody: $ => prec.right(choice(
      seq($.identifier, optional($.accessorTail)),
      seq($.integer, optional($.accessorTail)),
      seq("(", sepBy1($.groupedAccessor, ","), ")")
    )),

    accessorTail: $ => choice(
      seq("=", $._expression),
      seq($.gdot, $.accessorBody)
    ),

    groupedAccessor: $ => seq($.gdot, $.accessorBody),

    // --------- LET EXPRESSION ------------------------------------------------

    letExpr: $ => seq(
      repeat1($.letBinding),
      "in",
      $._expression
    ),

    letBinding: $ => seq(
      "let",
      choice($.identifier, "_"),
      "=",
      $._expression
    ),

    // --------- DO EXPRESSION -------------------------------------------------

    doExpr: $ => seq(
      "do",
      $._indent,
      repeat1(seq($._left_aligned, $.doStmt)),
      $._dedent
    ),

    doStmt: $ => choice(
      $.doBind,
      $.doLet,
      $._expression
    ),

    doBind: $ => seq($.identifier, "<-", $._expression),

    doLet: $ => seq("let", choice($.identifier, "_"), "=", $._expression),

    // --------- LAMBDA --------------------------------------------------------

    lambdaExpr: $ => seq(
      "\\",
      repeat1($.identifier),
      "->",
      $._expression
    ),

    // --------- PRIMITIVES ----------------------------------------------------

    language: $ => choice($.identifier, $.identifierU),

    tag: $ => /[a-z][a-zA-Z0-9]*:/,

    identifier: $ => /[a-z][a-zA-Z0-9_']*/,

    identifierU: $ => /[A-Z][a-zA-Z0-9_']*/,

    operator: $ => /[:!$%&*+./<=>?@\\^|~#]+/,

    listExpr: $ => brackets(sepBy($._expression, ",")),

    tupleExpr: $ => parens(sepBy2($._expression, ",")),

    recordExpr: $ => braces(sepBy($.recordExprEntry, ",")),

    recordExprEntry: $ => seq(
      $.identifier,
      "=",
      $._expression
    ),

    _primitiveExpr: $ => choice(
      $.string,
      $.interpString,
      $.integer,
      $.float,
      $._boolean
    ),

    // Simple string (no interpolation) - used in types, source terms, etc.
    // In expression context, interpString (external scanner) takes priority.
    string: $ => /"(?:\\.|[^"\\#]|#[^{])*"/,

    // Interpolated string (handled by external scanner)
    interpString: $ => seq(
      $.string_start,
      repeat(seq(
        optional($.string_content),
        $.interp_open,
        $._expression,
        $.interp_close
      )),
      optional($.string_content),
      $.string_end
    ),

    integer: $ => choice(
      /[0-9]+/,
      /0[xX][0-9a-fA-F]+/,
      /0[oO][0-7]+/,
      /0[bB][01]+/
    ),

    float: $ => choice(
      /[0-9]+\.[0-9]+([eE][+-]?[0-9]+)?/,
      /[0-9]+[eE][+-]?[0-9]+/
    ),

    _boolean: $ => choice($.true, $.false),
    true: $ => 'True',
    false: $ => 'False'
  }
});
