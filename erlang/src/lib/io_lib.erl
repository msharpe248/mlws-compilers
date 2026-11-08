%% io_lib for mlws-vm-erlc: format/2 and writing terms. ~p lays terms out
%% as OTP's io_lib_pretty does (line length 80, tagged tuples indented
%% after their tag), without depth limits or records.
-module(io_lib).
-export([format/2, fwrite/2, write/1, write/2, print/1, print/4, write_atom/1,
         write_string/1, write_string/2, write_char/1, write_binary/1, write_float/1,
         printable_list/1, printable_latin1_list/1, printable_unicode_list/1,
         char_list/1, deep_char_list/1, latin1_char_list/1, nl/0, chars_length/1,
         indentation/2, float_short/1, quote_atom/2]).

nl() -> "\n".

format(Fmt, Args) -> fwrite(Fmt, Args).

fwrite(Fmt, Args) when is_atom(Fmt) -> fwrite(atom_to_list(Fmt), Args);
fwrite(Fmt, Args) when is_binary(Fmt) -> fwrite(binary_to_list(Fmt), Args);
fwrite(Fmt, Args) when is_list(Fmt), is_list(Args) ->
    fmt(lists:flatten(Fmt), Args, 0, []);
fwrite(_, _) ->
    erlang:error(badarg).

%% Col: the characters since the last newline.
fmt([], [], _Col, Acc) ->
    lists:reverse(Acc);
fmt([], _, _, _) ->
    erlang:error(badarg);
fmt([$~ | Cs0], Args0, Col, Acc) ->
    {F0, Cs1, Args1} = field(Cs0, Args0),
    {P, Cs2, Args2} =
        case Cs1 of
            [$. | T1] -> field(T1, Args1);
            _ -> {none, Cs1, Args1}
        end,
    {Pad, Cs3, Args3} =
        case Cs2 of
            [$., $* | T2] -> [Pc | As] = Args2, {Pc, T2, As};
            [$., Pc | T2] -> {Pc, T2, Args2};
            _ -> {$\s, Cs2, Args2}
        end,
    Cs4 = modifiers(Cs3),
    {Adj, F} =
        case F0 of
            {left, N} -> {left, N};
            N when is_integer(N), N < 0 -> {left, -N};
            N -> {right, N}
        end,
    case Cs4 of
        [C | Cs5] ->
            {Out, Args4} = control(C, F, Adj, P, Pad, Args3, Col),
            fmt(Cs5, Args4, column(Out, Col), [Out | Acc]);
        [] ->
            erlang:error(badarg)
    end;
fmt([$\n | Cs], Args, _Col, Acc) ->
    fmt(Cs, Args, 0, [$\n | Acc]);
fmt([C | Cs], Args, Col, Acc) ->
    fmt(Cs, Args, Col + 1, [C | Acc]).

field([$- | Cs], Args) ->
    case field(Cs, Args) of
        {N, Cs1, Args1} when is_integer(N) -> {{left, N}, Cs1, Args1};
        R -> R
    end;
field([$* | Cs], [A | As]) when is_integer(A) -> {A, Cs, As};
field([C | _] = Cs, Args) when C >= $0, C =< $9 ->
    {N, Rest} = digits(Cs, 0),
    {N, Rest, Args};
field(Cs, Args) ->
    {none, Cs, Args}.

digits([C | Cs], N) when C >= $0, C =< $9 -> digits(Cs, N * 10 + C - $0);
digits(Cs, N) -> {N, Cs}.

modifiers([$t | Cs]) -> modifiers(Cs);
modifiers([$l | Cs]) -> modifiers(Cs);
modifiers([$k | Cs]) -> modifiers(Cs);
modifiers([$K | Cs]) -> modifiers(Cs);
modifiers(Cs) -> Cs.

%% The column after writing some (deep) characters.
column(Out, Col) -> col(lists:flatten(Out), Col).

col([], Col) -> Col;
col([$\n | Cs], _) -> col(Cs, 0);
col([_ | Cs], Col) -> col(Cs, Col + 1).

