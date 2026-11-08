%% Runtime support for mlws-vm-erlc, in Erlang: exit reasons sent between
%% processes as text (ser/deser), and crash reports.
-module(erts).
-export([ser/1, deser/1, report/2, main_error/2, fname/3, connect/1, cookie/0]).

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

%% The MLWSVM name of M:F/A (as the compiler's fn_name makes it), for
%% apply/3 of a function of a program compiled with --modules.
fname(M, F, A) ->
    list_to_atom("E$" ++ mangle(atom_to_list(M)) ++ "$" ++ mangle(atom_to_list(F)) ++ "$" ++ integer_to_list(A)).

mangle([]) -> [];
mangle([C | T]) when C >= $a, C =< $z -> [C | mangle(T)];
mangle([C | T]) when C >= $A, C =< $Z -> [C | mangle(T)];
mangle([C | T]) when C >= $0, C =< $9 -> [C | mangle(T)];
mangle([$_ | T]) -> [$_ | mangle(T)];
mangle([C | T]) -> [$_, hex(C bsr 4), hex(C band 15), $_ | mangle(T)].

hex(D) when D < 10 -> $0 + D;
hex(D) -> $A + D - 10.

%% Connect to Node (name@host) through net_kernel, which holds the cookie:
%% true if it is this node or now connected.
connect(Node) ->
    case node() of
        nonode@nohost -> false;
        Node -> true;
        _ ->
            case whereis(net_kernel) of
                undefined -> false;
                K ->
                    K ! {connect, Node, self()},
                    receive {net_kernel, connected, Node, R} -> R end
            end
    end.

%% The cookie: net_kernel's, set_cookie's, or ~/.erlang.cookie's (nocookie
%% without one).
cookie() ->
    case whereis(net_kernel) of
        undefined ->
            case get('$cookie') of
                undefined -> file_cookie();
                C -> C
            end;
        K ->
            K ! {get_cookie, self()},
            receive {net_kernel, cookie, C} -> C end
    end.

file_cookie() ->
    case file:read_file('$rt':getenv("HOME") ++ "/.erlang.cookie") of
        {ok, B} -> list_to_atom(string:trim(binary_to_list(B)));
        _ -> nocookie
    end.
