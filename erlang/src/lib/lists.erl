%% lists for mlws-vm-erlc.
-module(lists).
-export([reverse/1, reverse/2, append/1, append/2, map/2, foldl/3, foldr/3, filter/2,
         foreach/2, seq/2, seq/3, nth/2, nthtail/2, last/1, sum/1, max/1, min/1, member/2,
         keyfind/3, keymember/3, keysearch/3, keystore/4, keydelete/3, keyreplace/4,
         keytake/3, keysort/2, keymerge/3, sort/1, sort/2, usort/1, usort/2, merge/2, merge/3,
         zip/2, zip3/3, unzip/1, unzip3/1, zipwith/3, flatten/1, flatten/2, flatlength/1,
         flatmap/2, any/2, all/2, partition/2, split/2, splitwith/2, sublist/2, sublist/3,
         duplicate/2, delete/2, subtract/2, takewhile/2, dropwhile/2, mapfoldl/3, mapfoldr/3,
         enumerate/1, enumerate/2, filtermap/2, concat/1, join/2, uniq/1, uniq/2, prefix/2,
         suffix/2, droplast/1, search/2, foldl/5, nthtail/3, max/2, min/2, sum/2,
         append/3, reverse/3, mapfoldl/2, sort/3, flatten/3, keyfind/2, all/1, any/1,
         split/3]).

reverse(L) -> '$rt':reverse(L, []).
reverse(L, T) -> '$rt':reverse(L, T).
reverse(L, T, _) -> reverse(L, T).

append(Ls) -> append_all(Ls).
append_all([L | Ls]) -> L ++ append_all(Ls);
append_all([]) -> [].
append(A, B) -> A ++ B.
append(A, B, C) -> A ++ B ++ C.

map(F, [H | T]) -> [F(H) | map(F, T)];
map(F, []) when is_function(F, 1) -> [].

foldl(F, Acc, [H | T]) -> foldl(F, F(H, Acc), T);
foldl(F, Acc, []) when is_function(F, 2) -> Acc.

foldl(F, Acc, L, _, _) -> foldl(F, Acc, L).

foldr(F, Acc, [H | T]) -> F(H, foldr(F, Acc, T));
foldr(F, Acc, []) when is_function(F, 2) -> Acc.

filter(P, L) -> [E || E <- L, P(E)].

foreach(F, [H | T]) -> F(H), foreach(F, T);
foreach(F, []) when is_function(F, 1) -> ok.

seq(First, Last) when is_integer(First), is_integer(Last), First - 1 =< Last ->
    seq_loop(Last, First, []).

seq_loop(N, First, Acc) when N >= First -> seq_loop(N - 1, First, [N | Acc]);
seq_loop(_, _, Acc) -> Acc.

seq(First, Last, Inc) when is_integer(First), is_integer(Last), is_integer(Inc), Inc > 0, First - Inc =< Last ->
    N = (Last - First) div Inc,
    seq_down(First + N * Inc, Inc, First, []);
seq(First, Last, Inc) when is_integer(First), is_integer(Last), is_integer(Inc), Inc < 0, First - Inc >= Last ->
    N = (First - Last) div (-Inc),
    seq_up(First + N * Inc, Inc, First, []);
seq(First, First, 0) when is_integer(First) -> [First].

seq_down(X, Inc, First, Acc) when X >= First -> seq_down(X - Inc, Inc, First, [X | Acc]);
seq_down(_, _, _, Acc) -> Acc.

seq_up(X, Inc, First, Acc) when X =< First -> seq_up(X - Inc, Inc, First, [X | Acc]);
seq_up(_, _, _, Acc) -> Acc.

nth(1, [H | _]) -> H;
nth(N, [_ | T]) when N > 1 -> nth(N - 1, T).

nthtail(0, L) -> L;
nthtail(N, [_ | T]) when N > 0 -> nthtail(N - 1, T).

nthtail(N, L, _) -> nthtail(N, L).