control($~, F, Adj, _P, Pad, Args, _Col) -> {adjust("~", F, Adj, Pad), Args};
control($n, _F, _Adj, _P, _Pad, Args, _Col) -> {"\n", Args};
control($i, _F, _Adj, _P, _Pad, [_ | Args], _Col) -> {[], Args};
control($w, F, Adj, P, Pad, [A | Args], _Col) ->
    {term(write(A), F, Adj, P, Pad), Args};
control($W, F, Adj, P, Pad, [A, _D | Args], _Col) ->
    {term(write(A), F, Adj, P, Pad), Args};
control($p, F, _Adj, P, _Pad, [A | Args], Col) ->
    {pretty(A, F, P, Col), Args};
control($P, F, _Adj, P, _Pad, [A, _D | Args], Col) ->
    {pretty(A, F, P, Col), Args};
control($s, F, Adj, P, Pad, [A | Args], _Col) ->
    {string(chars_of(A), F, Adj, P, Pad), Args};
control($c, F, Adj, P, Pad, [A | Args], _Col) when is_integer(A) ->
    N = case {P, F} of {none, none} -> 1; {none, _} -> F; _ -> P end,
    {adjust(lists:duplicate(N, A), F, Adj, Pad), Args};
control($b, F, Adj, P, Pad, [A | Args], _Col) when is_integer(A) ->
    {term(lower(integer_to_list(A, base(P))), F, Adj, none, Pad), Args};
control($B, F, Adj, P, Pad, [A | Args], _Col) when is_integer(A) ->
    {term(integer_to_list(A, base(P)), F, Adj, none, Pad), Args};
control($x, F, Adj, P, Pad, [A, Prefix | Args], _Col) when is_integer(A) ->
    {term(prefixed(Prefix, lower(integer_to_list(A, base(P)))), F, Adj, none, Pad), Args};
control($X, F, Adj, P, Pad, [A, Prefix | Args], _Col) when is_integer(A) ->
    {term(prefixed(Prefix, integer_to_list(A, base(P))), F, Adj, none, Pad), Args};
