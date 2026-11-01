-module(records).
-export([main/0]).
-record(person, {name, age = 0, email = none}).

older(#person{age = A} = P) -> P#person{age = A + 1}.

main() ->
    P = #person{name = "Ada", age = 36},
    io:format("~p~n", [P]),
    io:format("~p ~p~n", [P#person.name, P#person.age]),
    P2 = older(P),
    io:format("~p~n", [P2#person.age]),
    #person{name = N} = P2,
    io:format("~p~n", [N]),
    io:format("~p~n", [is_record(P, person)]),
    io:format("~p~n", [#person.age]),
    Ps = [#person{name = X} || X <- ["a", "b"]],
    io:format("~p~n", [[Q#person.name || Q <- Ps]]),
    ok.