last([E]) -> E;
last([_ | T]) -> last(T).

droplast([_]) -> [];
droplast([H | T]) -> [H | droplast(T)].

sum(L) -> sum(L, 0).
sum([H | T], Acc) -> sum(T, Acc + H);
sum([], Acc) -> Acc.

max([H | T]) -> max(T, H).
max([H | T], M) when H > M -> max(T, H);
max([_ | T], M) -> max(T, M);
max([], M) -> M.

min([H | T]) -> min(T, H).
min([H | T], M) when H < M -> min(T, H);
min([_ | T], M) -> min(T, M);
min([], M) -> M.

member(X, [Y | _]) when X =:= Y -> true;
member(X, [_ | T]) -> member(X, T);
member(_, []) -> false.

keyfind(K, N, [T | Ts]) when tuple_size(T) >= N, element(N, T) == K -> T;
keyfind(K, N, [_ | Ts]) -> keyfind(K, N, Ts);
keyfind(_, _, []) -> false.

keyfind(K, L) -> keyfind(K, 1, L).

keymember(K, N, L) -> keyfind(K, N, L) =/= false.

keysearch(K, N, L) ->
    case keyfind(K, N, L) of
        false -> false;
        T -> {value, T}
    end.

keystore(K, N, [T | Ts], New) when tuple_size(T) >= N, element(N, T) == K -> [New | Ts];
keystore(K, N, [T | Ts], New) -> [T | keystore(K, N, Ts, New)];
keystore(_, _, [], New) -> [New].

keydelete(K, N, [T | Ts]) when tuple_size(T) >= N, element(N, T) == K -> Ts;
keydelete(K, N, [T | Ts]) -> [T | keydelete(K, N, Ts)];
keydelete(_, _, []) -> [].

keyreplace(K, N, [T | Ts], New) when tuple_size(T) >= N, element(N, T) == K -> [New | Ts];
keyreplace(K, N, [T | Ts], New) -> [T | keyreplace(K, N, Ts, New)];
keyreplace(_, _, [], _) -> [].

keytake(K, N, L) -> keytake(K, N, L, []).
keytake(K, N, [T | Ts], Acc) when tuple_size(T) >= N, element(N, T) == K ->
    {value, T, reverse(Acc, Ts)};
keytake(K, N, [T | Ts], Acc) -> keytake(K, N, Ts, [T | Acc]);
keytake(_, _, [], _) -> false.

keysort(N, L) -> sort(fun(A, B) -> element(N, A) =< element(N, B) end, L).

keymerge(N, A, B) -> merge(fun(X, Y) -> element(N, X) =< element(N, Y) end, A, B).

%% Sorting: stable bottom-up merge sort, of pairs, then pairs of pairs, ...
sort(L) -> mergeall(runs(L)).

runs([X, Y | T]) when X =< Y -> [[X, Y] | runs(T)];
runs([X, Y | T]) -> [[Y, X] | runs(T)];
runs([X]) -> [[X]];
runs([]) -> [].

mergeall([]) -> [];
mergeall([L]) -> L;
mergeall(Ls) -> mergeall(mergepairs(Ls)).

mergepairs([A, B | T]) -> [merge2(A, B) | mergepairs(T)];
mergepairs(Ls) -> Ls.

merge2([X | Xs] = A, [Y | Ys] = B) ->
    if
        X =< Y -> [X | merge2(Xs, B)];
        true -> [Y | merge2(A, Ys)]
    end;
merge2([], B) -> B;
merge2(A, []) -> A.

%% Le(A, B) is true when A may come before B.
sort(Le, L) -> mergeall(Le, runs(Le, L)).

runs(Le, [X, Y | T]) ->
    case Le(X, Y) of
        true -> [[X, Y] | runs(Le, T)];
        false -> [[Y, X] | runs(Le, T)]
    end;
runs(_, [X]) -> [[X]];
runs(_, []) -> [].

