%% The BIFs of mlws-vm-erlc that are written in Erlang (the others are in
%% runtime.masm, or done inline by the compiler).
-module(erlang).
-export([min/2, max/2, put/2, get/1, get/0, erase/1, erase/0, get_keys/0, get_keys/1,
         integer_to_list/2, list_to_integer/2, integer_to_binary/2, binary_to_integer/2,
         atom_to_binary/2, binary_to_atom/2, binary_to_existing_atom/2,
         list_to_existing_atom/1, halt/2, unique_integer/1, '--'/2, '++'/2,
         is_alive/0, display/1, error/3, raise/3, nif_error/1, send/2, send/3,
         float_to_list/2, float_to_binary/2, append_element/2, delete_element/2,
         insert_element/3, list_to_bitstring/1, bitstring_to_list/1, binary_to_list/3,
         monotonic_time/1, system_time/1, convert_time_unit/3, timestamp/0, now/0,
         apply/2, apply/3, spawn/2, spawn/4, spawn_link/2, spawn_link/4, spawn_monitor/3,
         monitor/3, demonitor/2, process_info/2, statistics/1, system_info/1, 
         hd/1, tl/1, self/0,
         is_map_key/2, map_get/2, map_size/1, make_tuple/3, phash2/1, phash2/2,
         list_to_pid/1, pid_to_list/1, ref_to_list/1, fun_to_list/1, fun_info/2,
         is_builtin/3, function_exported/3, exit/1, throw/1, error/1,
         error/2, binary_part/2, split_binary/2, max/1,
         is_atom/1, is_binary/1, is_bitstring/1, is_boolean/1, is_float/1, is_function/1,
         is_function/2, is_integer/1, is_list/1, is_map/1, is_number/1, is_pid/1,
         is_reference/1, is_tuple/1, is_record/2, is_record/3]).

min(A, B) when B < A -> B;
min(A, _) -> A.

max(A, B) when B > A -> B;
max(A, _) -> A.

max(L) -> lists:max(L).

%% The process dictionary, a map.
put(K, V) ->
    D = '$rt':pdict(),
    Old = case maps:find(K, D) of {ok, O} -> O; error -> undefined end,
    '$rt':set_pdict(maps:put(K, V, D)),
    Old.

get(K) ->
    case maps:find(K, '$rt':pdict()) of
        {ok, V} -> V;
        error -> undefined
    end.

get() -> maps:to_list('$rt':pdict()).

erase(K) ->
    D = '$rt':pdict(),
    case maps:find(K, D) of
        {ok, V} -> '$rt':set_pdict(maps:remove(K, D)), V;
        error -> undefined
    end.

erase() ->
    L = get(),
    '$rt':set_pdict(maps:new()),
    L.

get_keys() -> maps:keys('$rt':pdict()).

get_keys(V) -> [K || {K, V1} <- get(), V1 =:= V].

integer_to_list(I, 10) -> integer_to_list(I);
integer_to_list(I, Base) when is_integer(I), is_integer(Base), Base >= 2, Base =< 36 ->
    case I < 0 of
        true -> [$- | digits(-I, Base, [])];
        false -> digits(I, Base, [])
    end;
integer_to_list(_, _) -> erlang:error(badarg).

digits(I, Base, Acc) when I < Base -> [digit(I) | Acc];
digits(I, Base, Acc) -> digits(I div Base, Base, [digit(I rem Base) | Acc]).

digit(D) when D < 10 -> $0 + D;
digit(D) -> $A + D - 10.

list_to_integer(L, 10) -> list_to_integer(L);
list_to_integer(L, Base) when is_list(L), is_integer(Base), Base >= 2, Base =< 36 ->
    case L of
        [$- | Ds] when Ds =/= [] -> -undigits(Ds, Base, 0);
        [$+ | Ds] when Ds =/= [] -> undigits(Ds, Base, 0);
        [_ | _] -> undigits(L, Base, 0);
        _ -> erlang:error(badarg)
    end;
list_to_integer(_, _) -> erlang:error(badarg).

undigits([], _, N) -> N;
undigits([C | Cs], Base, N) ->
    D = if
            C >= $0, C =< $9 -> C - $0;
            C >= $a, C =< $z -> C - $a + 10;
            C >= $A, C =< $Z -> C - $A + 10;
            true -> 99
        end,
    if
        D < Base -> undigits(Cs, Base, N * Base + D);
        true -> erlang:error(badarg)
    end.

