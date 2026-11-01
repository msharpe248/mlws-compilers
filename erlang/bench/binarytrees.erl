%% The binary-trees benchmark (depth 16).
-module(binarytrees).
-export([main/0]).

make(0) -> {nil, nil};
make(D) -> {make(D - 1), make(D - 1)}.

check({nil, nil}) -> 1;
check({L, R}) -> 1 + check(L) + check(R).

main() ->
    Max = 16,
    Stretch = Max + 1,
    io:format("stretch tree of depth ~w\t check: ~w~n", [Stretch, check(make(Stretch))]),
    Long = make(Max),
    depths(4, Max),
    io:format("long lived tree of depth ~w\t check: ~w~n", [Max, check(Long)]).

depths(D, Max) when D > Max -> ok;
depths(D, Max) ->
    N = 1 bsl (Max - D + 4),
    C = lists:foldl(fun(_, Acc) -> Acc + check(make(D)) end, 0, lists:seq(1, N)),
    io:format("~w\t trees of depth ~w\t check: ~w~n", [N, D, C]),
    depths(D + 2, Max).
