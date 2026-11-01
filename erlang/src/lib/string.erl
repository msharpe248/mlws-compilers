%% string for mlws-vm-erlc: strings as lists of characters (binaries are
%% turned into lists where it matters).
-module(string).
-export([tokens/2, join/2, concat/2, len/1, length/1, to_upper/1, to_lower/1, uppercase/1,
         lowercase/1, strip/1, strip/2, strip/3, substr/2, substr/3, sub_string/2,
         sub_string/3, str/2, rstr/2, chr/2, rchr/2, copies/2, words/1, words/2,
         left/2, left/3, right/2, right/3, centre/2, centre/3, split/2, split/3,
         trim/1, trim/2, trim/3, find/2, find/3, lexemes/2, slice/2, slice/3, reverse/1,
         is_empty/1, equal/2, prefix/2, replace/3, replace/4, pad/2, pad/3, pad/4,
         to_integer/1, to_float/1, chomp/1, list_to_integer/1, sub_word/2, sub_word/3,
         span/2, cspan/2, casefold/1, titlecase/1, next_grapheme/1]).

chars(B) when is_binary(B) -> unicode_list(B);
chars(L) when is_list(L) -> lists:flatten(L);
chars(A) when is_atom(A) -> atom_to_list(A).

unicode_list(B) -> atom_to_list('$rt':bin_str(B)).

%% Results are binaries when the subject was one.
like(B, S) when is_binary(B) -> unicode:characters_to_binary(S);
like(_, S) -> S.

tokens(S, Seps) -> tokens1(S, Seps, []).
tokens1([C | Cs], Seps, Acc) ->
    case lists:member(C, Seps) of
        true -> tokens1(Cs, Seps, Acc);
        false -> tokens2(Cs, Seps, Acc, [C])
    end;
tokens1([], _, Acc) -> lists:reverse(Acc).
tokens2([C | Cs], Seps, Acc, Tok) ->
    case lists:member(C, Seps) of
        true -> tokens1(Cs, Seps, [lists:reverse(Tok) | Acc]);
        false -> tokens2(Cs, Seps, Acc, [C | Tok])
    end;
tokens2([], _, Acc, Tok) -> lists:reverse([lists:reverse(Tok) | Acc]).

join([], _) -> [];
join([S], _) -> S;
join([S | Ss], Sep) -> S ++ Sep ++ join(Ss, Sep).

concat(A, B) -> A ++ B.

len(S) -> erlang:length(S).
length(S) -> erlang:length(chars(S)).

to_upper(C) when is_integer(C) -> up(C);
to_upper(S) -> [up(C) || C <- S].
to_lower(C) when is_integer(C) -> low(C);
to_lower(S) -> [low(C) || C <- S].

up(C) when C >= $a, C =< $z -> C - 32;
up(C) when C >= 224, C =< 254, C =/= 247 -> C - 32;
up(C) -> C.
low(C) when C >= $A, C =< $Z -> C + 32;
low(C) when C >= 192, C =< 222, C =/= 215 -> C + 32;
low(C) -> C.

uppercase(S) -> like(S, to_upper(chars(S))).
lowercase(S) -> like(S, to_lower(chars(S))).
casefold(S) -> lowercase(S).
titlecase(S) ->
    case chars(S) of
        [C | Cs] -> like(S, [up(C) | Cs]);
        [] -> S
    end.

strip(S) -> strip(S, both, $\s).
strip(S, Dir) -> strip(S, Dir, $\s).
strip(S, left, C) -> lists:dropwhile(fun(X) -> X =:= C end, S);
strip(S, right, C) -> lists:reverse(lists:dropwhile(fun(X) -> X =:= C end, lists:reverse(S)));
strip(S, both, C) -> strip(strip(S, left, C), right, C).

substr(S, Start) -> lists:nthtail(Start - 1, S).
substr(S, Start, Len) -> lists:sublist(lists:nthtail(Start - 1, S), Len).
sub_string(S, Start) -> substr(S, Start).
sub_string(S, Start, Stop) -> substr(S, Start, Stop - Start + 1).

str(S, Sub) -> str(S, Sub, 1).
str([_ | T] = S, Sub, I) ->
    case lists:prefix(Sub, S) of
        true -> I;
        false -> str(T, Sub, I + 1)
    end;