integer_to_binary(I, Base) -> list_to_binary(integer_to_list(I, Base)).
binary_to_integer(B, Base) -> list_to_integer(binary_to_list(B), Base).

atom_to_binary(A, _Enc) -> atom_to_binary(A).
binary_to_atom(B, _Enc) -> binary_to_atom(B).
binary_to_existing_atom(B, _Enc) -> binary_to_atom(B).
list_to_existing_atom(L) -> list_to_atom(L).

halt(Status, _Opts) -> halt(Status).

unique_integer(_Opts) -> unique_integer().

'--'(A, B) when is_list(A), is_list(B) -> subtract(A, B);
'--'(_, _) -> erlang:error(badarg).

subtract(A, []) -> A;
subtract(A, [X | Xs]) -> subtract(delete1(X, A), Xs).

delete1(X, [Y | Ys]) when X =:= Y -> Ys;
delete1(X, [Y | Ys]) -> [Y | delete1(X, Ys)];
delete1(_, []) -> [].

'++'(A, B) -> A ++ B.

is_alive() -> false.

display(T) -> io:format("~w~n", [T]), true.

error(R, _Args, _Opts) -> erlang:error(R).

raise(error, R, _) -> erlang:error(R);
raise(exit, R, _) -> erlang:exit(R);
raise(throw, R, _) -> erlang:throw(R);
raise(_, _, _) -> erlang:error(badarg).

nif_error(R) -> erlang:error(R).

send(P, M) -> P ! M.
send(P, M, _Opts) -> P ! M, ok.

float_to_list(F, Opts) -> float_opts(F, Opts).
float_to_binary(F, Opts) -> list_to_binary(float_opts(F, Opts)).
float_to_binary(F) -> list_to_binary(float_to_list(F)).

float_opts(F, Opts) ->
    case lists:keyfind(decimals, 1, Opts) of
        {decimals, D} ->
            S = io_lib:format("~." ++ integer_to_list(D) ++ "f", [F]),
            case lists:member(compact, Opts) of
                true -> compact(lists:flatten(S));
                false -> lists:flatten(S)
            end;
        false ->
            case lists:keyfind(scientific, 1, Opts) of
                {scientific, D} -> lists:flatten(io_lib:format("~." ++ integer_to_list(D + 1) ++ "e", [F]));
                false ->
                    case lists:member(short, Opts) of
                        true -> io_lib:float_short(F);
                        false -> float_to_list(F)
                    end
            end
    end.

compact(S) ->
    case lists:member($., S) of
        true ->
            R = lists:dropwhile(fun(C) -> C =:= $0 end, lists:reverse(S)),
            case R of
                [$. | _] -> lists:reverse([$0 | R]);
                _ -> lists:reverse(R)
            end;
        false -> S
    end.

append_element(T, E) -> list_to_tuple(tuple_to_list(T) ++ [E]).

delete_element(N, T) ->
    {A, [_ | B]} = lists:split(N - 1, tuple_to_list(T)),
    list_to_tuple(A ++ B).

insert_element(N, T, E) ->
    {A, B} = lists:split(N - 1, tuple_to_list(T)),
    list_to_tuple(A ++ [E | B]).

list_to_bitstring(L) -> list_to_binary(L).
bitstring_to_list(B) -> binary_to_list(B).

binary_to_list(B, S, E) -> lists:sublist(binary_to_list(B), S, E - S + 1).

monotonic_time(Unit) -> convert_time_unit(monotonic_time(), nanosecond, Unit).
system_time(Unit) -> convert_time_unit(system_time(), nanosecond, Unit).

convert_time_unit(T, From, To) -> T * per_second(To) div per_second(From).

per_second(second) -> 1;
per_second(seconds) -> 1;
per_second(millisecond) -> 1000;
per_second(milli_seconds) -> 1000;
per_second(microsecond) -> 1000000;
per_second(micro_seconds) -> 1000000;
per_second(nanosecond) -> 1000000000;
per_second(nano_seconds) -> 1000000000;
per_second(native) -> 1000000000;
per_second(perf_counter) -> 1000000000;
per_second(N) when is_integer(N) -> N.

