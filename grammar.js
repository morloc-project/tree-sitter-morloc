// Grammar for the morloc language.
//
// This is a transcription of the compiler's LALR grammar,
// library/Morloc/Frontend/Parser.y, and the rules below keep that file's
// nonterminal names so the two can be diffed when the language changes.
// Every terminal is produced by the external scanner in src/scanner.c,
// which reimplements Morloc.Frontend.Lexer, including the layout pass that
// inserts the virtual braces and semicolons the block rules below match.

function sepBy1(rule, separator) {
  return seq(rule, repeat(seq(separator, rule)));
}

function sepBy2(rule, separator) {
  return seq(rule, repeat1(seq(separator, rule)));
}

// Aliases that give the scanner's hidden terminals their conventional
// spelling in the tree, so queries can match "module", "(", "->" and so on.
let TOKENS = null;
function t($) {
  if (TOKENS === null) {
    TOKENS = {
      lparen: alias($._lparen, '('),
      rparen: alias($._rparen, ')'),
      lbracket: alias($._lbracket, '['),
      rbracket: alias($._rbracket, ']'),
      lbrace: alias($._lbrace, '{'),
      rbrace: alias($._rbrace, '}'),
      langle: alias($._langle, '<'),
      rangle: alias($._rangle, '>'),
      comma: alias($._comma, ','),
      backslash: alias($._backslash, '\\'),
      underscore: alias($._underscore, '_'),
      bang: alias($._bang, '!'),
      question: alias($._question, '?'),
      at: alias($._at, '@'),
      dot: alias($._dot, '.'),
      gdot: alias($._gdot, '.'),
      nsdot: alias($._nsdot, '.'),
      gdotchain: alias($._gdotchain, '.'),
      equals: alias($._equals, '='),
      dcolon: alias($._dcolon, '::'),
      arrow: alias($._arrow, '->'),
      fatarrow: alias($._fatarrow, '=>'),
      bind: alias($._bind, '<-'),
      star: alias($._star, '*'),
      minus: alias($._minus, '-'),
      pipe: alias($._pipe, '|'),
      colon: alias($._colon, ':'),
      semicolon: alias($._semicolon, ';'),
      plus: alias($._plus, '+'),
      slash: alias($._slash, '/'),
      inline: alias($._pragma_inline, '%inline'),
      kmodule: alias($._kw_module, 'module'),
      kimport: alias($._kw_import, 'import'),
      ksource: alias($._kw_source, 'source'),
      kfrom: alias($._kw_from, 'from'),
      kwhere: alias($._kw_where, 'where'),
      kas: alias($._kw_as, 'as'),
      ktrue: alias($._kw_true, 'True'),
      kfalse: alias($._kw_false, 'False'),
      ktype: alias($._kw_type, 'type'),
      knewtype: alias($._kw_newtype, 'newtype'),
      krecord: alias($._kw_record, 'record'),
      kobject: alias($._kw_object, 'object'),
      kclass: alias($._kw_class, 'class'),
      kinstance: alias($._kw_instance, 'instance'),
      keffect: alias($._kw_effect, 'effect'),
      kescapable: alias($._kw_escapable, 'escapable'),
      kinfixl: alias($._kw_infixl, 'infixl'),
      kinfixr: alias($._kw_infixr, 'infixr'),
      kinfix: alias($._kw_infix, 'infix'),
      klet: alias($._kw_let, 'let'),
      kin: alias($._kw_in, 'in'),
      kdo: alias($._kw_do, 'do'),
      knull: alias($._kw_null, 'Null'),
      inf: alias($._inf, 'Inf'),
      neginf: alias($._neginf, '-Inf'),
      nan: alias($._nan, 'NaN'),
    };
  }
  return TOKENS;
}