str([], _, _) -> 0.

rstr(S, Sub) -> rstr(S, Sub, 1, 0).
rstr([_ | T] = S, Sub, I, Last) ->
    case lists:prefix(Sub, S) of
        true -> rstr(T, Sub, I + 1, I);
        false -> rstr(T, Sub, I + 1, Last)
    end;
rstr([], _, _, Last) -> Last.

chr(S, C) -> chr(S, C, 1).
chr([C | _], C, I) -> I;
chr([_ | T], C, I) -> chr(T, C, I + 1);
chr([], _, _) -> 0.

rchr(S, C) -> rchr(S, C, 1, 0).
rchr([C | T], C, I, _) -> rchr(T, C, I + 1, I);
rchr([_ | T], C, I, L) -> rchr(T, C, I + 1, L);
rchr([], _, _, L) -> L.

span(S, Cs) -> erlang:length(lists:takewhile(fun(C) -> lists:member(C, Cs) end, S)).
cspan(S, Cs) -> erlang:length(lists:takewhile(fun(C) -> not lists:member(C, Cs) end, S)).

copies(S, N) -> lists:append(lists:duplicate(N, S)).

words(S) -> words(S, $\s).
words(S, C) -> erlang:length(tokens(S, [C])).

sub_word(S, N) -> sub_word(S, N, $\s).
sub_word(S, N, C) ->
    Ws = tokens(S, [C]),
    case N =< erlang:length(Ws) of
        true -> lists:nth(N, Ws);
        false -> []
    end.

left(S, N) -> left(S, N, $\s).
left(S, N, C) ->
    L = erlang:length(S),
    if
        L >= N -> lists:sublist(S, N);
        true -> S ++ lists:duplicate(N - L, C)
    end.

right(S, N) -> right(S, N, $\s).
right(S, N, C) ->
    L = erlang:length(S),
    if
        L >= N -> lists:nthtail(L - N, S);
        true -> lists:duplicate(N - L, C) ++ S
    end.

centre(S, N) -> centre(S, N, $\s).
centre(S, N, C) ->
    L = erlang:length(S),
    if
        L >= N -> lists:sublist(S, (L - N) div 2 + 1, N);
        true ->
            Left = (N - L) div 2,
            lists:duplicate(Left, C) ++ S ++ lists:duplicate(N - L - Left, C)
    end.

%% split(S, Pattern) / split(S, Pattern, leading | trailing | all)
split(S, P) -> split(S, P, leading).
split(S0, P0, Where) ->
    S = chars(S0),
    P = chars(P0),
    R = case Where of
            leading -> split_first(S, P, []);
            trailing ->
                case split_first(lists:reverse(S), lists:reverse(P), []) of
                    [A] -> [lists:reverse(A)];
                    [A, B] -> [lists:reverse(B), lists:reverse(A)]
                end;
            all -> split_all(S, P, [], [])
        end,
    [like(S0, X) || X <- R].

split_first([], _, Acc) -> [lists:reverse(Acc)];
split_first(S, P, Acc) ->
    case P =/= [] andalso lists:prefix(P, S) of
        true -> [lists:reverse(Acc), lists:nthtail(erlang:length(P), S)];
        false -> [C | Cs] = S, split_first(Cs, P, [C | Acc])
    end.

split_all([], _, Acc, Parts) -> lists:reverse([lists:reverse(Acc) | Parts]);
split_all(S, P, Acc, Parts) ->
    case P =/= [] andalso lists:prefix(P, S) of
        true -> split_all(lists:nthtail(erlang:length(P), S), P, [], [lists:reverse(Acc) | Parts]);
        false -> [C | Cs] = S, split_all(Cs, P, [C | Acc], Parts)
    end.

trim(S) -> trim(S, both).
trim(S, Dir) -> trim(S, Dir, " \t\n\r").
trim(S0, Dir, Cs0) ->
    S = chars(S0),
    Cs = case Cs0 of [C | _] when is_list(C) -> lists:flatten(Cs0); _ -> Cs0 end,
    Drop = fun(L) -> lists:dropwhile(fun(C) -> lists:member(C, Cs) end, L) end,
    R = case Dir of
            leading -> Drop(S);
            trailing -> lists:reverse(Drop(lists:reverse(S)));
            both -> lists:reverse(Drop(lists:reverse(Drop(S))))
        end,
    like(S0, R).