timestamp() ->
    Us = system_time() div 1000,
    {Us div 1000000000000, Us div 1000000 rem 1000000, Us rem 1000000}.

now() -> timestamp().

apply(F, Args) -> erlang:apply(F, Args).
apply(M, F, Args) -> erlang:apply(M, F, Args).

spawn(_Node, F) -> spawn(F).
spawn(_Node, M, F, A) -> spawn(M, F, A).
spawn_link(_Node, F) -> spawn_link(F).
spawn_link(_Node, M, F, A) -> spawn_link(M, F, A).
spawn_monitor(M, F, A) -> spawn_monitor(fun() -> apply(M, F, A) end).

monitor(Type, Item, _Opts) -> monitor(Type, Item).

demonitor(Ref, Opts) -> '$rt':demonitor2(Ref, Opts).

process_info(_Pid, message_queue_len) -> {message_queue_len, 0};
process_info(_Pid, Item) -> {Item, undefined}.

statistics(runtime) -> T = monotonic_time(millisecond), {T, 0};
statistics(wall_clock) -> T = monotonic_time(millisecond), {T, 0};
statistics(reductions) -> {0, 0};
statistics(_) -> 0.

system_info(process_count) -> length(processes());
system_info(otp_release) -> "29";
system_info(schedulers) -> 1;
system_info(schedulers_online) -> 1;
system_info(wordsize) -> 8;
system_info(_) -> undefined.

hd(L) -> erlang:hd(L).
tl(L) -> erlang:tl(L).
self() -> erlang:self().
exit(R) -> erlang:exit(R).
throw(R) -> erlang:throw(R).
error(R) -> erlang:error(R).
error(R, _) -> erlang:error(R).

is_map_key(K, M) -> maps:is_key(K, M).
map_get(K, M) -> maps:get(K, M).
map_size(M) -> maps:size(M).

make_tuple(N, Default, Inits) ->
    T = make_tuple(N, Default),
    lists:foldl(fun({I, V}, Acc) -> setelement(I, Acc, V) end, T, Inits).

phash2(T) -> phash2(T, 4294967296).
phash2(T, Range) -> hash(lists:flatten(io_lib:write(T)), 0) rem Range.

hash([C | Cs], H) -> hash(Cs, (H * 31 + C) band 16#FFFFFFF);
hash([], H) -> H.

list_to_pid(L) ->
    [_, N, _] = string:tokens(L, "<.>"),
    '$rt':make_pid(list_to_integer(N)).

pid_to_list(P) -> lists:flatten(io_lib:write(P)).
ref_to_list(R) -> lists:flatten(io_lib:write(R)).
fun_to_list(F) -> lists:flatten(io_lib:write(F)).

fun_info(F, arity) -> {arity, '$rt':fun_arity(F)};
fun_info(_F, Item) -> {Item, undefined}.

is_builtin(_, _, _) -> false.
function_exported(_, _, _) -> false.

binary_part(B, {S, L}) -> binary_part(B, S, L).
split_binary(B, N) -> {binary_part(B, 0, N), binary_part(B, N, byte_size(B) - N)}.

%% The type tests, for fun is_integer/1 and apply (the compiler does them inline).
is_atom(X) -> erlang:is_atom(X).
is_binary(X) -> erlang:is_binary(X).
is_bitstring(X) -> erlang:is_bitstring(X).
is_boolean(X) -> erlang:is_boolean(X).
is_float(X) -> erlang:is_float(X).
is_function(X) -> erlang:is_function(X).
is_function(X, N) -> erlang:is_function(X, N).
is_integer(X) -> erlang:is_integer(X).
is_list(X) -> erlang:is_list(X).
is_map(X) -> erlang:is_map(X).
is_number(X) -> erlang:is_number(X).
is_pid(X) -> erlang:is_pid(X).
is_reference(X) -> erlang:is_reference(X).
is_tuple(X) -> erlang:is_tuple(X).
is_record(X, Tag) -> is_tuple(X) andalso tuple_size(X) > 0 andalso element(1, X) =:= Tag.
is_record(X, Tag, Size) -> is_tuple(X) andalso tuple_size(X) =:= Size andalso element(1, X) =:= Tag.
