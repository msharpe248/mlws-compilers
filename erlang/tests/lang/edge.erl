-module(edge).
-export([main/0]).
-record(acc, {total = 0, items = []}).

add(#acc{total = T, items = Is} = A, X) when is_integer(X) -> A#acc{total = T + X, items = [X | Is]}.

nested(N) ->
    F = fun(X) -> G = fun(Y) -> [X + Y + Z || Z <- lists:seq(1, N)] end, G(X * 10) end,
    F(1).

deep(0) -> [];
deep(N) -> [N | deep(N - 1)].

main() ->
    io:format("~p~n", [length(lists:map(fun(X) -> X + 1 end, lists:seq(1, 1000000)))]),
    io:format("~p~n", [length(deep(500000))]),
    A = lists:foldl(fun(X, Acc) -> add(Acc, X) end, #acc{}, [1, 2, 3]),
    io:format("~p~n", [A]),
    io:format("~p~n", [nested(3)]),
    io:format("~p~n", [lists:foldl(fun(X, P) -> P * X end, 1, lists:seq(1, 40))]),
    io:format("~p~n", [(1 bsl 64) div 3]),
    io:format("~p~n", [-(1 bsl 70) rem 7]),
    io:format("~p~n", [(1 bsl 64) - (1 bsl 64) + 5]),
    io:format("~p~n", [1 bsl 64 > 1 bsl 63]),
    io:format("~p~n", [{1 bsl 64 =:= 1 bsl 64, 1.0e20 < 1 bsl 70}]),
    io:format("~p~n", [integer_to_list(1 bsl 80)]),
    io:format("~p~n", [list_to_integer("123456789012345678901234567890")]),
    R = try
            receive after 0 -> throw(inner) end
        catch
            throw:inner -> caught
        end,
    io:format("~p~n", [R]),
    S = "key=value",
    case S of
        "key=" ++ V -> io:format("value ~s~n", [V]);
        _ -> io:format("none~n")
    end,
    Fs = [fun() -> I * I end || I <- [1, 2, 3]],
    io:format("~p~n", [[F() || F <- Fs]]),
    io:format("~p~n", [lists:zipwith(fun(X, Y) -> X * Y end, [1, 2, 3], [4, 5, 6])]),
    io:format("~p~n", [lists:max([3, 1, 4, 1, 5])]),
    io:format("~p~n", [element(1, catch error(boom))]),
    io:format("~p~n", [string:join([integer_to_list(X) || X <- [1, 2, 3]], ", ")]),
    {T1, T2} = lists:split(2, [a, b, c, d]),
    io:format("~p ~p~n", [T1, T2]),
    X1 = 5, Pred = fun(X) when X > X1 -> true; (_) -> false end,
    io:format("~p~n", [lists:filter(Pred, lists:seq(1, 10))]),
    io:format("~p~n", [lists:sort([{K, V} || K := V <- #{a => 1, b => 2}])]),
    io:format("~p~n", [lists:sort(maps:to_list(#{K => V * 2 || K := V <- #{x => 1, y => 2}}))]),
    ok.