control($#, F, Adj, P, Pad, [A | Args], _Col) when is_integer(A) ->
    B = base(P),
    {term(prefixed(integer_to_list(B) ++ "#", integer_to_list(A, B)), F, Adj, none, Pad), Args};
control($+, F, Adj, P, Pad, [A | Args], _Col) when is_integer(A) ->
    B = base(P),
    {term(prefixed(integer_to_list(B) ++ "#", lower(integer_to_list(A, B))), F, Adj, none, Pad), Args};
control($f, F, Adj, P, Pad, [A | Args], _Col) when is_number(A) ->
    {term(fmt_f(float(A), prec(P, 6)), F, Adj, none, Pad), Args};
control($e, F, Adj, P, Pad, [A | Args], _Col) when is_number(A) ->
    {term(fmt_e(float(A), prec(P, 6)), F, Adj, none, Pad), Args};
control($g, F, Adj, P, Pad, [A | Args], _Col) when is_number(A) ->
    {term(fmt_g(float(A), prec(P, 6)), F, Adj, none, Pad), Args};
control(_, _, _, _, _, _, _) ->
    erlang:error(badarg).

base(none) -> 10;
base(B) when is_integer(B), B >= 2, B =< 36 -> B;
base(_) -> erlang:error(badarg).

prec(none, D) -> D;
prec(P, _) -> P.

prefixed(Prefix, [$- | Ds]) -> [$- | chars_of(Prefix) ++ Ds];
prefixed(Prefix, Ds) -> chars_of(Prefix) ++ Ds.

lower([C | Cs]) when C >= $A, C =< $Z -> [C + 32 | lower(Cs)];
lower([C | Cs]) -> [C | lower(Cs)];
lower([]) -> [].

chars_of(A) when is_atom(A) -> atom_to_list(A);
chars_of(B) when is_binary(B) -> unicode_chars(B);
chars_of(L) when is_list(L) -> flat_chars(L, []);
chars_of(_) -> erlang:error(badarg).

flat_chars([C | Cs], Acc) when is_integer(C) -> flat_chars(Cs, [C | Acc]);
flat_chars([L | Cs], Acc) when is_list(L) -> flat_chars(Cs, lists:reverse(flat_chars(L, []), Acc));
flat_chars([B | Cs], Acc) when is_binary(B) -> flat_chars(Cs, lists:reverse(unicode_chars(B), Acc));
flat_chars([], Acc) -> lists:reverse(Acc);
flat_chars(B, Acc) when is_binary(B) -> lists:reverse(Acc, unicode_chars(B));
flat_chars(_, _) -> erlang:error(badarg).

unicode_chars(B) -> atom_to_list('$rt':bin_str(B)).

%% A term: padded to F, or all stars if it does not fit.
term(S, none, _Adj, none, _Pad) -> S;
term(S, none, Adj, P, Pad) -> term(S, P, Adj, P, Pad);
term(S, F, Adj, _P, Pad) ->
    L = lists:flatlength(S),
    if
        L > F -> lists:duplicate(F, $*);
        true -> adjust(S, F, Adj, Pad)
    end.

%% A string: cut to P characters (or F), then padded to F.
string(S, none, _Adj, none, _Pad) -> S;
string(S, F, Adj, none, Pad) -> string(S, F, Adj, F, Pad);
string(S, none, _Adj, P, _Pad) -> take(S, P);
string(S, F, Adj, P, Pad) -> adjust(take(S, min(P, F)), F, Adj, Pad).

take(_, 0) -> [];
take([C | Cs], N) -> [C | take(Cs, N - 1)];
take([], _) -> [].

adjust(S, none, _Adj, _Pad) -> S;
adjust(S, F, Adj, Pad) ->
    L = lists:flatlength(S),
    if
        L >= F -> S;
        Adj =:= left -> [S, lists:duplicate(F - L, Pad)];
        true -> [lists:duplicate(F - L, Pad), S]
    end.

%% ---------------------------------------------------------------- floats

fmt_f(X, P) -> atom_to_list('$rt':fmt_float(X, P, 0)).

%% ~e: P significant digits, the exponent without leading zeros.
fmt_e(X, P) when P >= 2 ->
    S = atom_to_list('$rt':fmt_float(X, P - 1, 1)),
    fix_exp(S);
fmt_e(_, _) -> erlang:error(badarg).

fix_exp([$e, Sign | Ds]) -> [$e, Sign | strip_zeros(Ds)];
fix_exp([C | Cs]) -> [C | fix_exp(Cs)];
fix_exp([]) -> [].

strip_zeros([$0, D | Ds]) -> strip_zeros([D | Ds]);
strip_zeros(Ds) -> Ds.

fmt_g(X, P) ->
    A = abs(X),
    E = if A < 0.1 -> -2; true -> exponent(A, 0) end,
    if
        P =< 1, E =:= -1 -> fmt_f(X, P - 1 - E);
        P - 1 > E, E >= -1 -> fmt_f(X, P - 1 - E);
        P =< 1 -> fmt_e(X, 2);
        true -> fmt_e(X, P)
    end.

%% floor(log10(A)) for A >= 0.1
exponent(A, E) when A < 1.0 -> E - 1;
exponent(A, E) when A < 10.0 -> E;
exponent(A, E) -> exponent(A / 10, E + 1).

%% The shortest form that reads back as the same float: "0.1", "1.0e10".
write_float(X) -> float_short(X).

float_short(X) ->
    S = atom_to_list('$rt':float_str(X)),
    {Sign, S1} = case S of [$- | T] -> {"-", T}; _ -> {"", S} end,
    {Mant, Exp} = split_exp(S1, []),
    {Int, Frac} = split_dot(Mant, []),
    Digits0 = Int ++ Frac,
    %% value = 0.Digits * 10^E
    {Digits1, E0} = strip_leading(Digits0, length(Int) + Exp),
    Digits = strip_trailing(Digits1),
    case Digits of
        [] -> Sign ++ "0.0";
        _ ->
            Fixed = fixed(Digits, E0),
            Sci = sci(Digits, E0),
            case length(Fixed) =< length(Sci) of
                true -> Sign ++ Fixed;
                false -> Sign ++ Sci
            end
    end.

split_exp([$e | Ds], Acc) -> {lists:reverse(Acc), list_to_integer(Ds)};
split_exp([C | Cs], Acc) -> split_exp(Cs, [C | Acc]);
split_exp([], Acc) -> {lists:reverse(Acc), 0}.

split_dot([$. | Cs], Acc) -> {lists:reverse(Acc), Cs};
split_dot([C | Cs], Acc) -> split_dot(Cs, [C | Acc]);
split_dot([], Acc) -> {lists:reverse(Acc), []}.

strip_leading([$0 | Ds], E) -> strip_leading(Ds, E - 1);
strip_leading(Ds, E) -> {Ds, E}.

strip_trailing(Ds) -> lists:reverse(strip_zeros_r(lists:reverse(Ds))).

strip_zeros_r([$0 | Ds]) -> strip_zeros_r(Ds);
strip_zeros_r(Ds) -> Ds.

fixed(Ds, E) when E =< 0 -> "0." ++ lists:duplicate(-E, $0) ++ Ds;
fixed(Ds, E) when E >= length(Ds) -> Ds ++ lists:duplicate(E - length(Ds), $0) ++ ".0";
fixed(Ds, E) -> {A, B} = lists:split(E, Ds), A ++ "." ++ B.

sci([D], E) -> [D] ++ ".0e" ++ integer_to_list(E - 1);
sci([D | Ds], E) -> [D, $. | Ds] ++ "e" ++ integer_to_list(E - 1).

%% ---------------------------------------------------------------- writing terms

write(T) -> write(T, -1).

write(T, _Depth) when is_integer(T) -> integer_to_list(T);
write(T, _Depth) when is_float(T) -> float_short(T);
write(T, _Depth) when is_atom(T) -> write_atom(T);
write(T, _Depth) when is_list(T) -> write_list(T, fun write/1);
write(T, _Depth) when is_tuple(T) -> write_tuple(T, fun write/1);
write(T, _Depth) when is_map(T) -> write_map(T, fun write/1);
write(T, _Depth) when is_binary(T) -> write_binary(T);
write(T, _Depth) -> write_other(T).

write_other(T) when is_pid(T) ->
    "<" ++ integer_to_list('$rt':pid_node(T)) ++ "." ++ integer_to_list('$rt':pid_num(T)) ++ ".0>";
write_other(T) when is_reference(T) -> "#Ref<0." ++ '$rt':ref_str(T) ++ ">";
write_other(T) when is_function(T) -> "#Fun<" ++ '$rt':fun_str(T) ++ ">";
write_other(_) -> "?".

write_list([], _W) -> "[]";
write_list([H | T], W) -> [$[, W(H) | write_tail(T, W)].

write_tail([], _W) -> "]";
write_tail([H | T], W) -> [$,, W(H) | write_tail(T, W)];
write_tail(T, W) -> [$|, W(T), $]].

write_tuple(T, W) ->
    case tuple_to_list(T) of
        [] -> "{}";
        [H | Es] -> [${, W(H), [[$,, W(E)] || E <- Es], $}]
    end.

write_map(M, W) ->
    case maps:to_list(M) of
        [] -> "#{}";
        [{K, V} | Ps] ->
            ["#{", W(K), " => ", W(V), [[$,, W(K1), " => ", W(V1)] || {K1, V1} <- Ps], $}]
    end.

