%% re, on MLWSVM's regular expressions (regex.*): RE2's syntax, which is
%% Perl's without backreferences and lookaround, matched in linear time.
%% Named groups are (?<name>...) or (?P<name>...).
%% Offsets are bytes, as in OTP. A compiled pattern is
%% {re_pattern, Pattern, Flags, Unicode}.
-module(re).
-export([compile/1, compile/2, run/2, run/3, replace/3, replace/4,
         split/2, split/3, inspect/2]).

compile(RE) -> compile(RE, []).

compile(RE, Opts) ->
    try {ok, pattern(RE, Opts)}
    catch error:badarg -> {error, {"bad pattern", 0}}
    end.

run(S, RE) -> run(S, RE, []).

run(S, RE, Opts) ->
    P = pattern(RE, Opts),
    Bin = subject(S, P),
    {Spec, Type} = case capture(Opts) of
                       {all_names, T} -> {spec(sorted_names(P)), T};
                       C -> C
                   end,
    Off = proplists:get_value(offset, Opts, 0),
    case lists:member(global, Opts) of
        false ->
            case find(P, Bin, Off) of
                nomatch -> nomatch;
                M when Spec =:= none -> _ = M, match;
                M -> {match, captured(M, Spec, Type, Bin, P)}
            end;
        true ->
            case all(P, Bin, Off) of
                [] -> nomatch;
                _ when Spec =:= none -> match;
                Ms -> {match, [captured(M, Spec, Type, Bin, P) || M <- Ms]}
            end
    end.

replace(S, RE, Repl) -> replace(S, RE, Repl, []).

replace(S, RE, Repl, Opts) ->
    P = pattern(RE, Opts),
    Bin = subject(S, P),
    R = unicode:characters_to_binary(Repl),
    Ms = case lists:member(global, Opts) of
             true -> all(P, Bin, 0);
             false -> case find(P, Bin, 0) of nomatch -> []; M -> [M] end
         end,
    Out = iolist_to_binary(replaced(Ms, Bin, R, 0)),
    result(Out, proplists:get_value(return, Opts, iodata), P).

%% The names of the pattern's named groups, in alphabetical order.
inspect({re_pattern, _, _, _} = P, namelist) -> {namelist, sorted_names(P)};
inspect(_, _) -> erlang:error(badarg).

split(S, RE) -> split(S, RE, []).

split(S, RE, Opts) ->
    P = pattern(RE, Opts),
    Bin = subject(S, P),
    Parts = case proplists:get_value(parts, Opts, infinity) of
                N when is_integer(N), N > 0 -> N;
                _ -> infinity
            end,
    Ms = splits(all(P, Bin, 0), Parts),
    Pieces = pieces(Ms, Bin, 0),
    Trimmed = case lists:member(trim, Opts) orelse
                  proplists:get_value(parts, Opts, infinity) =:= 0 of
                  true -> trim(Pieces);
                  false -> Pieces
              end,
    Ret = proplists:get_value(return, Opts, iodata),
    case lists:member(group, Opts) of
        false -> [result(X, Ret, P) || X <- lists:append(Trimmed)];
        true -> [[result(X, Ret, P) || X <- G] || G <- Trimmed]
    end.

%% The pattern, compiled options and all.
pattern({re_pattern, _, _, _} = P, _) -> P;
pattern(RE, Opts) ->
    U = lists:member(unicode, Opts),
    Pat = case U of
              true -> unicode:characters_to_binary(RE);
              false -> iolist_to_binary(RE)
          end,
    Flags = [F || {O, F} <- [{caseless, $i}, {multiline, $m}, {dotall, $s}],
                  lists:member(O, Opts)],
    P = {re_pattern, Pat, list_to_atom(Flags), U},
    _ = find(P, <<>>, 0),           % badarg now for a bad pattern
    P.

subject(S, _) when is_binary(S) -> S;
subject(S, {re_pattern, _, _, true}) -> unicode:characters_to_binary(S);
subject(S, _) -> iolist_to_binary(S).

result(B, binary, _) -> B;
result(B, iodata, _) -> B;
result(B, list, {re_pattern, _, _, true}) -> unicode:characters_to_list(B);
result(B, list, _) -> binary_to_list(B).

%% Each group's name (a binary, or [] for one without), by group number - 1.
names({re_pattern, Pat, Flags, _}) -> '$rt':re_names(Pat, Flags).

sorted_names(P) -> lists:sort([N || N <- tuple_to_list(names(P)), N =/= []]).

%% No values to capture: just match.
spec([]) -> none;
spec(Ns) -> Ns.

capture(Opts) ->
    case lists:keyfind(capture, 1, Opts) of
        {capture, Spec} -> {Spec, index};
        {capture, Spec, Type} -> {Spec, Type};
        false -> {all, index}
    end.

%% A match: a list of {Start, End} for the match and each group.
find({re_pattern, Pat, Flags, _}, Bin, Off) when Off =< byte_size(Bin) ->
    case '$rt':re_find(Pat, Flags, Bin, Off) of
        nomatch -> nomatch;
        T -> pairs(tuple_to_list(T))
    end;
