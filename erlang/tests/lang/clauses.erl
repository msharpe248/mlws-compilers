%% Clauses in a row that begin with tuples of one size share the tuple test:
%% a clause failing after it (an element, a later argument, a guard) goes on
%% to the next clause's elements.
-module(clauses).
-export([main/0]).

f({a, X}, _) -> {first, X};
f({b, X}, Y) when X > Y -> {second, X};
f({b, X}, y) -> {third, X};
f({_, _}, Z) when is_list(Z) -> {fourth, Z};
f({X, Y, Z}, _) -> {triple, X, Y, Z};
f({_, _}, _) -> pair;
f(Other, _) -> {other, Other}.

g({leaf}) -> 1;
g({node, L, R}) -> g(L) + g(R);
g(_) -> 0.

main() ->
    io:format("~p~n", [[f({a, 1}, 0), f({b, 5}, 1), f({b, 1}, y), f({b, 1}, "s"),
                        f({c, 1}, [x]), f({1, 2, 3}, 0), f({b, 1}, 9), f(x, 0), f({}, 0)]]),
    io:format("~p~n", [g({node, {node, {leaf}, {leaf}}, {leaf}})]),
    io:format("~p~n", [g({node, x, {leaf}})]),
    io:format("~p~n", [try h({a, 2}) catch error:R -> R end]).

h({a, 1}) -> one;
h({b, _}) -> b.