write_binary(B) ->
    case binary_to_list(B) of
        [] -> "<<>>";
        [C | Cs] -> ["<<", integer_to_list(C), [[$,, integer_to_list(D)] || D <- Cs], ">>"]
    end.

%% ~p's form of a binary: a string if it is printable.
print_binary(B) ->
    L = binary_to_list(B),
    case L =/= [] andalso printable_latin1_list(L) of
        true -> ["<<", write_string(L), ">>"];
        false -> write_binary(B)
    end.

write_atom(A) ->
    Cs = atom_to_list(A),
    case quote_atom(A, Cs) of
        true -> write_quoted(Cs, $');
        false -> Cs
    end.

quote_atom(A, Cs) ->
    case Cs of
        [C | Rest] when C >= $a, C =< $z -> not (name_chars(Rest) andalso not reserved(A));
        [C | Rest] when C >= 223, C =< 255, C =/= 247 -> not (name_chars(Rest) andalso not reserved(A));
        _ -> true
    end.

name_chars([C | Cs]) when C >= $a, C =< $z; C >= $A, C =< $Z; C >= $0, C =< $9; C =:= $_; C =:= $@ ->
    name_chars(Cs);
name_chars([C | Cs]) when C >= 192, C =< 255, C =/= 215, C =/= 247 ->
    name_chars(Cs);
name_chars([]) -> true;
name_chars(_) -> false.

reserved(A) ->
    lists:member(A, ['after', 'and', 'andalso', 'band', 'begin', 'bnot', 'bor', 'bsl', 'bsr',
                     'bxor', 'case', 'catch', 'cond', 'div', 'else', 'end', 'fun', 'if', 'let',
                     'maybe', 'not', 'of', 'or', 'orelse', 'receive', 'rem', 'try', 'when', 'xor']).

write_string(S) -> write_quoted(S, $").

write_string(S, Q) -> write_quoted(S, Q).

write_quoted(Cs, Q) -> [Q, [quote_char(C, Q) || C <- Cs], Q].

quote_char(Q, Q) -> [$\\, Q];
quote_char($\\, _) -> "\\\\";
quote_char($\n, _) -> "\\n";
quote_char($\r, _) -> "\\r";
quote_char($\t, _) -> "\\t";
quote_char($\v, _) -> "\\v";
quote_char($\b, _) -> "\\b";
quote_char($\f, _) -> "\\f";
quote_char($\e, _) -> "\\e";
quote_char($\d, _) -> "\\d";
quote_char(C, _) when C < 32 -> [$\\, $^, C + 64];
quote_char(C, _) -> C.

write_char(C) -> [$$ | quote_char(C, -1)].

print(T) -> pretty(T, none, none, 0).

print(T, Col, Ll, _Depth) -> pp(T, Col, Ll).

printable_list(L) -> printable_latin1_list(L).

printable_latin1_list([C | Cs]) when is_integer(C), C >= 32, C =< 126 -> printable_latin1_list(Cs);
printable_latin1_list([C | Cs]) when is_integer(C), C >= 160, C =< 255 -> printable_latin1_list(Cs);
printable_latin1_list([C | Cs]) when C =:= $\n; C =:= $\r; C =:= $\t; C =:= $\v;
                                     C =:= $\b; C =:= $\f; C =:= $\e ->
    printable_latin1_list(Cs);
printable_latin1_list([]) -> true;
printable_latin1_list(_) -> false.

printable_unicode_list([C | Cs]) when is_integer(C), C >= 32, C =< 126 -> printable_unicode_list(Cs);
printable_unicode_list([C | Cs]) when is_integer(C), C >= 160, C < 16#D800 -> printable_unicode_list(Cs);
printable_unicode_list([C | Cs]) when is_integer(C), C > 16#DFFF, C < 16#FFFE -> printable_unicode_list(Cs);
printable_unicode_list([C | Cs]) when is_integer(C), C > 16#FFFF, C =< 16#10FFFF -> printable_unicode_list(Cs);
printable_unicode_list([C | Cs]) when C =:= $\n; C =:= $\r; C =:= $\t; C =:= $\v;
                                      C =:= $\b; C =:= $\f; C =:= $\e ->
    printable_unicode_list(Cs);
printable_unicode_list([]) -> true;
printable_unicode_list(_) -> false.

char_list([C | Cs]) when is_integer(C), C >= 0, C =< 16#10FFFF -> char_list(Cs);
char_list([]) -> true;
char_list(_) -> false.

latin1_char_list([C | Cs]) when is_integer(C), C >= 0, C =< 255 -> latin1_char_list(Cs);
latin1_char_list([]) -> true;
latin1_char_list(_) -> false.

deep_char_list([C | Cs]) when is_integer(C) -> deep_char_list(Cs);
deep_char_list([L | Cs]) when is_list(L) -> deep_char_list(L) andalso deep_char_list(Cs);
deep_char_list([]) -> true;
deep_char_list(_) -> false.

chars_length(S) -> lists:flatlength(S).

indentation([], I) -> I;
indentation([$\n | Cs], _) -> indentation(Cs, 0);
indentation([$\t | Cs], I) -> indentation(Cs, ((I + 8) div 8) * 8);
indentation([C | Cs], I) when is_integer(C) -> indentation(Cs, I + 1);
indentation([L | Cs], I) -> indentation(Cs, indentation(L, I)).

%% ---------------------------------------------------------------- ~p

%% ~p at column Col: line length F (80), first column P (Col + 1).
pretty(T, F, P, Col) ->
    Ll = case F of none -> 80; _ -> F end,
    C = case P of none -> Col + 1; _ -> P end,
    pp(T, C, Ll).

%% A term's layout: {Kind, Len, Flat} where Kind is atomic or a compound
%% {list, Elems, Tail}, {tuple, Elems} or {map, Pairs}.
pp(T, Col, Ll) ->
    {K, Len, Flat} = If = layout(T),
    if
        K =:= atomic -> Flat;
        Len < Ll - Col -> Flat;
        true -> pp_if(If, Col, Ll, indent(Col), 0)
    end.

layout(T) when is_list(T) ->
    case T =/= [] andalso printable_latin1_list(T) of
        true ->
            S = write_string(T),
            {atomic, lists:flatlength(S), S};
        false ->
            case T of
                [] -> {atomic, 2, "[]"};
                _ -> layout_list(T, [], 1)
            end
    end;
layout(T) when is_tuple(T) ->
    Es = [layout(E) || E <- tuple_to_list(T)],
    Len = case Es of [] -> 2; _ -> lists:sum([L || {_, L, _} <- Es]) + length(Es) + 1 end,
    Tagged = case tuple_to_list(T) of
                 [A, _ | _] when is_atom(A) -> true;
                 _ -> false
             end,
    {{tuple, Tagged, Es}, Len, flat_tuple(Es)};
layout(T) when is_map(T) ->
    Ps = [{layout(K), layout(V)} || {K, V} <- maps:to_list(T)],
    Len = case Ps of
              [] -> 3;
              _ -> lists:sum([LK + LV + 4 || {{_, LK, _}, {_, LV, _}} <- Ps]) + length(Ps) + 2
          end,
    {{map, Ps}, Len, flat_map(Ps)};
layout(T) when is_binary(T) ->
    S = print_binary(T),
    {atomic, lists:flatlength(S), S};
layout(T) ->
    S = write(T),
    {atomic, lists:flatlength(S), S}.

layout_list([H | T], Acc, Len) ->
    {_, L, _} = E = layout(H),
    layout_list(T, [E | Acc], Len + L + 1);
layout_list([], Acc, Len) ->
    Es = lists:reverse(Acc),
    {{list, Es, none}, Len, flat_list(Es, none)};
layout_list(Tail, Acc, Len) ->
    {_, L, _} = TE = layout(Tail),
    Es = lists:reverse(Acc),
    {{list, Es, TE}, Len + L + 1, flat_list(Es, TE)}.

flat_list([{_, _, F} | Es], Tail) ->
    [$[, F, [[$,, F1] || {_, _, F1} <- Es], case Tail of none -> []; {_, _, TF} -> [$|, TF] end, $]].

flat_tuple([]) -> "{}";
flat_tuple([{_, _, F} | Es]) -> [${, F, [[$,, F1] || {_, _, F1} <- Es], $}].

flat_map([]) -> "#{}";
flat_map([{{_, _, KF}, {_, _, VF}} | Ps]) ->
    ["#{", KF, " => ", VF, [[$,, KF1, " => ", VF1] || {{_, _, KF1}, {_, _, VF1}} <- Ps], $}].

indent(N) when N > 1 -> lists:duplicate(N - 1, $\s);
indent(_) -> [].

atm({K, _, _}) -> K =:= atomic.

pp_if({_, Len, Flat}, Col, Ll, _Ind, LD) when Len < Ll - Col - LD ->
    Flat;
pp_if({{list, Es, Tail}, _, _}, Col, Ll, Ind, LD) ->
    [$[, pp_list(Es, Tail, Col + 1, Ll, [$\s | Ind], LD, $|), $]];
pp_if({{tuple, true, [{_, TLen, TFlat} | Es]}, _, _}, Col, Ll, Ind, LD) ->
    TagInd = TLen + 2,
    [${, TFlat, $,, pp_list(Es, none, Col + TagInd, Ll, lists:duplicate(TagInd, $\s) ++ Ind, LD, $,), $}];
pp_if({{tuple, _, Es}, _, _}, Col, Ll, Ind, LD) ->
    [${, pp_list(Es, none, Col + 1, Ll, [$\s | Ind], LD, $,), $}];
pp_if({{map, Ps}, _, _}, Col, Ll, Ind, LD) ->
    ["#{", pp_map(Ps, Col + 2, Ll, "  " ++ Ind, LD), $}];