chomp(S0) ->
    S = chars(S0),
    R = case lists:reverse(S) of
            [$\n, $\r | T] -> lists:reverse(T);
            [$\n | T] -> lists:reverse(T);
            _ -> S
        end,
    like(S0, R).

find(S, P) -> find(S, P, leading).
find(S0, P0, leading) ->
    case find_first(chars(S0), chars(P0)) of
        nomatch -> nomatch;
        R -> like(S0, R)
    end;
find(S0, P0, trailing) ->
    S = chars(S0),
    P = chars(P0),
    case find_last(S, P, nomatch) of
        nomatch -> nomatch;
        R -> like(S0, R)
    end.

find_first([], []) -> [];
find_first([], _) -> nomatch;
find_first([_ | T] = S, P) ->
    case lists:prefix(P, S) of
        true -> S;
        false -> find_first(T, P)
    end.

find_last([], _, Last) -> Last;
find_last([_ | T] = S, P, Last) ->
    case lists:prefix(P, S) of
        true -> find_last(T, P, S);
        false -> find_last(T, P, Last)
    end.

lexemes(S0, Seps0) ->
    Seps = case Seps0 of [C | _] when is_list(C) -> lists:flatten(Seps0); _ -> Seps0 end,
    [like(S0, T) || T <- tokens(chars(S0), Seps)].

slice(S, Start) -> like(S, lists:nthtail(min(Start, erlang:length(chars(S))), chars(S))).
slice(S, Start, Len) ->
    Cs = chars(S),
    like(S, lists:sublist(lists:nthtail(min(Start, erlang:length(Cs)), Cs), Len)).

reverse(S) -> like(S, lists:reverse(chars(S))).

is_empty(S) -> chars(S) =:= [].

equal(A, B) -> chars(A) =:= chars(B).

prefix(S, P) ->
    Cs = chars(S),
    Pc = chars(P),
    case lists:prefix(Pc, Cs) of
        true -> like(S, lists:nthtail(erlang:length(Pc), Cs));
        false -> nomatch
    end.

replace(S, P, R) -> replace(S, P, R, leading).
replace(S, P, R, Where) ->
    Parts = split(S, P, Where),
    Sep = case is_binary(S) of true -> R; false -> chars(R) end,
    case is_binary(S) of
        true -> lists:join(Sep, Parts);
        false -> lists:join(Sep, Parts)
    end.

pad(S, N) -> pad(S, N, trailing, $\s).
pad(S, N, Dir) -> pad(S, N, Dir, $\s).
pad(S, N, Dir, C) ->
    L = erlang:length(chars(S)),
    if
        L >= N -> S;
        Dir =:= leading -> [lists:duplicate(N - L, C), S];
        Dir =:= trailing -> [S, lists:duplicate(N - L, C)];
        true ->
            Left = (N - L) div 2,
            [lists:duplicate(Left, C), S, lists:duplicate(N - L - Left, C)]
    end.

to_integer(S0) ->
    S = chars(S0),
    {Ds, Rest} = case S of
                     [Sign | T] when Sign =:= $-; Sign =:= $+ ->
                         {D, R} = lists:splitwith(fun(C) -> C >= $0 andalso C =< $9 end, T),
                         {[Sign | D], R};
                     _ -> lists:splitwith(fun(C) -> C >= $0 andalso C =< $9 end, S)
                 end,
    case Ds of
        [] -> {error, no_integer};
        [_] when hd(Ds) =:= $-; hd(Ds) =:= $+ -> {error, no_integer};
        _ -> {list_to_integer(Ds), like(S0, Rest)}
    end.

list_to_integer(S) -> to_integer(S).

to_float(S0) ->
    S = chars(S0),
    {Ds, Rest} = lists:splitwith(fun(C) -> (C >= $0 andalso C =< $9) orelse C =:= $. orelse C =:= $-
                                               orelse C =:= $+ orelse C =:= $e orelse C =:= $E end, S),
    case catch list_to_float(Ds) of
        F when is_float(F) -> {F, like(S0, Rest)};
        _ -> {error, no_float}
    end.

next_grapheme(S) ->
    case chars(S) of
        [C | Cs] -> [C | like(S, Cs)];
        [] -> []
    end.