module.exports = grammar({
  name: 'morloc',

  externals: $ => [
    $._vlbrace,
    $._vrbrace,
    $._vsemi,
    $._lparen,
    $._rparen,
    $._lbracket,
    $._rbracket,
    $._lbrace,
    $._rbrace,
    $._langle,
    $._rangle,
    $._comma,
    $._backslash,
    $._underscore,
    $._bang,
    $._question,
    $._at,
    $._dot,
    $._gdot,
    $._nsdot,
    $._gdotchain,
    $._equals,
    $._dcolon,
    $._arrow,
    $._fatarrow,
    $._bind,
    $._star,
    $._minus,
    $._pipe,
    $._colon,
    $._semicolon,
    $._kw_module,
    $._kw_import,
    $._kw_source,
    $._kw_from,
    $._kw_where,
    $._kw_as,
    $._kw_true,
    $._kw_false,
    $._kw_type,
    $._kw_newtype,
    $._kw_record,
    $._kw_object,
    $._kw_class,
    $._kw_instance,
    $._kw_effect,
    $._kw_escapable,
    $._kw_infixl,
    $._kw_infixr,
    $._kw_infix,
    $._kw_let,
    $._kw_in,
    $._kw_do,
    $._kw_null,
    // 'export' is reserved by the lexer but reaches no production, so using
    // it as a name is an error, as it is in the compiler.
    $._kw_export,
    $._inf,
    $._neginf,
    $._nan,
    $.lower,
    $.upper,
    $._plus,
    $._slash,
    $.operator,
    $.integer,
    $.float,
    $.string,
    $.string_start,
    $.string_mid,
    $.string_end,
    $._interp_open,
    $._interp_close,
    $.intrinsic,
    $.tick_name,
    $.backtick_name,
    $._pragma_inline,
    // Emitted for a chained getter that follows a group accessor. It appears
    // in no rule, so the construct the compiler rejects becomes an error
    // node here too.
    $._gdot_after_group,
    $.comment,
    $.doc_comment,
    $.group_comment,
  ],

  extras: $ => [$.comment, $.doc_comment, $.group_comment],

  conflicts: $ => [
    [$.operator_ref_paren, $.operator_section],
    [$._top_decls_explicit],
    [$._top_decls_virtual],
    [$.var_expr, $._evar_or_op],
    [$._guard_clauses],
    [$._type, $.function_type],
    [$._infix_expr, $.infix_expr],
    [$._app_expr, $.apply_expr],
    [$._expr, $.annotation_expr],
    [$._add_type, $.nat_mul_type],
    [$._non_fun_type, $.nat_add_type],
    [$._mul_type, $.apply_type],
    [$._atom_type, $.constraint],
  ],

  rules: {
    //----------------------------------------------------------------
    // Program
    //----------------------------------------------------------------

    source_file: $ => choice(repeat1($.module), $._top_body),

    module: $ => seq(
      t($).kmodule,
      optional(field('name', $.module_name)),
      t($).lparen,
      optional($.exports),
      t($).rparen,
      $._top_body,
    ),

    _top_body: $ => choice(
      seq($._vlbrace, optional($._top_decls_virtual), $._vrbrace),
      seq(t($).lbrace, optional($._top_decls_explicit), t($).rbrace),
    ),

    // The compiler rewrites a bare expression in final position into an
    // assignment to __expr__, which is how `morloc eval` scripts are written.
    _top_decls_virtual: $ => choice(
      sepBy1($._top_decl, $._vsemi),
      seq(repeat(seq($._top_decl, $._vsemi)), $.trailing_expr),
    ),

    _top_decls_explicit: $ => choice(
      sepBy1($._top_decl, t($).semicolon),
      seq(repeat(seq($._top_decl, t($).semicolon)), $.trailing_expr),
    ),

    trailing_expr: $ => $._expr,

    _top_decl: $ => choice(
      $.import_decl,
      $.typedef_decl,
      $.typeclass_decl,
      $.effect_decl,
      $.instance_decl,
      $.fixity_decl,
      $.source_decl,
      $.inline_source_decl,
      $._sig_or_ass,
    ),

    _sig_or_ass: $ => choice(
      $.signature,
      $.declaration,
      $.guarded_declaration,
      $.pattern_declaration,
    ),

    signature: $ => seq(
      field('name', $._evar_or_op),
      t($).dcolon,
      field('type', $.sig_type),
    ),

    declaration: $ => seq(
      field('name', $._evar_or_op),
      repeat(field('parameter', $._atom_expr)),
      t($).equals,
      field('body', $._expr),
      optional($.where_block),
    ),

    guarded_declaration: $ => seq(
      field('name', $._evar_or_op),
      repeat(field('parameter', $._atom_expr)),
      $._guard_clauses,
      t($).colon,
      field('default', $._expr),
      optional($.where_block),
    ),

    // `| p1 ... pn = body` clauses, gathered without a layout separator.
    pattern_declaration: $ => seq(
      field('name', $._evar_or_op),
      repeat1($.pattern_clause),
      optional($.where_block),
    ),

    pattern_clause: $ => seq(
      t($).pipe,
      repeat1(field('pattern', $._atom_expr)),
      choice(
        seq(t($).equals, field('body', $._expr)),
        seq(
          $._guard_clauses,
          optional($._vsemi),
          t($).colon,
          field('default', $._expr),
        ),
      ),
    ),

    // Layout inserts a VSEMI between clauses aligned at a block's own indent.
    _guard_clauses: $ => seq(
      $.guard_clause,
      repeat(seq(optional($._vsemi), $.guard_clause)),
    ),

    guard_clause: $ => seq(
      t($).question,
      field('condition', $._expr),
      t($).equals,
      field('body', $._expr),
    ),

    where_block: $ => seq(
      t($).kwhere,
      choice(
        seq($._vlbrace, sepBy1($._sig_or_ass, $._vsemi), $._vrbrace),
        seq(t($).lbrace, sepBy1($._sig_or_ass, t($).semicolon), t($).rbrace),
      ),
    ),

    //----------------------------------------------------------------
    // Module names
    //----------------------------------------------------------------

    module_name: $ => sepBy1(
      $.module_comp,
      choice(t($).dot, t($).gdot, t($).nsdot, t($).gdotchain),
    ),

    module_comp: $ => seq($.lower, repeat(seq(t($).minus, $.lower))),

    //----------------------------------------------------------------
    // Exports
    //----------------------------------------------------------------

    exports: $ => choice(t($).star, sepBy1($.export_item, t($).comma)),

    export_item: $ => $._symbol,

    _symbol: $ => choice($.lower, $.upper, $.operator_ref_paren),

    operator_ref_paren: $ => seq(
      t($).lparen,
      choice($.operator_name, t($).minus, t($).dot),
      t($).rparen,
    ),

    operator_name: $ => choice(
      $.operator,
      t($).plus,
      t($).star,
      t($).langle,
      t($).rangle,
      t($).slash,
    ),

    //----------------------------------------------------------------
    // Imports
    //----------------------------------------------------------------

    import_decl: $ => seq(
      t($).kimport,
      choice(
        field('module', $.import_module_name),
        seq(t($).gdot, field('module', $.module_name)),
      ),
      optional(seq(t($).kas, field('alias', $.lower))),
      optional($.import_list),
    ),

    import_module_name: $ => choice(
      seq($.module_comp, t($).slash, $.module_name),
      $.module_name,
    ),

    import_list: $ => seq(
      t($).lparen,
      optional(sepBy1($.import_item, t($).comma)),
      t($).rparen,
    ),

    import_item: $ => choice(
      seq($.lower, optional(seq(t($).kas, $.lower))),
      seq($.upper, optional(seq(t($).kas, $.upper))),
      seq($.operator_ref_paren, optional(seq(t($).kas, $.lower))),
    ),

    //----------------------------------------------------------------
    // Type definitions
    //----------------------------------------------------------------

    typedef_decl: $ => choice(
      seq(
        t($).ktype,
        field('language', $._lang_token),
        t($).fatarrow,
        $.typedef_term,
        t($).equals,
        $.concrete_rhs,
      ),
      seq(t($).ktype, $.typedef_term, optional(seq(t($).equals, $._type))),
      seq(t($).knewtype, $.typedef_term, optional(seq(t($).equals, $._type))),
      seq(
        $.nam_type,
        $.typedef_term,
        t($).kwhere,
        $._vlbrace,
        sepBy1($.nam_entry, $._vsemi),
        $._vrbrace,
      ),
      seq(
        $.nam_type,
        optional(seq(field('language', $._lang_token), t($).fatarrow)),
        $.typedef_term,
        t($).equals,
        $.nam_constructor,
        optional($.nam_entries),
      ),
    ),

    nam_type: $ => choice(t($).krecord, t($).kobject),

    _lang_token: $ => choice($.upper, $.lower),

    typedef_term: $ => choice(
      seq(field('name', $.upper), repeat($.typedef_param)),
      seq(t($).lparen, field('name', $.upper), repeat($.typedef_param), t($).rparen),
    ),

    typedef_param: $ => choice(
      $.lower,
      $.upper,
      seq(t($).lparen, $.lower, t($).dcolon, $.upper, t($).rparen),
      seq(t($).lparen, $._type, t($).rparen),
    ),

    nam_constructor: $ => choice(
      seq($.string, repeat($._nam_constructor_arg)),
      $.upper,
      $.lower,
    ),

    // A restricted atom type: the '{...}' forms are excluded so a positional
    // argument cannot be confused with the field list that may follow.
    _nam_constructor_arg: $ => choice(
      seq(t($).lparen, $._type, t($).rparen),
      seq(t($).lparen, $._type, t($).comma, sepBy1($._type, t($).comma), t($).rparen),
      seq(t($).lbracket, $._type, t($).rbracket),
      seq(t($).question, $._nam_constructor_arg),
      $.upper,
      $.lower,
      $.string,
      $.integer,
    ),

    nam_entries: $ => seq(
      t($).lbrace,
      sepBy1($.nam_entry, t($).comma),
      t($).rbrace,
    ),

    nam_entry: $ => seq(
      field('key', $.lower),
      t($).dcolon,
      field('type', $._type),
    ),

    concrete_rhs: $ => choice(
      seq($.string, repeat($._atom_type)),
      $._non_string_type,
    ),

    //----------------------------------------------------------------
    // Types
    //----------------------------------------------------------------

    _type: $ => choice($.function_type, $._non_fun_type),

    function_type: $ => prec.right(seq($._non_fun_type, t($).arrow, $._type)),

    _non_fun_type: $ => choice($.effect_type, $._add_type),

    effect_type: $ => prec.right(seq(
      t($).langle,
      $.effect_row,
      t($).rangle,
      $._non_fun_type,
    )),

    effect_row: $ => sepBy1(choice($.upper, $.lower), t($).comma),

    _add_type: $ => choice($.nat_add_type, $._mul_type),

    nat_add_type: $ => prec.left(seq(
      $._add_type,
      choice(t($).plus, t($).minus),
      $._mul_type,
    )),

    _mul_type: $ => choice($.nat_mul_type, $._app_type),

    nat_mul_type: $ => prec.left(seq(
      $._mul_type,
      choice(t($).star, t($).slash),
      $._app_type,
    )),

    _app_type: $ => choice($.apply_type, $._atom_type),

    apply_type: $ => prec.left(seq($._app_type, $._atom_type)),

    _atom_type: $ => choice(
      $.unit_type,
      $.paren_type,
      $.tuple_type,
      $.list_type,
      $.optional_type,
      $.record_type,
      $.tick_list_type,
      $.upper,
      $.labeled_type,
      $.lower,
      $.string,
      $.integer,
    ),

    unit_type: $ => seq(t($).lparen, t($).rparen),
    paren_type: $ => seq(t($).lparen, $._type, t($).rparen),
    tuple_type: $ => seq(
      t($).lparen,
      sepBy2($._type, t($).comma),
      t($).rparen,
    ),
    list_type: $ => seq(t($).lbracket, $._type, t($).rbracket),
    optional_type: $ => seq(t($).question, $._atom_type),
    record_type: $ => seq(
      t($).lbrace,
      optional(sepBy1($.record_type_entry, t($).comma)),
      t($).rbrace,
    ),
    record_type_entry: $ => seq(
      field('key', $.lower),
      choice(t($).equals, t($).dcolon),
      field('type', $._non_fun_type),
    ),
    tick_list_type: $ => seq(
      t($).lbracket,
      sepBy1($.tick_name, t($).comma),
      t($).rbracket,
    ),
    labeled_type: $ => prec.right(seq($.lower, t($).at, $._non_fun_type)),

    // The right-hand side of a concrete type alias, which may not begin with
    // a string: a leading string there is the concrete type's own name.
    _non_string_type: $ => choice(
      $.non_string_function_type,
      $._non_string_non_fun,
    ),

    non_string_function_type: $ => prec.right(seq(
      $._non_string_non_fun,
      t($).arrow,
      $._type,
    )),

    _non_string_non_fun: $ => choice(
      $.non_string_effect_type,
      $._non_string_add,
    ),

    non_string_effect_type: $ => prec.right(seq(
      t($).langle,
      $.effect_row,
      t($).rangle,
      $._non_string_non_fun,
    )),

    _non_string_add: $ => choice($.non_string_nat_add, $._non_string_mul),

    non_string_nat_add: $ => prec.left(seq(
      $._non_string_add,
      choice(t($).plus, t($).minus),
      $._non_string_mul,
    )),

    _non_string_mul: $ => choice($.non_string_nat_mul, $._non_string_app),

    non_string_nat_mul: $ => prec.left(seq(
      $._non_string_mul,
      choice(t($).star, t($).slash),
      $._non_string_app,
    )),

    _non_string_app: $ => choice($.non_string_apply, $._non_string_atom),

    non_string_apply: $ => prec.left(seq($._non_string_app, $._atom_type)),

    _non_string_atom: $ => choice(
      $.unit_type,
      $.paren_type,
      $.tuple_type,
      $.list_type,
      $.non_string_optional_type,
      $.upper,
      $.labeled_type,
      $.lower,
      $.integer,
    ),

    non_string_optional_type: $ => seq(t($).question, $._non_string_atom),

    //----------------------------------------------------------------
    // Typeclasses, effects and instances
    //----------------------------------------------------------------

    typeclass_decl: $ => seq(
      t($).kclass,
      $.class_head,
      optional(seq(
        t($).kwhere,
        $._vlbrace,
        sepBy1($.signature, $._vsemi),
        $._vrbrace,
      )),
    ),

    class_head: $ => choice(
      seq($._app_type, t($).fatarrow, $._app_type),
      seq(
        t($).lparen,
        sepBy1($.constraint, t($).comma),
        t($).rparen,
        t($).fatarrow,
        $._app_type,
      ),
      $._app_type,
    ),

    constraint: $ => seq(field('class', $.upper), repeat1($._atom_type)),

    effect_decl: $ => seq(
      optional(t($).kescapable),
      t($).keffect,
      sepBy1($.upper, t($).comma),
    ),

    instance_decl: $ => seq(
      t($).kinstance,
      sepBy1($.instance_head, t($).comma),
      optional(seq(
        t($).kwhere,
        $._vlbrace,
        sepBy1($._instance_item, $._vsemi),
        $._vrbrace,
      )),
    ),

    instance_head: $ => seq(field('class', $.upper), repeat1($._atom_type)),

    _instance_item: $ => choice(
      $.source_decl,
      $.inline_source_decl,
      $._sig_or_ass,
    ),

    //----------------------------------------------------------------
    // Fixity
    //----------------------------------------------------------------

    fixity_decl: $ => seq(
      field('associativity', choice(t($).kinfixl, t($).kinfixr, t($).kinfix)),
      field('precedence', $.integer),
      sepBy1($.operator_ref, t($).comma),
    ),

    operator_ref: $ => choice(
      $.operator_ref_paren,
      $.operator_name,
      t($).dot,
      t($).minus,
      $.lower,
    ),

    //----------------------------------------------------------------
    // Source declarations
    //----------------------------------------------------------------

    source_decl: $ => seq(
      t($).ksource,
      field('language', $._lang_token),
      optional(seq(t($).kfrom, field('path', $.string))),
      choice(
        seq(t($).lparen, sepBy1($.source_item, t($).comma), t($).rparen),
        seq(
          t($).kwhere,
          $._vlbrace,
          sepBy1($.source_new_item, $._vsemi),
          $._vrbrace,
        ),
      ),
    ),

    inline_source_decl: $ => seq(t($).inline, $.source_decl),

    source_item: $ => choice(
      seq($.string, optional(seq(t($).kas, $._source_alias))),
      seq($.operator_ref_paren, optional(seq(t($).kas, $._source_alias))),
      seq($.backtick_name, t($).kas, $._source_alias),
    ),

    _source_alias: $ => choice($.lower, $.upper, $.operator_ref_paren),

    source_new_item: $ => seq(
      optional(t($).inline),
      choice($.lower, $.operator_ref_paren, $.backtick_name),
    ),

    //----------------------------------------------------------------
    // Signature types
    //----------------------------------------------------------------

    sig_type: $ => choice(
      seq(field('constraints', $._type), t($).fatarrow, $._type),
      $._type,
    ),

    //----------------------------------------------------------------
    // Expressions
    //----------------------------------------------------------------

    _expr: $ => choice(
      $.let_expr,
      $.lambda_expr,
      $.guard_expr,
      $._infix_expr,
      $.annotation_expr,
    ),

    annotation_expr: $ => prec.right(seq(
      $._infix_expr,
      t($).dcolon,
      field('type', $._type),
    )),

    guard_expr: $ => seq(
      $._guard_clauses,
      optional($._vsemi),
      t($).colon,
      field('default', $._expr),
    ),

    let_expr: $ => seq(
      t($).klet,
      choice(
        seq($._vlbrace, sepBy1($.let_binding, $._vsemi), $._vrbrace),
        seq(t($).lbrace, sepBy1($.let_binding, t($).semicolon), t($).rbrace),
      ),
      choice(seq(t($).kin, $._expr), $.let_expr),
    ),

    let_binding: $ => choice(
      seq(field('name', $._atom_expr), t($).equals, field('body', $._expr)),
      seq(
        field('name', $.lower),
        $._guard_clauses,
        t($).colon,
        field('default', $._expr),
      ),
    ),

    lambda_expr: $ => seq(
      t($).backslash,
      repeat1(field('parameter', $._atom_expr)),
      t($).arrow,
      field('body', $._expr),
    ),

    _infix_expr: $ => choice($._operand, $.infix_expr),

    infix_expr: $ => prec.right(seq(
      $._operand,
      field('operator', choice($.operator_name, t($).minus, t($).dot)),
      $._expr,
    )),

    _operand: $ => choice($._app_expr, $.negate_expr),

    negate_expr: $ => prec.left(seq(t($).minus, $._app_expr)),

    _app_expr: $ => choice($._atom_expr, $.apply_expr),

    apply_expr: $ => prec.left(seq(
      field('function', $._atom_expr),
      repeat1(field('argument', $._atom_expr)),
    )),

    _atom_expr: $ => choice(
      $._paren_expr,
      $.getter_expr,
      $._string_expr,
      $.boolean,
      $._num_expr,
      $.list_expr,
      $.record_expr,
      $.var_expr,
      $.do_expr,
      $.null_expr,
      $.intrinsic,
      $.wildcard,
      $.as_expr,
      $.force_expr,
    ),

    // Tight prefix eval sugar: `bar !x` is `bar (!x)`.
    force_expr: $ => seq(t($).bang, $._atom_expr),

    wildcard: $ => t($).underscore,

    as_expr: $ => seq(field('name', $.lower), t($).at, field('pattern', $._atom_expr)),

    null_expr: $ => t($).knull,

    _paren_expr: $ => choice(
      $.unit_expr,
      $.operator_section,
      $.left_section,
      $.right_section,
      $.paren_expr,
      $.tuple_expr,
    ),

    unit_expr: $ => seq(t($).lparen, t($).rparen),

    operator_section: $ => seq(
      t($).lparen,
      choice($.operator_name, t($).minus, t($).dot),
      t($).rparen,
    ),

    left_section: $ => seq(
      t($).lparen,
      choice($.operator_name, t($).dot),
      $._expr,
      t($).rparen,
    ),

    right_section: $ => seq(
      t($).lparen,
      $._operand,
      choice($.operator_name, t($).minus, t($).dot),
      t($).rparen,
    ),

    paren_expr: $ => seq(t($).lparen, $._expr, t($).rparen),

    tuple_expr: $ => seq(
      t($).lparen,
      sepBy2($._expr, t($).comma),
      t($).rparen,
    ),

    record_expr: $ => seq(
      t($).lbrace,
      sepBy1($.record_entry, t($).comma),
      t($).rbrace,
    ),

    record_entry: $ => seq(
      field('key', $.lower),
      t($).equals,
      field('value', $._expr),
    ),

    list_expr: $ => seq(
      t($).lbracket,
      optional(sepBy1($._expr, t($).comma)),
      t($).rbracket,
    ),

    do_expr: $ => seq(
      t($).kdo,
      choice(
        seq($._vlbrace, sepBy1($._do_stmt, $._vsemi), $._vrbrace),
        seq(t($).lbrace, sepBy1($._do_stmt, t($).semicolon), t($).rbrace),
      ),
    ),

    _do_stmt: $ => choice($.do_bind, $.do_let, $._expr),

    do_bind: $ => seq(
      field('name', $._atom_expr),
      t($).bind,
      field('body', $._expr),
    ),

    do_let: $ => seq(
      t($).klet,
      $._vlbrace,
      sepBy1($.let_binding, $._vsemi),
      $._vrbrace,
    ),

    //----------------------------------------------------------------
    // Accessors
    //----------------------------------------------------------------

    getter_expr: $ => seq(
      choice(t($).gdot, t($).gdotchain),
      $.accessor_body,
    ),

    // The chain is greedy: a glued dot always extends the accessor rather
    // than starting a new one, so `.home.city` is one two-step accessor.
    accessor_body: $ => prec.right(choice(
      seq(field('key', $.lower), optional($._accessor_tail)),
      seq(field('index', $.integer), optional($._accessor_tail)),
      seq(t($).lparen, sepBy1($.grouped_accessor, t($).comma), t($).rparen),
      seq(
        t($).lbracket,
        sepBy1($._bracket_axis, t($).comma),
        t($).rbracket,
        optional($._accessor_tail),
      ),
    )),

    _accessor_tail: $ => choice(
      $.accessor_set,
      seq(t($).gdotchain, $.accessor_body),
    ),

    accessor_set: $ => seq(t($).equals, $._expr),

    grouped_accessor: $ => seq(
      choice(t($).gdot, t($).gdotchain),
      $.accessor_body,
    ),

    // A single index, or a start:stop:step slice with every part optional.
    _bracket_axis: $ => choice($._expr, $.slice),

    slice: $ => choice(
      seq($._expr, t($).colon, optional($._expr), optional(seq(t($).colon, optional($._expr)))),
      seq(t($).colon, optional($._expr), optional(seq(t($).colon, optional($._expr)))),
    ),

    //----------------------------------------------------------------
    // Atoms
    //----------------------------------------------------------------

    var_expr: $ => choice(
      seq($.lower, t($).nsdot, $.lower),
      $.lower,
    ),

    boolean: $ => choice(t($).ktrue, t($).kfalse),

    _num_expr: $ => choice(
      $.integer,
      $.float,
      $.inf,
      $.neg_inf,
      $.nan,
    ),

    inf: $ => t($).inf,
    neg_inf: $ => t($).neginf,
    nan: $ => t($).nan,

    _string_expr: $ => choice($.string, $.interp_string),

    interp_string: $ => seq(
      $.string_start,
      $._interp_open,
      $._expr,
      $._interp_close,
      repeat(seq(
        $.string_mid,
        $._interp_open,
        $._expr,
        $._interp_close,
      )),
      $.string_end,
    ),

    _evar_or_op: $ => choice($.lower, $.operator_ref_paren),
  },
});
