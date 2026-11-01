%% proplists for mlws-vm-erlc.
-module(proplists).
-export([get_value/2, get_value/3, lookup/2, lookup_all/2, get_all_values/2, is_defined/2,
         delete/2, get_bool/2, get_keys/1, property/1, property/2, unfold/1, compact/1]).

get_value(K, L) -> get_value(K, L, undefined).
get_value(K, [P | Ps], D) ->
    if
        is_atom(P), P =:= K -> true;
        tuple_size(P) >= 1, element(1, P) =:= K ->
            case P of
                {_, V} -> V;
                _ -> D
            end;
        true -> get_value(K, Ps, D)
    end;
get_value(_, [], D) -> D.

lookup(K, [P | Ps]) ->
    if
        is_atom(P), P =:= K -> {K, true};
        tuple_size(P) >= 1, element(1, P) =:= K -> P;
        true -> lookup(K, Ps)
    end;
lookup(_, []) -> none.

lookup_all(K, L) -> [norm(P) || P <- L, key(P) =:= K].

get_all_values(K, L) -> [V || {K1, V} <- [norm(P) || P <- L], K1 =:= K].

key(P) when is_atom(P) -> P;
key(P) when is_tuple(P), tuple_size(P) >= 1 -> element(1, P);
key(_) -> '$none'.

norm(P) when is_atom(P) -> {P, true};
norm(P) -> P.

is_defined(K, L) -> lists:any(fun(P) -> key(P) =:= K end, L).

delete(K, L) -> [P || P <- L, key(P) =/= K].

get_bool(K, L) ->
    case lookup(K, L) of
        {K, true} -> true;
        _ -> false
    end.

get_keys(L) -> lists:usort([key(P) || P <- L, key(P) =/= '$none']).

property({K, true}) when is_atom(K) -> K;
property(P) -> P.
property(K, true) when is_atom(K) -> K;
property(K, V) -> {K, V}.

unfold(L) -> [norm(P) || P <- L].
compact(L) -> [property(P) || P <- L].
