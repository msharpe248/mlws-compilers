-module(funs).
-export([main/0, double/1]).

double(X) -> 2 * X.

make_adder(N) -> fun(X) -> X + N end.

compose(F, G) -> fun(X) -> F(G(X)) end.

main() ->
    Add3 = make_adder(3),
    io:format("~p~n", [Add3(4)]),
    io:format("~p~n", [lists:map(fun(X) -> X * X end, [1, 2, 3])]),
    io:format("~p~n", [lists:map(fun double/1, [1, 2, 3])]),
    io:format("~p~n", [lists:map(fun funs:double/1, [4, 5])]),
    io:format("~p~n", [lists:foldl(fun(X, Acc) -> X + Acc end, 0, [1, 2, 3, 4])]),
    io:format("~p~n", [lists:filter(fun(X) -> X rem 2 =:= 0 end, lists:seq(1, 10))]),
    F = compose(fun(X) -> X + 1 end, fun(X) -> X * 10 end),
    io:format("~p~n", [F(5)]),
    Fact = fun Fact(0) -> 1; Fact(N) -> N * Fact(N - 1) end,
    io:format("~p~n", [Fact(10)]),
    Y = 100,
    G = fun(X) -> case X of Y -> same; _ -> different end end,
    io:format("~p ~p~n", [G(100), G(1)]),
    H = fun(A, B) -> {A, B} end,
    io:format("~p~n", [H(1, 2)]),
    io:format("~p~n", [apply(H, [x, y])]),
    io:format("~p~n", [apply(funs, double, [21])]),
    io:format("~p~n", [erlang:apply(lists, reverse, [[1, 2, 3]])]),
    Counter = fun(Self, N) when N > 0 -> Self(Self, N - 1) + 1; (_, 0) -> 0 end,
    io:format("~p~n", [Counter(Counter, 5)]),
    io:format("~p ~p ~p~n", [is_function(Add3), is_function(Add3, 1), is_function(Add3, 2)]),
    io:format("~p~n", [lists:sort(fun(A, B) -> A > B end, [3, 1, 2])]),
    io:format("~p~n", [[X * Y1 || X <- [1, 2], Y1 <- [10, 20]]]),
    io:format("~p~n", [[X || X <- [1, a, 2, b], is_integer(X)]]),
    io:format("~p~n", [[{X, Y1} || {X, Y1} <- [{1, a}, {2, b}, bad], X > 1]]),
    ok.
