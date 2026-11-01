%% maps for mlws-vm-erlc. A map keeps its keys in term order.
-module(maps).
-export([new/0, get/2, get/3, put/3, find/2, remove/2, keys/1, values/1, to_list/1,
         from_list/1, size/1, is_key/2, merge/2, fold/3, map/2, filter/2, foreach/2,
         update/3, update_with/3, update_with/4, with/2, without/2, take/2, from_keys/2,
         filtermap/2, merge_with/3, groups_from_list/2, groups_from_list/3, intersect/2,
         iterator/1, next/1, foldl/3]).

new() -> #{}.
get(K, M) -> '$rt':map_get(K, M).
get(K, M, Default) ->
    case '$rt':map_find2(K, M) of
        {ok, V} -> V;
        error -> Default
    end.
put(K, V, M) -> M#{K => V}.
find(K, M) -> '$rt':map_find2(K, M).
remove(K, M) -> '$rt':map_remove(K, M).
keys(M) -> '$rt':map_keys(M).
values(M) -> '$rt':map_values(M).
to_list(M) -> '$rt':map_to_list(M).
from_list(L) -> '$rt':map_from_list(L).
size(M) -> '$rt':map_size(M).
is_key(K, M) -> '$rt':map_is_key(K, M).
merge(A, B) -> '$rt':map_merge(A, B).

fold(F, Acc, M) -> lists:foldl(fun({K, V}, A) -> F(K, V, A) end, Acc, to_list(M)).
foldl(F, Acc, M) -> fold(F, Acc, M).

map(F, M) -> from_list([{K, F(K, V)} || {K, V} <- to_list(M)]).

filter(P, M) -> from_list([{K, V} || {K, V} <- to_list(M), P(K, V)]).

filtermap(F, M) ->
    from_list(lists:filtermap(fun({K, V}) ->
                                      case F(K, V) of
                                          true -> true;
                                          {true, V1} -> {true, {K, V1}};
                                          false -> false
                                      end
                              end, to_list(M))).

foreach(F, M) -> lists:foreach(fun({K, V}) -> F(K, V) end, to_list(M)).

update(K, V, M) -> M#{K := V}.

update_with(K, F, M) -> M#{K := F(get(K, M))}.

update_with(K, F, Init, M) ->
    case find(K, M) of
        {ok, V} -> M#{K := F(V)};
        error -> M#{K => Init}
    end.

with(Ks, M) -> from_list([{K, V} || {K, V} <- to_list(M), lists:member(K, Ks)]).
without(Ks, M) -> lists:foldl(fun(K, A) -> remove(K, A) end, M, Ks).

take(K, M) ->
    case find(K, M) of
        {ok, V} -> {V, remove(K, M)};
        error -> error
    end.

from_keys(Ks, V) -> from_list([{K, V} || K <- Ks]).

merge_with(F, A, B) ->
    fold(fun(K, V2, Acc) ->
                 case find(K, Acc) of
                     {ok, V1} -> Acc#{K := F(K, V1, V2)};
                     error -> Acc#{K => V2}
                 end
         end, A, B).

intersect(A, B) -> from_list([{K, V} || {K, V} <- to_list(B), is_key(K, A)]).

groups_from_list(KeyF, L) -> groups_from_list(KeyF, fun(X) -> X end, L).
groups_from_list(KeyF, ValF, L) ->
    M = lists:foldl(fun(X, Acc) ->
                            K = KeyF(X),
                            update_with(K, fun(Vs) -> [ValF(X) | Vs] end, [ValF(X)], Acc)
                    end, #{}, L),
    map(fun(_, Vs) -> lists:reverse(Vs) end, M).

iterator(M) -> {iter, to_list(M)}.
next({iter, [{K, V} | T]}) -> {K, V, {iter, T}};
next({iter, []}) -> none.