find(_, _, _) -> nomatch.

pairs([S, E | T]) -> [{S, E} | pairs(T)];
pairs([]) -> [].

%% Every match from Off on; after an empty one, the next starts a byte on.
all(P, Bin, Off) ->
    case find(P, Bin, Off) of
        nomatch -> [];
        [{S, E} | _] = M when E =:= S -> [M | all(P, Bin, E + 1)];
        [{_, E} | _] = M -> [M | all(P, Bin, E)]
    end.

captured(M, Spec, Type, Bin, P) ->
    Gs = case Spec of
             all -> set(M);
             first -> [hd(M)];
             all_but_first -> tl(set(M));
             Ns when is_list(Ns) -> [group(N, M, P) || N <- Ns]
         end,
    [value(G, Type, Bin, P) || G <- Gs].

%% Without the groups after the last that took part, as PCRE reports.
set(M) -> lists:reverse(lists:dropwhile(fun({S, _}) -> S =:= -1 end, lists:reverse(M))).

group(N, M, _) when is_integer(N), N >= 0, N < length(M) -> lists:nth(N + 1, M);
group(N, _, _) when is_integer(N) -> {-1, -1};
group(N, M, P) when is_atom(N) -> group(atom_to_binary(N, utf8), M, P);
group(N, M, P) when is_list(N) -> group(unicode:characters_to_binary(N), M, P);
group(N, M, P) when is_binary(N) -> group(number(N, tuple_to_list(names(P)), 1), M, P);
group(_, _, _) -> erlang:error(badarg).

%% A named group's number; -1 (not taking part) for a name it does not have.
number(N, [N | _], K) -> K;
number(N, [_ | Ns], K) -> number(N, Ns, K + 1);
number(_, [], _) -> -1.

value({-1, _}, index, _, _) -> {-1, 0};
value({S, E}, index, _, _) -> {S, E - S};
value({-1, _}, binary, _, _) -> <<>>;
value({-1, _}, list, _, _) -> [];
value({S, E}, Type, Bin, P) -> result(binary_part(Bin, S, E - S), Type, P).

%% The text with each match replaced: & is the match, \N and \g{N} group N.
replaced([], Bin, _, At) -> [binary_part(Bin, At, byte_size(Bin) - At)];
replaced([[{S, E} | _] = M | Ms], Bin, R, At) ->
    [binary_part(Bin, At, S - At), expand(R, M, Bin) | replaced(Ms, Bin, R, E)].

expand(<<$\\, $&, R/binary>>, M, Bin) -> [$& | expand(R, M, Bin)];
expand(<<$\\, $\\, R/binary>>, M, Bin) -> [$\\ | expand(R, M, Bin)];
expand(<<$\\, $g, ${, R/binary>>, M, Bin) ->
    {N, <<$}, R1/binary>>} = digits(R, 0),
    [part(N, M, Bin) | expand(R1, M, Bin)];
expand(<<$\\, D, _/binary>> = R0, M, Bin) when D >= $0, D =< $9 ->
    <<$\\, R/binary>> = R0,
    {N, R1} = digits(R, 0),
    [part(N, M, Bin) | expand(R1, M, Bin)];
expand(<<$&, R/binary>>, M, Bin) -> [part(0, M, Bin) | expand(R, M, Bin)];
expand(<<C, R/binary>>, M, Bin) -> [C | expand(R, M, Bin)];
expand(<<>>, _, _) -> [].

digits(<<D, R/binary>>, N) when D >= $0, D =< $9 -> digits(R, N * 10 + D - $0);
digits(R, N) -> {N, R}.

part(N, M, Bin) when N < length(M) ->
    case lists:nth(N + 1, M) of
        {-1, _} -> <<>>;
        {S, E} -> binary_part(Bin, S, E - S)
    end;
part(_, _, _) -> <<>>.

%% The matches split cuts at: not an empty one where the last cut ended (or
%% at the start), and at most Parts - 1 of them.
splits(Ms, Parts) ->
    Cuts = cuts(Ms, 0),
    case Parts of
        infinity -> Cuts;
        N -> lists:sublist(Cuts, N - 1)
    end.

cuts([[{S, S} | _] | Ms], S) -> cuts(Ms, S);
cuts([[{_, E} | _] = M | Ms], _) -> [M | cuts(Ms, E)];
cuts([], _) -> [].

%% Each piece of text, with the groups of the match after it.
pieces([], Bin, At) -> [[binary_part(Bin, At, byte_size(Bin) - At)]];
pieces([[{S, E} | Gs] | Ms], Bin, At) ->
    [[binary_part(Bin, At, S - At) | [grouptext(G, Bin) || G <- Gs]] | pieces(Ms, Bin, E)].

grouptext({-1, _}, _) -> <<>>;
grouptext({S, E}, Bin) -> binary_part(Bin, S, E - S).

trim(Pieces) ->
    lists:reverse(lists:dropwhile(fun(G) -> lists:all(fun(B) -> B =:= <<>> end, G) end,
                                  lists:reverse(Pieces))).