mergeall(_, []) -> [];
mergeall(_, [L]) -> L;
mergeall(Le, Ls) -> mergeall(Le, mergepairs(Le, Ls)).

mergepairs(Le, [A, B | T]) -> [merge(Le, A, B) | mergepairs(Le, T)];
mergepairs(_, Ls) -> Ls.

sort(Le, L, _) -> sort(Le, L).

merge(A, B) -> merge2(A, B).

merge(Le, [X | Xs] = A, [Y | Ys] = B) ->
    case Le(X, Y) of
        true -> [X | merge(Le, Xs, B)];
        false -> [Y | merge(Le, A, Ys)]
    end;
merge(_, [], B) -> B;
merge(_, A, []) -> A.

usort(L) -> uniq_sorted(sort(L)).
usort(Le, L) -> uniq_by(Le, sort(Le, L)).

uniq_sorted([X, Y | T]) when X == Y -> uniq_sorted([X | T]);
uniq_sorted([X | T]) -> [X | uniq_sorted(T)];
uniq_sorted([]) -> [].

uniq_by(Le, [X, Y | T]) ->
    case Le(X, Y) andalso Le(Y, X) of
        true -> uniq_by(Le, [X | T]);
        false -> [X | uniq_by(Le, [Y | T])]
    end;
uniq_by(_, L) -> L.

zip([X | Xs], [Y | Ys]) -> [{X, Y} | zip(Xs, Ys)];
zip([], []) -> [].

zip3([X | Xs], [Y | Ys], [Z | Zs]) -> [{X, Y, Z} | zip3(Xs, Ys, Zs)];
zip3([], [], []) -> [].

unzip(L) -> {[X || {X, _} <- L], [Y || {_, Y} <- L]}.
unzip3(L) -> {[X || {X, _, _} <- L], [Y || {_, Y, _} <- L], [Z || {_, _, Z} <- L]}.

zipwith(F, [X | Xs], [Y | Ys]) -> [F(X, Y) | zipwith(F, Xs, Ys)];
zipwith(_, [], []) -> [].

flatten(L) -> flatten(L, []).
flatten(L, Tail) -> reverse(flat(L, []), Tail).
flatten(L, Tail, _) -> flatten(L, Tail).

flat([H | T], Acc) when is_list(H) -> flat(T, flat(H, Acc));
flat([H | T], Acc) -> flat(T, [H | Acc]);
flat([], Acc) -> Acc.

flatlength(L) -> flatlength(L, 0).
flatlength([H | T], N) when is_list(H) -> flatlength(T, flatlength(H, N));
flatlength([_ | T], N) -> flatlength(T, N + 1);
flatlength([], N) -> N.

flatmap(F, [H | T]) -> F(H) ++ flatmap(F, T);
flatmap(F, []) when is_function(F, 1) -> [].

any(P, [H | T]) -> case P(H) of true -> true; false -> any(P, T) end;
any(P, []) when is_function(P, 1) -> false.

all(P, [H | T]) -> case P(H) of true -> all(P, T); false -> false end;
all(P, []) when is_function(P, 1) -> true.

any(L) -> member(true, L).
all(L) -> not member(false, L).

partition(P, L) -> partition(P, L, [], []).
partition(P, [H | T], As, Bs) ->
    case P(H) of
        true -> partition(P, T, [H | As], Bs);
        false -> partition(P, T, As, [H | Bs])
    end;
partition(_, [], As, Bs) -> {reverse(As), reverse(Bs)}.

split(N, L) when is_integer(N), N >= 0 -> split(N, L, []).
split(0, L, Acc) -> {reverse(Acc), L};
split(N, [H | T], Acc) -> split(N - 1, T, [H | Acc]).

splitwith(P, L) -> splitwith(P, L, []).
splitwith(P, [H | T] = L, Acc) ->
    case P(H) of
        true -> splitwith(P, T, [H | Acc]);
        false -> {reverse(Acc), L}
    end;
