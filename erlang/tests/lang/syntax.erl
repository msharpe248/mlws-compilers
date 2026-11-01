-module(syntax).
-export([main/0]).

main() ->
    io:format("~p~n", [16#FF + 8#17 + 2#1010]),
    io:format("~p~n", [1_000_000]),
    io:format("~p~n", [[$\n, $\t, $\s, $\\, $', $"]]),
    io:format("~p~n", ["tab\there"]),
    io:format("~p~n", ['hello world']),
    io:format("~p~n", [begin X = 1, X + 1 end]),
    io:format("~p~n", [-(3)]),
    F = fun(X) when X > 0 -> pos; (0) -> zero; (_) -> neg end,
    io:format("~p~n", [[F(1), F(0), F(-1)]]),
    Y = case catch list_to_integer("x") of
            {'EXIT', _} -> bad;
            N -> N
        end,
    io:format("~p~n", [Y]),
    io:format("~p~n", [[X || X <- lists:seq(1, 20), X rem 3 =:= 0, X rem 2 =:= 0]]),
    io:format("~p~n", [[{X, Y2} || X <- [1, 2, 3], Y2 <- [a, b], X =/= 2]]),
    io:format("~p~n", [[X || {ok, X} <- [{ok, 1}, error, {ok, 2}]]]),
    G = fun G(0, Acc) -> Acc; G(N, Acc) -> G(N - 1, Acc * N) end,
    io:format("~p~n", [G(25, 1)]),
    Z = if 1 > 2 -> a; 1 < 2, 2 < 3 -> b; true -> c end,
    io:format("~p~n", [Z]),
    io:format("~p~n", [is_atom(true) andalso not is_atom(1)]),
    io:format("~p~n", [(fun() -> nested end)()]),
    {A, B} = {B0, A0} = {1, 2},
    io:format("~p~n", [{A, B, A0, B0}]),
    io:format("~p~n", [tuple_size({}) + length([])]),
    io:format("~p~n", [atom_to_list('quoted\nnewline')]),
    io:format("~p~n", [[1 | [2 | [3]]]]),
    io:format("~p~n", [lists:map(fun({K, V}) -> K + V end, [{1, 2}, {3, 4}])]),
    io:format("~p~n", [element(1, {erlang:max(4, 9), x})]),
    ok.