pp_if({_, _, Flat}, _Col, _Ll, _Ind, _LD) ->
    Flat.

last_depth([], none, LD) -> LD + 1;
last_depth(_, _, _) -> 0.

pp_list([E | Es], Tail, Col0, Ll, Ind, LD, S) ->
    {ES, WE} = pp_element(E, Col0, Ll, Ind, last_depth(Es, Tail, LD)),
    [ES | pp_tail(Es, Tail, Col0, Col0 + WE, Ll, Ind, LD, S)].

pp_element({_, Len, Flat} = E, Col, Ll, _Ind, LD) when Len < Ll - Col - LD ->
    case atm(E) of
        true -> {Flat, Len};
        false -> {Flat, Ll}
    end;
pp_element(E, Col, Ll, Ind, LD) ->
    {pp_if(E, Col, Ll, Ind, LD), Ll}.

pp_tail([], none, _Col0, _Col, _Ll, _Ind, _LD, _S) ->
    [];
pp_tail([], {_, Len, Flat} = E, Col0, Col, Ll, Ind, LD, S) ->
    case Len + 1 < Ll - Col - (LD + 1) andalso atm(E) of
        true -> [S | Flat];
        false -> [S, $\n, Ind | pp_if(E, Col0, Ll, Ind, LD + 1)]
    end;
