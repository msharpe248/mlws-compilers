-module(hello).
-export([main/0]).

main() ->
    N = length([X * X || X <- lists:seq(1, 20000), X rem 3 =:= 0]),
    io:format("Hello from Erlang ~p~n", [N]).