splitwith(_, [], Acc) -> {reverse(Acc), []}.

sublist(L, N) when is_integer(N), N >= 0 -> take(L, N).
sublist(L, S, N) when is_integer(S), S >= 1 -> take(nthtail_safe(S - 1, L), N).

nthtail_safe(0, L) -> L;
nthtail_safe(N, [_ | T]) -> nthtail_safe(N - 1, T);
nthtail_safe(_, []) -> [].

take(_, 0) -> [];
take([H | T], N) -> [H | take(T, N - 1)];
take([], _) -> [].

duplicate(N, X) when is_integer(N), N >= 0 -> dup(N, X, []).
dup(0, _, Acc) -> Acc;
dup(N, X, Acc) -> dup(N - 1, X, [X | Acc]).

delete(X, [Y | T]) when X =:= Y -> T;
delete(X, [Y | T]) -> [Y | delete(X, T)];
delete(_, []) -> [].

subtract(A, B) -> A -- B.

takewhile(P, [H | T]) ->
    case P(H) of
        true -> [H | takewhile(P, T)];
        false -> []
    end;
takewhile(_, []) -> [].

dropwhile(P, [H | T] = L) ->
    case P(H) of
        true -> dropwhile(P, T);
        false -> L
    end;
dropwhile(_, []) -> [].

mapfoldl(F, Acc, [H | T]) ->
    {R, Acc1} = F(H, Acc),
    {Rs, Acc2} = mapfoldl(F, Acc1, T),
    {[R | Rs], Acc2};
mapfoldl(_, Acc, []) -> {[], Acc}.

mapfoldl(F, L) -> mapfoldl(F, [], L).

mapfoldr(F, Acc, [H | T]) ->
    {Rs, Acc1} = mapfoldr(F, Acc, T),
    {R, Acc2} = F(H, Acc1),
    {[R | Rs], Acc2};
mapfoldr(_, Acc, []) -> {[], Acc}.

enumerate(L) -> enumerate(1, L).
enumerate(N, [H | T]) -> [{N, H} | enumerate(N + 1, T)];
enumerate(_, []) -> [].

filtermap(F, [H | T]) ->
    case F(H) of
        true -> [H | filtermap(F, T)];
        {true, V} -> [V | filtermap(F, T)];
        false -> filtermap(F, T)
    end;
filtermap(_, []) -> [].

concat(L) -> flatten([thing_to_list(X) || X <- L]).

thing_to_list(X) when is_integer(X) -> integer_to_list(X);
thing_to_list(X) when is_float(X) -> float_to_list(X);
thing_to_list(X) when is_atom(X) -> atom_to_list(X);
thing_to_list(X) when is_list(X) -> X.

join(_, []) -> [];
join(_, [X]) -> [X];
join(Sep, [X | T]) -> [X, Sep | join(Sep, T)].

uniq(L) -> uniq(L, #{}).
uniq([H | T], Seen) ->
    case maps:is_key(H, Seen) of
        true -> uniq(T, Seen);
        false -> [H | uniq(T, maps:put(H, true, Seen))]
    end;
uniq([], _) -> [];
uniq(F, L) when is_function(F, 1) -> uniq_f(F, L, #{}).

uniq_f(F, [H | T], Seen) ->
    K = F(H),
    case maps:is_key(K, Seen) of
        true -> uniq_f(F, T, Seen);
        false -> [H | uniq_f(F, T, maps:put(K, true, Seen))]
    end;
uniq_f(_, [], _) -> [].

prefix([X | Xs], [Y | Ys]) when X =:= Y -> prefix(Xs, Ys);
prefix([], _) -> true;
prefix(_, _) -> false.

suffix(S, L) ->
    D = length(L) - length(S),
    D >= 0 andalso nthtail(D, L) =:= S.

search(P, [H | T]) ->
    case P(H) of
        true -> {value, H};
        false -> search(P, T)
    end;
search(_, []) -> false.
