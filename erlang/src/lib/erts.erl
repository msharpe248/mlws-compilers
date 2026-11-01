%% Runtime support for mlws-vm-erlc, in Erlang: exit reasons sent between
%% processes as text (ser/deser), and crash reports.
-module(erts).
-export([ser/1, deser/1, report/2, main_error/2]).

%% A term as text (~w), as an atom-like string for the VM.
ser(T) -> '$rt':list_to_str(lists:flatten(io_lib:write(T))).

%% And back.
deser(S) ->
    case catch term(atom_to_list(S)) of
        {T, _} -> T;
        _ -> S
    end.

term([C | _] = S) when C >= $0, C =< $9 -> number(S);
term([$-, C | _] = S) when C >= $0, C =< $9 -> number(S);
term([$' | S]) -> quoted(S, []);
term([$[, $] | S]) -> {[], S};
term([$[ | S]) -> list(S, []);
term([${, $} | S]) -> {{}, S};
term([${ | S]) -> tuple(S, []);
term([$#, ${, $} | S]) -> {#{}, S};
term([$#, ${ | S]) -> map(S, #{});
term([$<, $<, $>, $> | S]) -> {<<>>, S};
term([$<, $< | S]) -> bin(S, []);
term([$<, $0, $. | S]) -> {N, [$., $0, $> | S1]} = number(S), {'$rt':make_pid(N), S1};
term("#Ref<0." ++ S) -> {A, [$. | S1]} = number(S), {B, [$> | S2]} = number(S1), {'$rt':make_ref2(A, B), S2};
term("#Fun<" ++ S) -> {'fun', lists:nthtail(1, lists:dropwhile(fun(C) -> C =/= $> end, S))};
term(S) -> atom(S, []).

number(S) ->
    {Ds, Rest} = lists:splitwith(fun(C) -> (C >= $0 andalso C =< $9) orelse C =:= $- orelse C =:= $.
                                          orelse C =:= $e orelse C =:= $+ end, S),
    case lists:member($., Ds) of
        true -> {list_to_float(Ds), Rest};
        false -> {list_to_integer(Ds), Rest}
    end.

atom([C | S], Acc) when C >= $a, C =< $z; C >= $A, C =< $Z; C >= $0, C =< $9; C =:= $_; C =:= $@ ->
    atom(S, [C | Acc]);
atom(S, Acc) -> {list_to_atom(lists:reverse(Acc)), S}.

quoted([$\\, C | S], Acc) -> quoted(S, [unescape(C) | Acc]);
quoted([$' | S], Acc) -> {list_to_atom(lists:reverse(Acc)), S};
quoted([C | S], Acc) -> quoted(S, [C | Acc]).

unescape($n) -> $\n;
unescape($t) -> $\t;
unescape($r) -> $\r;
unescape(C) -> C.

list(S, Acc) ->
    {T, S1} = term(S),
    case S1 of
        [$, | S2] -> list(S2, [T | Acc]);
        [$] | S2] -> {lists:reverse([T | Acc]), S2};
        [$| | S2] -> {Tail, [$] | S3]} = term(S2), {lists:reverse([T | Acc], Tail), S3}
    end.

tuple(S, Acc) ->
    {T, S1} = term(S),
    case S1 of
        [$, | S2] -> tuple(S2, [T | Acc]);
        [$} | S2] -> {list_to_tuple(lists:reverse([T | Acc])), S2}
    end.

map(S, M) ->
    {K, " => " ++ S1} = term(S),
    {V, S2} = term(S1),
    case S2 of
        [$, | S3] -> map(S3, maps:put(K, V, M));
        [$} | S3] -> {maps:put(K, V, M), S3}
    end.

bin(S, Acc) ->
    {N, S1} = number(S),
    case S1 of
        [$, | S2] -> bin(S2, [N | Acc]);
        [$>, $> | S2] -> {list_to_binary(lists:reverse([N | Acc])), S2}
    end.

%% A process ended with an error: say so on standard error, as erl does.
report(exit, _Reason) -> ok;
report(_Class, Reason) ->
    '$rt':put_chars_err(io_lib:format("=ERROR REPORT====~nError in process ~w with exit value:~n~p~n~n",
                                      [self(), Reason])).

%% An exception left main.
main_error(Class, Reason) ->
    '$rt':put_chars_err(io_lib:format("mlws-vm-erl: exception ~w: ~p~n", [Class, Reason])).