pp_tail([{_, Len, Flat} = E | Es], Tail, Col0, Col, Ll, Ind, LD, S) ->
    LD1 = last_depth(Es, Tail, LD),
    ELen = 1 + Len,
    Fits = case LD1 of
               0 -> ELen + 1 < Ll - Col;
               _ -> ELen < Ll - Col - LD1
           end,
    case Fits andalso atm(E) of
        true ->
            [$,, Flat | pp_tail(Es, Tail, Col0, Col + ELen, Ll, Ind, LD, S)];
        false ->
            {ES, WE} = pp_element(E, Col0, Ll, Ind, LD1),
            [$,, $\n, Ind, ES | pp_tail(Es, Tail, Col0, Col0 + WE, Ll, Ind, LD, S)]
    end.

pp_map([P | Ps], Col, Ll, Ind, LD) ->
    {PS, PW} = pp_pair(P, Col, Ll, Ind, last_depth(Ps, none, LD)),
    [PS | pp_pairs_tail(Ps, Col, Col + PW, Ll, Ind, LD)].

pair_len({{_, LK, _}, {_, LV, _}}) -> LK + LV + 4.

pair_flat({{_, _, KF}, {_, _, VF}}) -> [KF, " => ", VF].

pair_atm({K, V}) -> atm(K) andalso atm(V).

