-module(patterns).
-export([main/0]).

fact(0) -> 1;
fact(N) when N > 0 -> N * fact(N - 1).

classify(X) when is_integer(X), X < 0 -> negative;
classify(0) -> zero;
classify(X) when is_integer(X) -> positive;
classify(X) when is_atom(X) -> atom;
classify({_, _}) -> pair;
classify([_ | _]) -> list;
classify(_) -> other.

len([]) -> 0;
len([_ | T]) -> 1 + len(T).

sum(L) -> sum(L, 0).
sum([H | T], Acc) -> sum(T, Acc + H);
sum([], Acc) -> Acc.

same(X, X) -> true;
same(_, _) -> false.

greet("hello " ++ Name) -> {greeting, Name};
greet(Other) -> {unknown, Other}.

area({circle, R}) -> 3.0 * R * R;
area({rect, W, H}) -> W * H;
area({square, S}) -> area({rect, S, S}).

main() ->
    io:format("~p~n", [fact(20)]),
    io:format("~p~n", [fact(30)]),
    io:format("~p~n", [[classify(X) || X <- [-3, 0, 5, foo, {a, b}, [1], 2.5]]]),
    io:format("~p ~p~n", [len([1, 2, 3]), sum([1, 2, 3, 4])]),
    io:format("~p ~p~n", [same(1, 1), same(1, 2)]),
    io:format("~p ~p~n", [greet("hello world"), greet("bye")]),
    io:format("~p ~p ~p~n", [area({circle, 2}), area({rect, 3, 4}), area({square, 5})]),
    {A, [B | C]} = {1, [2, 3, 4]},
    io:format("~p ~p ~p~n", [A, B, C]),
    case {ok, 42} of
        {ok, V} when V > 40 -> io:format("big ~p~n", [V]);
        {ok, V} -> io:format("small ~p~n", [V]);
        error -> io:format("error~n")
    end,
    R = if
            A > 5 -> big;
            A > 0 -> small;
            true -> zero
        end,
    io:format("~p~n", [R]),
    T = {point, 3, 4},
    {point, PX, PY} = T,
    io:format("~p~n", [PX * PY]),
    case [1, 2, 3] of
        [First | _] = Whole -> io:format("~p ~p~n", [First, Whole])
    end,
    Z = case 5 of
            N when N rem 2 =:= 0 -> even;
            _ -> odd
        end,
    io:format("~p~n", [Z]),
    io:format("~p~n", [guard_test(5)]),
    io:format("~p~n", [guard_test(a)]),
    io:format("~p~n", [guard_test({1, 2, 3})]),
    ok.

guard_test(X) when X > 3; is_atom(X) -> yes;
guard_test(X) when tuple_size(X) =:= 3 -> triple;
guard_test(_) -> no.
