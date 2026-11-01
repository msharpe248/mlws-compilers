%% Maps of more than 32 keys, for the runtime of mlws-vm-erlc: a persistent
%% red-black tree (Okasaki) in the order of the keys. A node is
%% {Color, Left, Key, Value, Right}; e is the empty tree.
-module(erts_map).
-export([insert/3, from_kv/1]).

%% {Tree, 1} if K was not there, {Tree, 0} if its value was replaced.
insert(K, V, T) ->
    {{_, L, K1, V1, R}, Grew} = ins(K, V, T),
    {{b, L, K1, V1, R}, Grew}.

ins(K, V, e) -> {{r, e, K, V, e}, 1};
ins(K, V, {C, L, K1, V1, R}) ->
    case '$rt':mapcmp(K, K1) of
        -1 -> {L1, G} = ins(K, V, L), {balance(C, L1, K1, V1, R), G};
        1 -> {R1, G} = ins(K, V, R), {balance(C, L, K1, V1, R1), G};
        0 -> {{C, L, K, V, R}, 0}
    end.

balance(b, {r, {r, A, XK, XV, B}, YK, YV, C}, ZK, ZV, D) -> {r, {b, A, XK, XV, B}, YK, YV, {b, C, ZK, ZV, D}};
balance(b, {r, A, XK, XV, {r, B, YK, YV, C}}, ZK, ZV, D) -> {r, {b, A, XK, XV, B}, YK, YV, {b, C, ZK, ZV, D}};
balance(b, A, XK, XV, {r, {r, B, YK, YV, C}, ZK, ZV, D}) -> {r, {b, A, XK, XV, B}, YK, YV, {b, C, ZK, ZV, D}};
balance(b, A, XK, XV, {r, B, YK, YV, {r, C, ZK, ZV, D}}) -> {r, {b, A, XK, XV, B}, YK, YV, {b, C, ZK, ZV, D}};
balance(C, L, K, V, R) -> {C, L, K, V, R}.

%% A tree of the keys and values in a list [K1, V1, K2, V2, ...] in key order.
from_kv(L) -> from_kv(L, e).
from_kv([K, V | T], Tree) -> {Tree1, _} = insert(K, V, Tree), from_kv(T, Tree1);
from_kv([], Tree) -> Tree.