pp_pairs_tail([], _Col0, _Col, _Ll, _Ind, _LD) ->
    [];
pp_pairs_tail([P | Ps], Col0, Col, Ll, Ind, LD) ->
    LD1 = last_depth(Ps, none, LD),
    ELen = 1 + pair_len(P),
    Fits = case LD1 of
               0 -> ELen + 1 < Ll - Col;
               _ -> ELen < Ll - Col - LD1
           end,
    case Fits andalso pair_atm(P) of
        true ->
            [$,, pair_flat(P) | pp_pairs_tail(Ps, Col0, Col + ELen, Ll, Ind, LD)];
        false ->
            {PS, PW} = pp_pair(P, Col0, Ll, Ind, LD1),
            [$,, $\n, Ind, PS | pp_pairs_tail(Ps, Col0, Col0 + PW, Ll, Ind, LD)]
    end.

pp_pair({K, V} = P, Col, Ll, Ind, LD) ->
    Len = pair_len(P),
    if
        Len < Ll - Col - LD ->
            case pair_atm(P) of
                true -> {pair_flat(P), Len};
                false -> {pair_flat(P), Ll}
            end;
        true ->
            Ind2 = "    " ++ Ind,
            {[pp_if(K, Col, Ll, Ind, LD), " =>\n", Ind2 | pp_if(V, Col + 4, Ll, Ind2, LD)], Ll}
    end.
