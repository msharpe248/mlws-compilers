%% Lists built on a tail that is a call's result, a comprehension's
%% variable or an aliased argument, and heads that alias an argument.
-module(cons_tails).
-export([main/0]).

main() ->
    io:format("~p~n", [[1 | tl([0, 2, 3])]]),
    io:format("~p~n", [[a, b | lists:seq(1, 3)]]),
    io:format("~p~n", [[[1 | T] || T <- [[2], [3, 4], []]]]),
    io:format("~p~n", [[begin A = [1 | T], {A, T} end || T <- [[2], [3]]]]),
    io:format("~p~n", [[{[0 | T], T, [9 | T]} || T <- [[x], [y, z]]]]),
    io:format("~p~n", [[[H | T] || [H | _] = T <- [[1, 2], [], [3]]]]),
    io:format("~p~n", [interleave([1, 3, 5], [2, 4, 6, 8])]),
    io:format("~p~n", [alias([1, 2, 3], [4])]),
    io:format("~p~n", [alias2({p, 7}, [z])]),
    T = lists:reverse([3, 2]),
    L = [1 | T],
    io:format("~p ~p~n", [T, L]),
    F = fun(X) -> [X | lists:duplicate(2, X)] end,
    io:format("~p ~p~n", [F(q), [F(r) | F(s)]]).

interleave([X | Xs] = A, [Y | Ys] = B) ->
    if
        X =< Y -> [X | interleave(Xs, B)];
        true -> [Y | interleave(A, Ys)]
    end;
interleave([], B) -> B;
interleave(A, []) -> A.

alias([H | _] = L, [M] = R) -> {H, L, M, R, [H | L] ++ R}.

alias2(P = {p, N}, Acc) -> [P, N | Acc].
